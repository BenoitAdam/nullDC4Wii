#!/usr/bin/env python3
"""
Independent VQ+twiddle texture decoder, matching gxRend.cpp's algorithm
exactly (twop/twiddle_razi, codebook pixel order, RGB565 layout), to
render the captured raw VRAM dump as a real PNG for visual comparison.

Usage: python3 decode_vq.py dump.txt output.png
  dump.txt: pasted log containing [VQDUMP-HDR] and [VQDUMP] lines
"""
import sys
import re
from PIL import Image

def parse_dump(path):
    """Returns a dict {addr: (declared_len, bytes)} for every
    [VQDUMP-HDR]/[VQDUMP] block found in the file."""
    blocks = {}
    addr = None
    length = None
    data = None
    with open(path, 'r', errors='ignore') as f:
        for line in f:
            m = re.search(r'\[VQDUMP-HDR\]\s+addr=0x([0-9A-Fa-f]+)\s+len=(\d+)', line)
            if m:
                addr = int(m.group(1), 16)
                length = int(m.group(2))
                data = bytearray()
                blocks[addr] = (length, data)
                continue
            m = re.search(r'\[VQDUMP\]\s+\+([0-9A-Fa-f]+):\s+([0-9A-Fa-f ]+)', line)
            if m and data is not None:
                off = int(m.group(1), 16)
                hexbytes = m.group(2).split()
                chunk = bytes(int(b, 16) for b in hexbytes)
                if len(data) < off + len(chunk):
                    data.extend(b'\x00' * (off + len(chunk) - len(data)))
                data[off:off+len(chunk)] = chunk
    return {a: (l, bytes(d)) for a, (l, d) in blocks.items()}

def twop(x, y, x_sz, y_sz):
    """Exact port of gxRend.cpp's twop()/twiddle_razi()."""
    rv = 0
    sh = 0
    x_sz >>= 1
    y_sz >>= 1
    while x_sz != 0 or y_sz != 0:
        if y_sz:
            rv |= (y & 1) << sh
            y_sz >>= 1
            y >>= 1
            sh += 1
        if x_sz:
            rv |= (x & 1) << sh
            x_sz >>= 1
            x >>= 1
            sh += 1
    return rv

def rgb565_to_rgb888(px):
    r = (px >> 11) & 0x1F
    g = (px >> 5) & 0x3F
    b = px & 0x1F
    r = (r * 255) // 31
    g = (g * 255) // 63
    b = (b * 255) // 31
    return (r, g, b)

def decode_vq_mip(codebook, idx_data, w, h):
    """
    codebook: 2048 bytes (256 entries x 8 bytes).
    idx_data: index-map bytes for this WxH level, starting at offset 0.
    Returns a w x h RGB image (PIL.Image).
    """
    img = Image.new('RGB', (w, h))
    px = img.load()

    divider = 4  # 2x2 block
    for y in range(0, h, 2):
        for x in range(0, w, 2):
            block_idx = twop(x, y, w, h) // divider
            # gxRend reads: p_in[(offset_y|table_x[x]) ^ 3]
            # Apply ^3 at byte level relative to a 4-byte-aligned group:
            phys = (block_idx & ~3) | ((block_idx & 3) ^ 3)
            idx = idx_data[phys]

            cb = codebook[idx*8: idx*8+8]
            # host_ptr_xor(u16*) reads ^2: s0=bytes[2:4], s1=bytes[0:2],
            # s2=bytes[6:8], s3=bytes[4:6] -- all as big-endian u16.
            s0 = (cb[2] << 8) | cb[3]
            s1 = (cb[0] << 8) | cb[1]
            s2 = (cb[6] << 8) | cb[7]
            s3 = (cb[4] << 8) | cb[5]

            px[x,   y]   = rgb565_to_rgb888(s0)  # (0,0)
            px[x,   y+1] = rgb565_to_rgb888(s1)  # (0,1)
            px[x+1, y]   = rgb565_to_rgb888(s2)  # (1,0)
            px[x+1, y+1] = rgb565_to_rgb888(s3)  # (1,1)
    return img

if __name__ == '__main__':
    in_path, out_path = sys.argv[1], sys.argv[2]
    w = int(sys.argv[3]) if len(sys.argv) > 3 else 512
    h = w
    target_addr = int(sys.argv[4], 16) if len(sys.argv) > 4 else None

    blocks = parse_dump(in_path)
    print(f"Found {len(blocks)} block(s) in dump:")
    for a, (l, d) in blocks.items():
        print(f"  addr=0x{a:06X} declared_len={l} actual_bytes={len(d)}")

    if target_addr is not None:
        if target_addr not in blocks:
            print(f"ERROR: addr=0x{target_addr:06X} not found in dump.")
            sys.exit(1)
        addr, (length, data) = target_addr, blocks[target_addr]
    else:
        # No address given: pick the block whose size best matches the
        # requested width's expected full data size (codebook + all
        # smaller mips + this level's index map).
        VQMipPoint = [2048 + v for v in
                      [0x00000, 0x00001, 0x00002, 0x00006, 0x00016, 0x00056,
                       0x00156, 0x00556, 0x01556, 0x05556, 0x15556]]
        import math
        mip_idx_guess = int(math.log2(w))
        expected_len = VQMipPoint[mip_idx_guess] + (w * h // 4)
        addr = min(blocks, key=lambda a: abs(len(blocks[a][1]) - expected_len))
        length, data = blocks[addr]
        print(f"No address given; auto-selected addr=0x{addr:06X} "
              f"(closest match to expected size {expected_len} for w={w})")

    print(f"Using addr=0x{addr:06X} len={length} actual_bytes={len(data)}")

    VQMipPoint = [2048 + v for v in
                  [0x00000, 0x00001, 0x00002, 0x00006, 0x00016, 0x00056,
                   0x00156, 0x00556, 0x01556, 0x05556, 0x15556]]
    import math
    mip_idx = int(math.log2(w))
    idx_offset = VQMipPoint[mip_idx]
    print(f"w=h={w}, mip_idx={mip_idx}, idx_offset=0x{idx_offset:X}, "
          f"need >= {idx_offset + w*h//4} bytes")

    if len(data) < idx_offset + w*h//4:
        print(f"ERROR: dump too short for w={w} (have {len(data)} bytes, "
              f"need {idx_offset + w*h//4}). Wrong block or truncated dump?")
        sys.exit(1)

    idx_data = data[idx_offset:]
    codebook = data[0:2048]

    img = decode_vq_mip(codebook, idx_data, w, h)
    img.save(out_path)
    print(f"Saved {out_path}")
