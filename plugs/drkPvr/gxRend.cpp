// Wii Rendering



// ============================================================================
// RUNTIME - PRESET SELECTION
// ============================================================================

// This is defined in main.cpp
extern "C" int get_accuracy_preset();

// Helper macros to check current accuracy mode
#define FAST() (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

// This is defined in main.cpp
extern "C" int get_graphism_preset();

// Helper macros to check current graphism mode
#define LOW() (get_graphism_preset() == 0)
#define NORMAL() (get_graphism_preset() == 1)
#define HIGH() (get_graphism_preset() == 2)
#define EXTRA() (get_graphism_preset() == 3)

// This is defined in main.cpp
extern "C" int get_ratio_preset();

// Helper macros to check current graphism mode
#define ORIGINAL() (get_ratio_preset() == 0)
#define FULLSCREEN() (get_ratio_preset() == 1)


// This is defined in main.cpp
extern "C" int get_debug_loop();

// This is defined in nullDC.cpp
extern int g_current_frameskip; // 0 = no skip, 1 = skip 1 frame, 2 = skip 2 frame
extern int g_frame_counter;



#include "config.h"
#include "gxRend.h"
#include <gccore.h>
#include <malloc.h>
#include "regs.h"
#include "wii/wii_audio.h"


// The FIFO is the command buffer for the GX hardware. 
// 256KB is a standard size for most homebrew applications.
#define DEFAULT_FIFO_SIZE (256 * 1024)

using namespace TASplitter;

u8 *vram_buffer;

// Double buffering setup: frameBuffer[0] and [1] prevent screen tearing.
static void *frameBuffer[2] = {NULL, NULL};
static GXRModeObj *rmode;
static u8 gp_fifo[DEFAULT_FIFO_SIZE] __attribute__((aligned(32)));
static int fb = 0; // Current framebuffer index

// Set once at startup or when preset changes
static u8 min_filt, mag_filt, bias_clamp, edge_lod, aniso;
static f32 lod_bias;

void ApplyGraphismPreset() {
  switch (get_graphism_preset()) {
    case 0: min_filt = GX_NEAR; mag_filt = GX_NEAR; lod_bias = 0.0f;
            bias_clamp = GX_DISABLE; edge_lod = GX_DISABLE; aniso = GX_ANISO_1; break;
    case 1: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = 0.0f;
            bias_clamp = GX_DISABLE; edge_lod = GX_DISABLE; aniso = GX_ANISO_1; break;
    case 2: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = -0.5f;
            bias_clamp = GX_ENABLE; edge_lod = GX_ENABLE; aniso = GX_ANISO_2; break;
    case 3: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = -1.0f;
            bias_clamp = GX_ENABLE; edge_lod = GX_ENABLE; aniso = GX_ANISO_4; break;
    default: min_filt = GX_LINEAR; mag_filt = GX_LINEAR; lod_bias = 0.0f;
              bias_clamp = GX_DISABLE; edge_lod = GX_DISABLE; aniso = GX_ANISO_1; break;
  }
}

// Macro to convert ABGR (standard for some PC/PVR formats) to RGBA/Other
// #define ABGR8888(x) ((x & 0xFF00FF00) | ((x >> 16) & 0xFF) | ((x & 0xFF) << 16)) // already defined elsewhere
/*
  Texture Format Conversion Notes:
  FMT: 1555 DTP    -> RGB5A3 DP
     4444 DTP    -> RGB5A3 DP

     8888 P      -> 8888 P
     565  DTP    -> 565 DP
     YUV  DT     -> ? 565 is possibe but LQ ...

*/




// Macros for extracting color channels from Dreamcast-specific pixel formats.
#define ABGR4444_A(x) ((x) >> 12)
#define ABGR4444_R(x) ((x >> 8) & 0xF)
#define ABGR4444_G(x) ((x >> 4) & 0xF)
#define ABGR4444_B(x) ((x) & 0xF)

#define ABGR0565_R(x) ((x) >> 11)
#define ABGR0565_G(x) ((x >> 5) & 0x3F)
#define ABGR0565_B(x) ((x) & 0x1F)

#define ABGR1555_A(x) ((x >> 15))
#define ABGR1555_R(x) ((x >> 10) & 0x1F)
#define ABGR1555_G(x) ((x >> 5) & 0x1F)
#define ABGR1555_B(x) ((x) & 0x1F)
#define MAKE_555A3

#define MAKE_565(r, g, b, a)
#define MAKE_1555(r, g, b, a)
#define MAKE_4444(r, g, b, a)

// Dreamcast stores pixels as ABGR (alpha-blue-green-red from MSB).
// Wii GX expects RGB (red-green-blue) in the same bit positions.
// These macros swap R and B channels to correct the color output.

// RGB565 (DC: R5G6B5) → GX RGB565: identical layout, no conversion needed
#define ABGR0565(x) (x)

// DC ARGB1555 bit layout matches GX RGB5A3 bit15 directly — pass through as-is
#define ABGR1555(x) (x)

// ARGB4444 (DC: A4 R4 G4 B4) → GX RGB5A3
// ARGB4444 has 4 alpha (transparency) bit, Wii's RGB5A3 has 3 bits
// Truncate A4 → A3, keep R4 G4 B4, force bit15=0 (blended mode).
#define ABGR4444(x) ( 0x0000u                             \
    | ((((x) & 0xF000u) >> 1) & 0x7000u)  /* A4 → A3 */  \
    | ((x) & 0x0FFFu) )                   /* R4 G4 B4 */

// ABGR8888 → RGBA8888: swap R (bits 16-23) and B (bits 0-7)
#define ABGR8888(x) ( ((x) & 0xFF00FF00u)          \
                    | (((x) & 0x00FF0000u) >> 16)   \
                    | (((x) & 0x000000FFu) << 16) )

#define colclamp(low, hi, val) \
  {                            \
    if (val < low)             \
      val = low;               \
    if (val > hi)              \
      val = hi;                \
  }


// Vertex structure used to feed the Wii's GX pipeline.
struct Vertex
{
  float u, v;        // Texture coordinates
  unsigned int col;  // Vertex color
  float x, y, z;     // 3D coordinates
};

// Structures to manage the internal drawing lists and state 
// passed from the Dreamcast's Tile Accelerator.
struct VertexList
{
  union
  {
    Vertex *ptr;
    s32 count;
  };
};

struct PolyParam
{
  PCW pcw;
  ISP_TSP isp;

  TSP tsp;
  TCW tcw;
};

struct TextureCacheDesc
{
  GXTexObj tex;
  GXTlutObj pal;
  u32 addr;
  bool has_pal;
};

void VBlank() {}

// Static arrays for vertex data to avoid frequent heap allocations.
// Limited by the Wii's MEM1/MEM2 availability.
Vertex ALIGN16 vertices[43008]; // 42*1024 = Wii memory limit
VertexList ALIGN16 lists[8192]; // 8*1024
PolyParam ALIGN16 listModes[8192]; // 8*1024

Vertex *curVTX = vertices;
VertexList *curLST = lists;
VertexList *TransLST = 0;
PolyParam *curMod = listModes;
bool global_regd;
float vtx_min_Z;
float vtx_max_Z;

char fps_text[512];

struct VertexDecoder;
FifoSplitter<VertexDecoder> TileAccel;

union _ISP_BACKGND_T_type
{
  struct
  {
    u32 tag_offset : 3;
    u32 tag_address : 21;
    u32 skip : 3;
    u32 shadow : 1;
    u32 cache_bypass : 1;
  };
  u32 full;
};

union _ISP_BACKGND_D_type
{
  u32 i;
  f32 f;
};

// Conversion logic to translate Dreamcast VRAM addressing (32-bit interleaved)
// into a linear 64-bit bank structure compatible with the emulator's memory map.
static INLINE u32 fast_ConvOffset32toOffset64(u32 offset32)
{
  offset32 &= VRAM_MASK;
  u32 bank = ((offset32 >> 22) & 0x1) << 2;
  u32 lower = offset32 & 0x3;
  u32 addr_shifted = (offset32 & 0x3FFFFC) << 1;
  return addr_shifted | bank | lower;
}

// Helpers to read float/int values directly from the virtualized PVR VRAM.
f32 vrf(u32 addr)
{
  return *(f32 *)&params.vram[fast_ConvOffset32toOffset64(addr)];
}

u32 vri(u32 addr)
{
  return *(u32 *)&params.vram[fast_ConvOffset32toOffset64(addr)];
}

// Converts 16-bit PVR UV coordinates to 32-bit floats.
static f32 CVT16UV(u32 uv)
{
  uv <<= 16;
  return *(f32 *)&uv;
}

// Primary decoder that translates raw PVR vertex data into the 'Vertex' struct.
void decode_pvr_vertex(u32 base, u32 ptr, Vertex *cv)
{
  // ISP
  // TSP
  // TCW
  ISP_TSP isp;
  TSP tsp;
  TCW tcw;

  isp.full = vri(base);
  tsp.full = vri(base + 4);
  tcw.full = vri(base + 8);
  (void)tsp;
  (void)tcw; // Currently unused but may be needed later

  // Read coordinates: PVR positions are typically already transformed.
  // XYZ
  // UV
  // Base Col
  // Offset Col

  // XYZ are _allways_ there :)
  cv->x = vrf(ptr);
  ptr += 4;
  cv->y = vrf(ptr);
  ptr += 4;
  cv->z = vrf(ptr);
  ptr += 4;

  // Handle Texture Coordinates (UVs)
  if (isp.Texture)
  { // Do texture , if any
    if (isp.UV_16b)
    {
      u32 uv = vri(ptr);
      cv->u = CVT16UV((u16)uv);
      cv->v = CVT16UV((u16)(uv >> 16));
      ptr += 4;
    }
    else
    {
      cv->u = vrf(ptr);
      ptr += 4;
      cv->v = vrf(ptr);
      ptr += 4;
    }
  }

  // Handle Vertex Color
  u32 col = vri(ptr);
  ptr += 4;
  cv->col = col; // ABGR8888(col);
  if (isp.Offset)
  {
    // Skip offset color for now (used for specular-like highlights)
    (void)vri(ptr);
    ptr += 4;
     //	vert_packed_color_(cv->spc,col);
  }
}

// Resets internal pointers for the next frame's vertex list.
void reset_vtx_state()
{
  curVTX = vertices;
  curLST = lists;
  curMod = listModes;
  global_regd = false;
  vtx_min_Z = 131072; // 128 * 1024 // if someone uses more, i realy realy dont care
  vtx_max_Z = 0;      // lower than 0 is invalid for pvr .. i wonder if SA knows that.
}

#define VTX_TFX(x) (x)
#define VTX_TFY(y) (y)

// The Dreamcast uses "Twiddled" (Morton Order) textures to improve cache locality.
// This function converts linear X/Y coordinates into the twiddled memory address.
// input : address in the yyyyyxxxxx format
// output : address in the xyxyxyxy format
// U : x resolution , V : y resolution
// twidle works on 64b words
u32 fastcall twop(u32 x, u32 y, u32 x_sz, u32 y_sz)
{
  u32 rv = 0;
  u32 sh = 0;
  x_sz >>= 1;
  y_sz >>= 1;
  while (x_sz != 0 || y_sz != 0)
  {
    if (y_sz)
    {
      u32 temp = y & 1;
      rv |= temp << sh;
      y_sz >>= 1;
      y >>= 1;
      sh++;
    }
    if (x_sz)
    {
      u32 temp = x & 1;
      rv |= temp << sh;
      x_sz >>= 1;
      x >>= 1;
      sh++;
    }
  }
  return rv;
}

// Handler functions
// Calculates the offset for a Wii texture in TPL/GX block format.
u32 GX_TexOffs(u32 x, u32 y, u32 w)
{
  w /= 4;
  u32 xs = x & 3;
  x >>= 2;
  u32 ys = y & 3;
  y >>= 2;

  return (y * w + x) * 16 + (ys * 4 + xs);
}

// converts Dreamcast "twiddled" textures to Wii GX block format.
// NOTE: This simple version is NOT USED - see texture_TW<class> template at line ~665
// which properly handles color conversion through pixel converter classes.
// Kept here for reference only.
/*
template<int pixel_format>
void fastcall texture_TW_UNUSED(u8* p_out, u8* p_in, u32 Width, u32 Height)
{
  ... removed to avoid compilation errors ...
}
*/


// Converts YUV422 data to RGB565 for Wii compatibility.
u32 YUV422(s32 Y, s32 Yu, s32 Yv)
{
  s32 B = (76283 * (Y - 16) + 132252 * (Yu - 128)) >> (16 + 3);                     // 5
  s32 G = (76283 * (Y - 16) - 53281 * (Yv - 128) - 25624 * (Yu - 128)) >> (16 + 2); // 6
  s32 R = (76283 * (Y - 16) + 104595 * (Yv - 128)) >> (16 + 3);                     // 5

  colclamp(0, 0x1F, B);
  colclamp(0, 0x3F, G);
  colclamp(0, 0x1F, R);

  return (R << 11) | (G << 5) | (B);  // RGB565 format
}

// Infrastructure for fast pixel conversion routines using static polymorphism/templates.
#define pixelcvt_start(name, xa, yb)                                                     \
  struct name                                                                            \
  {                                                                                      \
    static const u32 xpp = xa;                                                           \
    static const u32 ypp = yb;                                                           \
    __forceinline static void fastcall Convert(u16 *pb, u32 x, u32 y, u32 pbw, u8 *data) \
    {

#define pixelcvt_end \
  }                  \
  }
#define pixelcvt_next(name, x, y) \
  pixelcvt_end;                   \
  pixelcvt_start(name, x, y)

#define pixelcvt_startVQ(name, x, y)                     \
  struct name                                            \
  {                                                      \
    static const u32 xpp = x;                            \
    static const u32 ypp = y;                            \
    __forceinline static u32 fastcall Convert(u16 *data) \
    {

#define pixelcvt_endVQ \
  }                    \
  }
#define pixelcvt_nextVQ(name, x, y) \
  pixelcvt_endVQ;                   \
  pixelcvt_startVQ(name, x, y)

inline void pb_prel(u16 *dst, u32 pbw, u32 x, u32 y, u32 col)
{
  dst[GX_TexOffs(x, y, pbw)] = col;
}

// ===============
// Pixel Converters for Non-Twiddled (Planar) textures.
// ===============
pixelcvt_start(conv565_PL, 4, 1)
{
  // convert 4x1 565 to 4x1 8888
  u16 *p_in = (u16 *)data;
  // 0,0
  pb_prel(pb, pbw, x + 0, y, ABGR0565(p_in[0]));
  // 1,0
  pb_prel(pb, pbw, x + 1, y, ABGR0565(p_in[1]));
  // 2,0
  pb_prel(pb, pbw, x + 2, y, ABGR0565(p_in[2]));
  // 3,0
  pb_prel(pb, pbw, x + 3, y, ABGR0565(p_in[3]));
}
pixelcvt_next(conv1555_PL, 4, 1)
{
  // convert 4x1 1555 to 4x1 8888
  u16 *p_in = (u16 *)data;
  // 0,0
  pb_prel(pb, pbw, x + 0, y, ABGR1555(p_in[0]));
  // 1,0
  pb_prel(pb, pbw, x + 1, y, ABGR1555(p_in[1]));
  // 2,0
  pb_prel(pb, pbw, x + 2, y, ABGR1555(p_in[2]));
  // 3,0
  pb_prel(pb, pbw, x + 3, y, ABGR1555(p_in[3]));
}
pixelcvt_next(conv4444_PL, 4, 1)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;
  // 0,0
  pb_prel(pb, pbw, x + 0, y, ABGR4444(p_in[0]));
  // 1,0
  pb_prel(pb, pbw, x + 1, y, ABGR4444(p_in[1]));
  // 2,0
  pb_prel(pb, pbw, x + 2, y, ABGR4444(p_in[2]));
  // 3,0
  pb_prel(pb, pbw, x + 3, y, ABGR4444(p_in[3]));
}
pixelcvt_next(convYUV_PL, 4, 1)
{
  // convert 4x1 4444 to 4x1 8888
  u32 *p_in = (u32 *)data;

  s32 Y0 = (p_in[0] >> 8) & 255;  //
  s32 Yu = (p_in[0] >> 0) & 255;  // p_in[0]
  s32 Y1 = (p_in[0] >> 24) & 255; // p_in[3]
  s32 Yv = (p_in[0] >> 16) & 255; // p_in[2]

  // 0,0
  pb_prel(pb, pbw, x + 0, y, YUV422(Y0, Yu, Yv));
  // 1,0
  pb_prel(pb, pbw, x + 1, y, YUV422(Y1, Yu, Yv));

   // next 4 bytes
  p_in += 1;

  Y0 = (p_in[0] >> 8) & 255;  //
  Yu = (p_in[0] >> 0) & 255;  // p_in[0]
  Y1 = (p_in[0] >> 24) & 255; // p_in[3]
  Yv = (p_in[0] >> 16) & 255; // p_in[2]

  // 0,0
  pb_prel(pb, pbw, x + 2, y, YUV422(Y0, Yu, Yv));
  // 1,0
  pb_prel(pb, pbw, x + 3, y, YUV422(Y1, Yu, Yv));
}
pixelcvt_end;

// Pixel Converters for Twiddled textures.
pixelcvt_start(conv565_TW, 2, 2)
{
    // convert 4x1 565 to 4x1 8888
  u16 *p_in = (u16 *)data;
  // 0,0
  pb_prel(pb, pbw, x + 0, y + 0, ABGR0565(p_in[0]));
  // 0,1
  pb_prel(pb, pbw, x + 0, y + 1, ABGR0565(p_in[1]));
  // 1,0
  pb_prel(pb, pbw, x + 1, y + 0, ABGR0565(p_in[2]));
  // 1,1
  pb_prel(pb, pbw, x + 1, y + 1, ABGR0565(p_in[3]));
}
pixelcvt_next(conv1555_TW, 2, 2)
{
  // convert 4x1 1555 to 4x1 8888
  u16 *p_in = (u16 *)data;
  // 0,0
  pb_prel(pb, pbw, x + 0, y + 0, ABGR1555(p_in[0]));
  // 0,1
  pb_prel(pb, pbw, x + 0, y + 1, ABGR1555(p_in[1]));
  // 1,0
  pb_prel(pb, pbw, x + 1, y + 0, ABGR1555(p_in[2]));
  // 1,1
  pb_prel(pb, pbw, x + 1, y + 1, ABGR1555(p_in[3]));
}
pixelcvt_next(conv4444_TW, 2, 2)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;
  // 0,0
  pb_prel(pb, pbw, x + 0, y + 0, ABGR4444(p_in[0]));
  // 0,1
  pb_prel(pb, pbw, x + 0, y + 1, ABGR4444(p_in[1]));
  // 1,0
  pb_prel(pb, pbw, x + 1, y + 0, ABGR4444(p_in[2]));
  // 1,1
  pb_prel(pb, pbw, x + 1, y + 1, ABGR4444(p_in[3]));
}
pixelcvt_next(convYUV422_TW, 2, 2)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;

 s32 Y0 = (p_in[0] >> 8) & 255; //
  s32 Yu = (p_in[0] >> 0) & 255; // p_in[0]
  s32 Y1 = (p_in[2] >> 8) & 255; // p_in[3]
  s32 Yv = (p_in[2] >> 0) & 255; // p_in[2]

  // 0,0
  pb_prel(pb, pbw, x + 0, y + 0, YUV422(Y0, Yu, Yv));
  // 1,0
  pb_prel(pb, pbw, x + 1, y + 0, YUV422(Y1, Yu, Yv));

  // next 4 bytes
  // p_in+=2;

  Y0 = (p_in[1] >> 8) & 255; //
  Yu = (p_in[1] >> 0) & 255; // p_in[0]
  Y1 = (p_in[3] >> 8) & 255; // p_in[3]
  Yv = (p_in[3] >> 0) & 255; // p_in[2]

 // 0,1
  pb_prel(pb, pbw, x + 0, y + 1, YUV422(Y0, Yu, Yv));
  // 1,1
  pb_prel(pb, pbw, x + 1, y + 1, YUV422(Y1, Yu, Yv));
}
pixelcvt_end;

// VQ (Vector Quantization) Palette Converters. 
// These handle PVR's compressed texture formats by averaging or selecting colors from the codebook.
pixelcvt_startVQ(conv565_VQ, 2, 2)
{
  // Just use first pixel, no averaging (TEST) : no change with return data[0];
  // return data[0]; // Delete above and keep this. ACCURACY Opportunity ?
  u32 R = ABGR0565_R(data[0]) + ABGR0565_R(data[1]) + ABGR0565_R(data[2]) + ABGR0565_R(data[3]);
  u32 G = ABGR0565_G(data[0]) + ABGR0565_G(data[1]) + ABGR0565_G(data[2]) + ABGR0565_G(data[3]);
  u32 B = ABGR0565_B(data[0]) + ABGR0565_B(data[1]) + ABGR0565_B(data[2]) + ABGR0565_B(data[3]);
  R >>= 2;
  G >>= 2;
  B >>= 2;

  return R | (G << 5) | (B << 11);
}

pixelcvt_nextVQ(conv1555_VQ, 2, 2)
{
  // Just use first pixel, no averaging (TEST) : no change with return data[0];
  // return data[0]; // Delete above and keep this. ACCURACY Opportunity ?
  u32 R = ABGR1555_R(data[0]) + ABGR1555_R(data[1]) + ABGR1555_R(data[2]) + ABGR1555_R(data[3]);
  u32 G = ABGR1555_G(data[0]) + ABGR1555_G(data[1]) + ABGR1555_G(data[2]) + ABGR1555_G(data[3]);
  u32 B = ABGR1555_B(data[0]) + ABGR1555_B(data[1]) + ABGR1555_B(data[2]) + ABGR1555_B(data[3]);
  u32 A = ABGR1555_A(data[0]) + ABGR1555_A(data[1]) + ABGR1555_A(data[2]) + ABGR1555_A(data[3]);
  R >>= 2;
  G >>= 2;
  B >>= 2;
  A >>= 2;

  return R | (G << 5) | (B << 10) | (A << 15);

    // return ABGR1555(data[0]);
  /*
  //convert 4x1 1555 to 4x1 8888
  u16* p_in=(u16*)data;
  //0,0
  pb_prel(pb,pbw,0,0,ABGR1555(p_in[0]));
  //0,1
  pb_prel(pb,pbw,0,1,ABGR1555(p_in[1]));
  //1,0
  pb_prel(pb,pbw,1,0,ABGR1555(p_in[2]));
  //1,1
  pb_prel(pb,pbw,1,1,ABGR1555(p_in[3]));
  */
}
pixelcvt_nextVQ(conv4444_VQ, 2, 2)
{
  // Just use first pixel, no averaging (TEST) : no change with return data[0];
  // return data[0]; // Delete above and keep this. ACCURACY Opportunity ?

  u32 R = ABGR4444_R(data[0]) + ABGR4444_R(data[1]) + ABGR4444_R(data[2]) + ABGR4444_R(data[3]);
  u32 G = ABGR4444_G(data[0]) + ABGR4444_G(data[1]) + ABGR4444_G(data[2]) + ABGR4444_G(data[3]);
  u32 B = ABGR4444_B(data[0]) + ABGR4444_B(data[1]) + ABGR4444_B(data[2]) + ABGR4444_B(data[3]);
  u32 A = ABGR4444_A(data[0]) + ABGR4444_A(data[1]) + ABGR4444_A(data[2]) + ABGR4444_A(data[3]);
  R >>= 2;
  G >>= 2;
  B >>= 2;
  A >>= 2;

  return R | (G << 4) | (B << 8) | (A << 12);
  // return ABGR4444(data[0]);
  /*
  //convert 4x1 4444 to 4x1 8888
  u16* p_in=(u16*)data;
  //0,0
  pb_prel(pb,pbw,0,0,ABGR4444(p_in[0]));
  //0,1
  pb_prel(pb,pbw,0,1,ABGR4444(p_in[1]));
  //1,0
  pb_prel(pb,pbw,1,0,ABGR4444(p_in[2]));
  //1,1
  pb_prel(pb,pbw,1,1,ABGR4444(p_in[3]));
  */
}
pixelcvt_nextVQ(convYUV422_VQ, 2, 2)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;
  
  s32 Y0 = (p_in[0] >> 8) & 255; //
  s32 Yu = (p_in[0] >> 0) & 255; // p_in[0]
  s32 Y1 = (p_in[2] >> 8) & 255; // p_in[3]
  s32 Yv = (p_in[2] >> 0) & 255; // p_in[2]

  return YUV422(16 + ((Y0 - 16) + (Y1 - 16)) / 2.0f, Yu, Yv);
  /*
  //0,0
  pb_prel(pb,pbw,0,0,YUV422(Y0,Yu,Yv));
  //1,0
  pb_prel(pb,pbw,1,0,YUV422(Y1,Yu,Yv));

  //next 4 bytes
  //p_in+=2;

  Y0 = (p_in[1]>>8) &255; //
  Yu = (p_in[1]>>0) &255; //p_in[0]
  Y1 = (p_in[3]>>8) &255; //p_in[3]
  Yv = (p_in[3]>>0) &255; //p_in[2]

  //0,1
  pb_prel(pb,pbw,0,1,YUV422(Y0,Yu,Yv));
  //1,1
  pb_prel(pb,pbw,1,1,YUV422(Y1,Yu,Yv));*/
}
pixelcvt_endVQ;

// input : address in the yyyyyxxxxx format
// output : address in the xyxyxyxy format
// U : x resolution , V : y resolution
// Redefinition of twiddle for local use. (64b words)
u32 fastcall twiddle_razi(u32 x, u32 y, u32 x_sz, u32 y_sz)
{
  // u32 rv2=twiddle_optimiz3d(raw_addr,U);
  u32 rv = 0; // raw_addr & 3;//low 2 bits are directly passed  -> needs some misc stuff to work.However
  // Pvr internaly maps the 64b banks "as if" they were twidled :p

  // verify(x_sz==y_sz);
  u32 sh = 0;
  x_sz >>= 1;
  y_sz >>= 1;
  while (x_sz != 0 || y_sz != 0)
  {
    if (y_sz)
    {
      u32 temp = y & 1;
      rv |= temp << sh;
      y_sz >>= 1; y >>= 1; sh++;
    }
    if (x_sz)
    {
      u32 temp = x & 1;
      rv |= temp << sh;
      x_sz >>= 1; x >>= 1; sh++;
    }
  }
  return rv;
}

#define twop twiddle_razi
u8 *VramWork;

// Texture untwiddling and conversion template. (Handler)
template <class PixelConvertor>
void fastcall texture_TW(u8 *p_in, u32 Width, u32 Height)
{
  //		u32 p=0;
  u8 *pb = VramWork;
  // pb->amove(0,0);

  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp;

  for (u32 y = 0; y < Height; y += PixelConvertor::ypp)
  {
    for (u32 x = 0; x < Width; x += PixelConvertor::xpp)
    {
      u8 *p = &p_in[(twop(x, y, Width, Height) / divider) << 3];
      PixelConvertor::Convert((u16 *)pb, x, y, Width, p);

      // pb->rmovex(PixelConvertor::xpp);
      // pb+=PixelConvertor::xpp*2;
    }
    // pb->rmovey(PixelConvertor::ypp);
    // pb+=Width*(PixelConvertor::ypp-1)*2;
  }
  memcpy(p_in, VramWork, Width * Height * 2);
}

// Vector Quantization texture conversion template.
template <class PixelConvertor>
void fastcall texture_VQ(u8 *p_in, u32 Width, u32 Height, u8 *vq_codebook)
{
  // p_in+=256*4*2;
  //		u32 p=0;
  // VQ codebook to palette conversion
  // Codebook: 256 entries × 4 pixels (2x2 blocks) = 1024 pixels
  // Palette: 256 entries (one averaged/selected pixel per entry)

  u8 *pb = VramWork;
  // pb->amove(0,0);
  // Convert VQ cb to PAL8
  u16 *pal_cb = (u16 *)vq_codebook;

  // Convert codebook entries to palette - write to BEGINNING of codebook
  // This works because we read 4 pixels (entries 0-3) to make palette[0],
  // then read 4 pixels (entries 4-7) to make palette[1], etc.
  // So we're always reading ahead of where we're writing
  for (u32 palidx = 0; palidx < 256; palidx++)
  {
    pal_cb[palidx] = PixelConvertor::Convert(&pal_cb[palidx * 4]);
  }
  // Height/=PixelConvertor::ypp;
  // Width/=PixelConvertor::xpp;
  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp;
    // In texture_VQ, after the for loop at line ~662:
    // if g_debug = 1 (need implementation)
    // printf("First 16 palette entries:\n");
    // for (int i = 0; i < 16; i++) {
    //   printf("  [%d] = %04X\n", i, pal_cb[i]);
    // }

  for (u32 y = 0; y < Height; y += PixelConvertor::ypp)
  {
    for (u32 x = 0; x < Width; x += PixelConvertor::xpp)
    {
      u8 p = p_in[twop(x, y, Width, Height) / divider];
      // PixelConvertor::Convert((u16*)pb,Width,&vq_codebook[p*8]);
      //*pb=p;
      pb[GX_TexOffs(x, y, Width)] = p;
      // pb->rmovex(PixelConvertor::xpp);
      // pb+=1;
    }
    // pb->rmovey(PixelConvertor::ypp);
    // pb+=Width*(1-1);
  }
  // align up to 16 bytes
  u32 p_in_int = (u32)p_in;
  p_in_int &= ~15;
  p_in = (u8 *)p_in_int;

  memcpy(p_in, VramWork, Width * Height / divider);
}

// Planar (Linear) texture conversion.
template <int type>
void Plannar(u8 *praw, u32 w, u32 h)
{
  u16 *ptr = (u16 *)praw;
  u16 *dst = (u16 *)VramWork;
  u32 x, y;

  for (y = 0; y < h; y++)
  {
    for (x = 0; x < w; x++)
    {
      u32 col = *ptr++;
      switch (type)
      {
      case 1555:
        dst[GX_TexOffs(x, y, w)] = ABGR1555(col);
        break;
      case 565:
        dst[GX_TexOffs(x, y, w)] = ABGR0565(col);
        break;
      case 4444:
        dst[GX_TexOffs(x, y, w)] = ABGR4444(col);
        break;
      case 422:
      {
        s32 Y0 = (col >> 8) & 255; //
        s32 Yu = (col >> 0) & 255; // p_in[0]
        u32 col = *ptr++;
        s32 Y1 = (col >> 8) & 255; // p_in[3]
        s32 Yv = (col >> 0) & 255; // p_in[2]

        dst[GX_TexOffs(x, y, w)] = YUV422(Y0, Yu, Yv);
        x++;
        dst[GX_TexOffs(x, y, w)] = YUV422(Y1, Yu, Yv);
      }
      break;
      }
    }
  }
  // Copy GX-block data back to praw (= params.vram[tex_addr]).
  // texture_TW already does this same memcpy; Plannar must match it so that
  // GX_InitTexObj can be given ptex (params.vram, valid Wii/Dolphin memory)
  // instead of dst (vram_buffer, potentially a host-heap pointer Dolphin rejects).
  memcpy(praw, VramWork, w * h * 2);
}

// =========================
// Palette management for indexed textures.
// =========================

void SetupPaletteForTexture(u32 palette_index, u32 sz)
{
  palette_index &= ~(sz - 1);
  u32 fmtpal = PAL_RAM_CTRL & 3;

  if (fmtpal < 3)
    palette_index >>= 1;

    // sceGuClutMode(PalFMT[fmtpal],0,0xFF,0);//or whatever
    // sceGuClutLoad(sz/8,&palette_lut[palette_index]);
}

// PVR Mipmap offsets (Dreamcast specific).
const u32 MipPoint[8] =
    {
        0x00006, // 8
        0x00016, // 16
        0x00056, // 32
        0x00156, // 64
        0x00556, // 128
        0x01556, // 256
        0x05556, // 512
        0x15556  // 1024
};

// Macro to encapsulate the logic for twiddled textures (VQ or standard).
#define twidle_tex(format)                                                                \
  if (mod->tcw.NO_PAL.VQ_Comp)                                                            \
  {                                                                                       \
    vq_codebook = (u8 *)&params.vram[tex_addr];                                           \
    tex_addr += 2048; /* 256 * 4 * 2 */                                                         \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
    { /*int* p=0;*p=4;*/                                                                  \
      tex_addr += MipPoint[mod->tsp.TexU];                                                  \
    }                                                                                     \
    texture_VQ<conv##format##_VQ> /**/ ((u8 *)&params.vram[tex_addr], w, h, vq_codebook); \
    texVQ = 1;                                                                            \
  }                                                                                       \
  else                                                                                    \
  {                                                                                       \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
      tex_addr += MipPoint[mod->tsp.TexU] << 3;                                           \
    texture_TW<conv##format##_TW> /*TW*/ ((u8 *)&params.vram[tex_addr], w, h);            \
  }

#define norm_text(format)        \
  if (mod->tcw.NO_PAL.StrideSel) \
    w = 512;                     \
  Plannar<format>((u8 *)&params.vram[tex_addr], w, h);

  /*u32 sr;\
if (mod->tcw.NO_PAL.StrideSel)\
  {sr=(TEXT_CONTROL&31)*32;}\
        else\
  {sr=w;}\
  format((u8*)&params.vram[tex_addr],sr,h);*/

// Maps Dreamcast texture wrap/clamp modes to Wii GX constants.
int TexUV(u32 flip, u32 clamp)
{
  if (clamp)
    return GX_CLAMP;
  else if (flip)
    return GX_MIRROR;
  else
    return GX_REPEAT;
}

// ========================
// Processes the Dreamcast's TCW (Texture Control Word) to initialize Wii TexObjects.
// ========================

static void SetTextureParams(PolyParam *mod)
{

  GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

  u32 tex_addr = (mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
  u32 *ptex = (u32 *)&params.vram[tex_addr];

  // dst (= &pbuff[1]) MUST be 32-byte aligned: GX silently ignores misaligned
  // texture pointers and renders nothing -> black texture.
  // tex_addr*2 = TexAddr*16, which is only 16-byte aligned when TexAddr is odd,
  // so half of all textures were misaligned. The grey bar at the top was the
  // few cache lines that naturally evicted to RAM during conversion (aligned
  // by luck to a 32-byte boundary), making that slice visible while the rest
  // stayed in CPU cache as unreachable zeros for the GX hardware.
  // Rounding cache_offset up to 32 fixes alignment for every texture.
  // The minimum of 64 guarantees pbuff (sizeof=~56 bytes before dst) never
  // falls before the start of vram_buffer.
  u32 cache_offset = tex_addr * 2;
  cache_offset = (cache_offset + 31) & ~31u;  // round up -> 32-byte aligned dst
  if (cache_offset < 64) cache_offset = 64;   // room for TextureCacheDesc header
  TextureCacheDesc *pbuff = (TextureCacheDesc *)&vram_buffer[cache_offset] - 1;

  u32 FMT = GX_TF_RGB565; // Default format
  u32 texVQ = 0;
  u8 *vq_codebook;
  u32 w = 8 << mod->tsp.TexU;
  u32 h = 8 << mod->tsp.TexV;

  // ======================================
  // OLD CODE (Use later for FAST preset ?)
  // ======================================

  #if 0 // old code
		if (*ptex!=0xDEADBEEF || pbuff->addr!=tex_addr)
		{
			u32* dst=(u32*)&pbuff[1];
			u32 sz=(8<<mod->tsp.TexU) * (8<<mod->tsp.TexV)*2;

			if (mod->tcw.NO_PAL.ScanOrder)
				memcpy(dst,ptex,sz);
			else
				texture_TW((u8*)dst,(u8*)ptex,8<<mod->tsp.TexU,8<<mod->tsp.TexV);
        // texture_TW<1555>((u8*)dst, (u8*)ptex, 8<<mod->tsp.TexU, 8<<mod->tsp.TexV); // AI Claude suggestion

			//setup ..

			printf("Texture:%d %d %dx%d %08X --> %08X\n",mod->tcw.NO_PAL.PixelFmt,mod->tcw.NO_PAL.ScanOrder,8<<mod->tsp.TexU,8<<mod->tsp.TexV,tex_addr,dst);
			pbuff->addr=tex_addr;
			*ptex=0xDEADBEEF;
		}
  #endif


  // Only re-process texture if it has changed (marked by DEADBEEF sentinel).
  if (*ptex != 0xDEADBEEF || pbuff->addr != tex_addr || (mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder))
  {
    u32 *dst = (u32 *)&pbuff[1];
    VramWork = (u8 *)dst;
    pbuff->has_pal = false;
    pbuff->addr = tex_addr;

    switch (mod->tcw.NO_PAL.PixelFmt)
    {
    case 0:
    case 7:
      // 0	1555 value: 1 bit; RGB values: 5 bits each
      // 7	Reserved	Regarded as 1555
     if (mod->tcw.NO_PAL.ScanOrder)
      {
        // verify(tcw.NO_PAL.VQ_Comp==0);
        norm_text(1555);
      }
      else
      {
        // verify(tsp.TexU==tsp.TexV);
        twidle_tex(1555);
      }
      FMT = GX_TF_RGB5A3;
      break;

      // redo_argb:

    case 1:
      // 565 Format  R value: 5 bits; G value: 6 bits; B value: 5 bits
      if (mod->tcw.NO_PAL.ScanOrder)
      {
        // verify(tcw.NO_PAL.VQ_Comp==0);
        norm_text(565);
        //(&pbt,(u16*)&params.vram[sa],w,h);
      }
      else
      {
        // verify(tsp.TexU==tsp.TexV);
        twidle_tex(565);
      }
      FMT = GX_TF_RGB565;
      break;

      
    case 2:
      // 4444 Format: 4 bits; RGB values: 4 bits each
      if (mod->tcw.NO_PAL.ScanOrder)
      {
        // verify(tcw.NO_PAL.VQ_Comp==0);
        // argb4444to8888(&pbt,(u16*)&params.vram[sa],w,h);
        norm_text(4444);
      }
      else
      {
        twidle_tex(4444);
      }
      FMT = GX_TF_RGB5A3;
      break;
      
    case 3:
      // YUV422 Format 32 bits per 2 pixels; YUYV values: 8 bits each
      if (mod->tcw.NO_PAL.ScanOrder)
      {
        norm_text(422);
        // norm_text(ANYtoRAW);
      }
      else
      {
        // it cant be VQ , can it ?
        // docs say that yuv can't be VQ ...
        // HW seems to support it ;p
        twidle_tex(YUV422);
      }
      FMT = GX_TF_RGB565; // wha?
      break;
      // 4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
    case 5:
      // 5	4 BPP Palette	Palette texture with 4 bits/pixel
      verify(mod->tcw.PAL.VQ_Comp == 0);
      if (mod->tcw.NO_PAL.MipMapped)
        tex_addr += MipPoint[mod->tsp.TexU] << 1;

      SetupPaletteForTexture(mod->tcw.PAL.PalSelect << 4, 16);

      FMT = GX_TF_I4; // wha? the ?
      break;
    case 6:
      {
      // 6	8 BPP Palette	Palette texture with 8 bits/pixel
      verify(mod->tcw.PAL.VQ_Comp == 0);
      if (mod->tcw.NO_PAL.MipMapped)
        tex_addr += MipPoint[mod->tsp.TexU] << 2;

      SetupPaletteForTexture(mod->tcw.PAL.PalSelect << 4, 256);

      FMT = GX_TF_I8; // wha? the ? FUCK!
    }
      break;
    default:
      printf("Unhandled texture\n");
      // memset(temp_tex_buffer,0xFFEFCFAF,w*h*4);
    }

    if (texVQ)
    {
      // Flush VQ codebook from CPU cache so GX hardware can read it.
      DCFlushRange(vq_codebook, 2048); // 256 entries x 4 pixels x 2 bytes
      GX_InitTlutObj(&pbuff->pal, vq_codebook, FMT, 256);
      FMT = GX_TF_I8;
      w >>= 1;
      h >>= 1;
      pbuff->has_pal = true;
    }

    // Flush converted pixel data from CPU cache to physical RAM.
    // The Wii's GX hardware bypasses the L1/L2 cache and reads physical RAM
    // directly. Without this flush, GX sees stale zeros and renders black.
    // We flush ptex (params.vram), not dst (vram_buffer): params.vram is
    // Arena2 memory, valid on both real Wii and Dolphin. vram_buffer may be
    // a host-heap pointer that Dolphin's GX HLE rejects as invalid.
    {
      u32 flush_sz = texVQ ? (w * h)      // GX_TF_I8: 1 byte/texel (w,h already halved)
                           : (w * h * 2); // all 16-bit formats: 2 bytes/texel
      DCFlushRange(ptex, (flush_sz + 31) & ~31u);
    }

    // Init Texture Object — give GX ptex (params.vram) not dst (vram_buffer).
    bool use_mips = (mod->tcw.NO_PAL.MipMapped && get_graphism_preset() >= 2) ? GX_TRUE : GX_FALSE;
    GX_InitTexObj(&pbuff->tex, ptex, w, h, FMT, TexUV(mod->tsp.FlipU, mod->tsp.ClampU),
                  TexUV(mod->tsp.FlipV, mod->tsp.ClampV), use_mips);

    
    // Values from Apply Graphism Preset (LOW/NORMAL/HIGH/EXTRA)
    GX_InitTexObjLOD(&pbuff->tex, min_filt, mag_filt,
                  0.0f, 10.0f, lod_bias,
                  bias_clamp, edge_lod, aniso);
    
                  
    *ptex = 0xDEADBEEF;

    if(get_debug_loop() == 1){
      printf("Texture:%d %d %dx%d %08X --> %08X\n", mod->tcw.NO_PAL.PixelFmt, mod->tcw.NO_PAL.ScanOrder, 8 << mod->tsp.TexU, 8 << mod->tsp.TexV, tex_addr, (u32)dst);
    }
  }

  GX_LoadTexObj(&pbuff->tex, GX_TEXMAP0);

  if (pbuff->has_pal)
    GX_LoadTlut(&pbuff->pal, GX_TLUT0);
}

// ============================
// The main rendering loop. Executes GX commands to draw the stored vertex lists.
// ============================

void DoRender()
{
  // printf("MEM1 free: %.2f MB\n", ((unat)SYS_GetArena1Hi() - (unat)SYS_GetArena1Lo()) / 1024.f / 1024);
  // printf("MEM2 free: %.2f MB\n", ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / 1024.f / 1024.f);
  float dc_width = 640;
  float dc_height = 480;

  VIDEO_SetBlack(FALSE);
  // Set viewport to a centred 4:3 sub-region of the 16:9 framebuffer.
  // NDC [-1..+1] maps to this viewport, so all DC geometry (which is
  // already in 4:3 screen-space) displays with correct proportions.
  // In fullscreen mode use the whole width (stretched 16:9).
  if (FULLSCREEN())
  {
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
  }
  else
  {
    const float ratio  = (4.f / 3.f) / (16.f / 9.f); // 0.75
    const float vp_w   = rmode->fbWidth * ratio;
    const float vp_x   = (rmode->fbWidth - vp_w) * 0.5f;
    GX_SetViewport(vp_x, 0, vp_w, rmode->efbHeight, 0, 1);
  }
  GX_InvVtxCache();
  GX_InvalidateTexAll();

  // Single vertex format, always 24 bytes/vertex (POS+CLR0+TEX0).
  // VCD never changes mid-stream to avoid CP packet FIFO misalignment.
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,  GX_F32,   0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);

  GX_SetNumChans(1);
  GX_SetNumTexGens(1);

  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

  GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);

  // Background polygon handling
  u32 param_base = PARAM_BASE & 0xF00000;
  _ISP_BACKGND_D_type bg_d;

  bg_d.i = ISP_BACKGND_D & ~(0xF);
  (void)bg_d; // Currently unused but may be needed later

  bool PSVM = FPU_SHAD_SCALE & 0x100; // double parameters for volumes

  // ISP_BACKGND_T bitfield layout (from DC hardware spec, LSB first):
  //   bits [2:0]   tag_offset  (3 bits)
  //   bits [23:3]  tag_address (21 bits)
  //   bits [26:24] skip        (3 bits)
  //   bit  [27]    shadow      (1 bit)
  //   bit  [28]    cache_bypass(1 bit)
  // The Wii is big-endian so we must extract manually instead of
  // relying on the C bitfield struct (which packs in the wrong order).
  u32 raw_bgt        = ISP_BACKGND_T;
  u32 real_tag_offset  = (raw_bgt >> 0)  & 0x7;
  u32 real_tag_address = (raw_bgt >> 3)  & 0x1FFFFF;
  u32 real_skip        = (raw_bgt >> 24) & 0x7;
  u32 real_shadow      = (raw_bgt >> 27) & 0x1;

  // Get the strip base
  u32 strip_base = param_base + real_tag_address * 4;
  // Calculate the vertex size
  u32 strip_vs = (3 + real_skip) * 4;
  u32 strip_vert_num = real_tag_offset;

  if (PSVM && real_shadow)
  {
    strip_vs += real_skip * 4; // 2x the size needed for shadow volumes
  }
  // Get vertex ptr
  u32 vertex_ptr = strip_vert_num * strip_vs + strip_base + 3 * 4;
  // now , all the info is ready :p

  Vertex BGTest;

  decode_pvr_vertex(strip_base, vertex_ptr, &BGTest);

  // BGTest.col is in ARGB (Dreamcast PVR) format.
  // GXColor expects RGBA, so swap R and B channels before passing.
  u32 raw_col = BGTest.col;
  GXColor bgColor = {
    (u8)((raw_col >> 16) & 0xFF), // R (was at B position in ARGB)
    (u8)((raw_col >>  8) & 0xFF), // G
    (u8)((raw_col >>  0) & 0xFF), // B (was at R position in ARGB)
    (u8)((raw_col >> 24) & 0xFF)  // A
  };
  // Use the background vertex color as the EFB clear color.
  GX_SetCopyClear(bgColor, 0x00000000);

  GX_SetZMode(GX_TRUE, GX_GEQUAL, GX_TRUE);
  GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GX_SetAlphaUpdate(GX_TRUE);
  GX_SetColorUpdate(GX_TRUE);


  /*
  x'=x*xx + y*xy + z* xz + xw
  y'=x*yx + y*yy + z* yz + yw
  z'=x*zx + y*zy + z* zz + zw
  w'=x*wx + y*wy + z* wz + ww

  cx=x'/w'
  cy=y'/w'
  cz=z'/w'

  invW  = [+inf,0]
  w     = [0,+inf]

  post transform : -1 1
  w'=w
  z = w
  z' = A*w+B

  z'/w = A + B/w

  A + B*invW

  mapped to [0 1]
  min(invW)=1/max(W)
  max(invW)=1/min(W)

  A + B * max(invW) = vnear
  A + B * min(invW) = vfar

  A=-B*max(invW) + vnear

  -B*max(invW) + B*min(invW) = vfar
  B*(min(invW)-max(invW))=vfar
  B=vfar/(min(invW)-max(invW))

*/

  // sanitise values
  // Coordinate Projection Logic: Converts Dreamcast 1/W into Wii depth.

    if (vtx_min_Z <= 0.001)
    vtx_min_Z = 0.001;
  if (vtx_max_Z < 0 || vtx_max_Z > 128 * 1024)
    vtx_max_Z = 1;

  // Extra guard: if EFB garbage or NaN slipped through and corrupted the
  // Z range so that min >= max, the projection math below produces NaN/inf
  // which desyncs the GX FIFO ("GFX Fifo Opcode unknown 0x64").
  // Reset to a safe default range so the frame renders without a crash.
  // Can be removed
  if (vtx_min_Z >= vtx_max_Z || vtx_max_Z != vtx_max_Z || vtx_min_Z != vtx_min_Z)
  {
    vtx_min_Z = 0.001f;
    vtx_max_Z = 100000.0f;  // was 1.0f -- caused flash by collapsing all geometry to near-plane
  }

  // extend range
  vtx_max_Z *= 1.001; // to not clip vtx_max verts
  // vtx_min_Z*=0.999;

  // convert to [-1 .. 0]
  float p6 = -1 / (1 / vtx_max_Z - 1 / vtx_min_Z);
  float p5 = p6 / vtx_min_Z;

  // The projection matrix maps DC screen-space coords to GX clip space.
  // X aspect ratio is NOT corrected here — DC vertices are already in screen
  // space (x=[0..640]) and z is 1/W (depth), so a perspective matrix would
  // incorrectly couple x and z. X is remapped at vertex submission instead.
  Mtx44 mtx =
      {
          {(2.f / dc_width), 0, +(640.f / dc_width), 0},
          {0, -(2.f / dc_height), -(480.f / dc_height), 0},
          {0, 0, p5, p6},
          {0, 0, -1, 0}};

  // load the matrix to GX
  GX_LoadProjectionMtx(mtx, GX_PERSPECTIVE);

  // clear out other matrixes
  Mtx modelview;
  guMtxIdentity(modelview);
  GX_LoadPosMtxImm(modelview, GX_PNMTX0);

  Vertex *drawVTX = vertices;
  VertexList *drawLST = lists;
  PolyParam *drawMod = listModes;

  const VertexList *const crLST = curLST; // hint to the compiler that sceGUM cant edit this value !

  GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

  //		GX_SetAlphaCompare(GX_GREATER,0,GX_AOP,GX_ALWAYS,0);

  GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  int last_textured = -1;  // track texture state to skip redundant GX calls

  // Process opaque and then translucent lists.
  for (; drawLST != crLST; drawLST++)
  {
    if (drawLST == TransLST)
    {
      // enable blending & blending mode
      GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

      // setup alpha compare
    }

    s32 count = drawLST->count;
    if (count < 0)
    {
      int is_textured = drawMod->pcw.Texture ? 1 : 0;
      if (is_textured != last_textured)
      {
        if (is_textured)
        {
          GX_SetNumTexGens(1);
          GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
          GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
        }
        else
        {
          GX_SetNumTexGens(0);
          GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
          GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
        }
        last_textured = is_textured;
      }
      if (is_textured)
        SetTextureParams(drawMod);

      drawMod++;
      count &= 0x7FFF;
    }

    if (count)
    {
      GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, count);
      while (count--)
      {
        GX_Position3f32(drawVTX->x, drawVTX->y, -drawVTX->z);
        GX_Color1u32(HOST_TO_LE32(drawVTX->col));
        GX_TexCoord2f32(drawVTX->u, drawVTX->v);
        drawVTX++;
      }
      GX_End();
    }

    // sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,0,drawVTX);

    //			drawVTX+=count;
  }

  reset_vtx_state();

  GX_DrawDone();
  GX_CopyDisp(frameBuffer[fb], GX_TRUE);

  VIDEO_SetNextFramebuffer(frameBuffer[fb]);

  VIDEO_Flush();

  wii_audio_frame(); // Audio (Step AICA and push one frame of audio to ASND)
  VIDEO_WaitVSync();  // Needed for O3 mode (else Dreamcast logo take 5 seconds instead of 9)
}

// ============================
// START RENDERING
// ============================

void StartRender()
{
  u32 VtxCnt = curVTX - vertices;
  VertexCount += VtxCnt;

  render_end_pending_cycles = VtxCnt * 15;
  if (render_end_pending_cycles < 50000)
    render_end_pending_cycles = 50000;

  if (FB_W_SOF1 & 0x1000000)
  {
    // 2D direct framebuffer mode (logo screens).
    // Read from FB_R_SOF1 (what the DC video hardware displays),
    // NOT FB_W_SOF1 (write destination, may be the back buffer).
    static u16 fb2d_tex[640 * 480] ATTRIBUTE_ALIGN(32);

    u32 vram_addr = FB_R_SOF1 & 0x00FFFFFF;
    u16 *src = (u16 *)&params.vram[fast_ConvOffset32toOffset64(vram_addr)];

    // Tile into GX 4x4-block layout with LE->BE byte swap per pixel.
    for (int ty = 0; ty < 480; ty += 4)
      for (int tx = 0; tx < 640; tx += 4)
      {
        u16 *dst = &fb2d_tex[((ty / 4) * (640 / 4) + (tx / 4)) * 16];
        for (int row = 0; row < 4; row++)
        {
          u16 *srow = &src[(ty + row) * 640 + tx];
          for (int col = 0; col < 4; col++)
            *dst++ = (srow[col] >> 8) | (srow[col] << 8);
        }
      }
    DCFlushRange(fb2d_tex, sizeof(fb2d_tex));

    // FB_R_CTRL.fb_depth: 1 = RGB565, others = ARGB1555 (RGB5A3 on GX)
    GXTexObj texobj;
    if (FB_R_CTRL.fb_depth == 1)
      GX_InitTexObj(&texobj, fb2d_tex, 640, 480, GX_TF_RGB565,  GX_CLAMP, GX_CLAMP, GX_FALSE);
    else
      GX_InitTexObj(&texobj, fb2d_tex, 640, 480, GX_TF_RGB5A3,  GX_CLAMP, GX_CLAMP, GX_FALSE);
    GX_InitTexObjLOD(&texobj, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
    GX_LoadTexObj(&texobj, GX_TEXMAP0);

    // VTXFMT1: XY+UV only, leaves VTXFMT0 (3D path) undisturbed.
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS,  GX_POS_XY,  GX_F32, 0);
    GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST,  GX_F32, 0);
    GX_ClearVtxDesc();
    GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
    GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GX_SetNumChans(0);
    GX_SetNumTexGens(1);
    GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
    GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLORNULL);
    GX_SetTevOp(GX_TEVSTAGE0, GX_REPLACE);
    GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
    GX_SetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);

    Mtx44 ortho;
    guOrtho(ortho, 0, 480, 0, 640, 0, 1);
    GX_LoadProjectionMtx(ortho, GX_ORTHOGRAPHIC);
    Mtx mv;
    guMtxIdentity(mv);
    GX_LoadPosMtxImm(mv, GX_PNMTX0);

    // In 4:3 mode, draw the 2D framebuffer into a centred 4:3 sub-rectangle
    // so logo screens and 2D content have the same correct framing as 3D.
    float x0_2d, x1_2d;
    if (FULLSCREEN())
    {
      x0_2d = 0.f;
      x1_2d = 640.f;
    }
    else
    {
      // 4:3 inside 16:9: side bars are (1 - 3/4) / 2 * 640 = 80 px each side
      const float ratio = (4.f / 3.f) / (16.f / 9.f); // 0.75
      float margin = (640.f * (1.f - ratio)) * 0.5f;   // 80 px
      x0_2d = margin;
      x1_2d = 640.f - margin;
    }

    GX_Begin(GX_QUADS, GX_VTXFMT1, 4);
      GX_Position2f32(x0_2d,   0); GX_TexCoord2f32(0, 0);
      GX_Position2f32(x1_2d,   0); GX_TexCoord2f32(1, 0);
      GX_Position2f32(x1_2d, 480); GX_TexCoord2f32(1, 1);
      GX_Position2f32(x0_2d, 480); GX_TexCoord2f32(0, 1);
    GX_End();

    GX_DrawDone();
    GX_CopyDisp(frameBuffer[fb], GX_TRUE);
    VIDEO_SetNextFramebuffer(frameBuffer[fb]);
    VIDEO_Flush();
    VIDEO_WaitVSync();
    FrameCount++;
    return;
  }

  DoRender();

  FrameCount++;
}

void EndRender() {}



// ============================
// VertexDecoder struct: Handles the accumulation of vertex data into strips.
// ============================

struct VertexDecoder
{
  // list handling
  __forceinline static void StartList(u32 ListType)
  {
    if (ListType == ListType_Translucent)
      TransLST = curLST;
  }
  __forceinline static void EndList(u32 ListType) {}

  static u32 FLCOL(float *col)
  {
    u32 A = col[0] * 255;
    u32 R = col[1] * 255;
    u32 G = col[2] * 255;
    u32 B = col[3] * 255;
    if (A > 255)
      A = 255;
    if (R > 255)
      R = 255;
    if (G > 255)
      G = 255;
    if (B > 255)
      B = 255;

    return (A << 24) | (B << 16) | (G << 8) | R;
  }
  static u32 INTESITY(float inte)
  {
    u32 C = inte * 255;
    if (C > 255)
      C = 255;
    return (0xFF << 24) | (C << 16) | (C << 8) | (C);
  }

  // Polys
#define glob_param_bdc                 \
  if ((curVTX - vertices) > 38 * 1024) \
    reset_vtx_state();                 \
  global_regd = true;                  \
  curMod->pcw = pp->pcw;               \
  curMod->isp = pp->isp;               \
  curMod->tsp = pp->tsp;               \
  curMod->tcw = pp->tcw;

  __forceinline static void fastcall AppendPolyParam0(TA_PolyParam0 *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam1(TA_PolyParam1 *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam2A(TA_PolyParam2A *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam2B(TA_PolyParam2B *pp)
  {
  }
  __forceinline static void fastcall AppendPolyParam3(TA_PolyParam3 *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam4A(TA_PolyParam4A *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam4B(TA_PolyParam4B *pp)
  {
  }

  // Poly Strip handling
  // UPDATE SPRITES ON EDIT !
  __forceinline static void StartPolyStrip()
  {
    curLST->ptr = curVTX;
  }

  __forceinline static void EndPolyStrip()
  {
    curLST->count = (curVTX - curLST->ptr);
    if (global_regd)
    {
      curLST->count |= 0x80000000;
      global_regd = false;
      curMod++;
    }
    curLST++;
  }

// Standard vertex projection macro. PVR 'Z' is actually 1/W.
//
// Guard against zero/negative/NaN Z values that can arrive when the game reads
// back EFB (framebuffer) data that the emulator hasn't rendered yet (e.g.
// Castlevania: Resurrection shadow/reflection pass).  A bad Z causes W=1/Z to
// become infinity or NaN, which then corrupts vtx_min_Z / vtx_max_Z, blows up
// the projection matrix in DoRender(), and ultimately desyncs the GX FIFO
// producing "GFX Fifo Opcode unknown (0x64)".
//
// Clamping to 0.0001f keeps W finite and pushes the vertex safely to the far
// plane where it is invisible but harmless.
#define vert_base(dst, _x, _y, _z) /*VertexCount++;*/         \
  float _safe_z = (_z < 0.0001f) ? 0.0001f : _z;             \
  float W = 1.0f / _safe_z;                                   \
  curVTX[dst].x = VTX_TFX(_x) * W;                           \
  curVTX[dst].y = VTX_TFY(_y) * W;                           \
  if (W > 0.0f && W < vtx_min_Z)                             \
    vtx_min_Z = W;                                            \
  if (W > 0.0f && W > vtx_max_Z)                             \
    vtx_max_Z = W;                                            \
  curVTX[dst].z = W; /*Linearly scaled later*/

  // Poly Vertex handlers
#define vert_cvt_base vert_base(0, vtx->xyz[0], vtx->xyz[1], vtx->xyz[2])

  // Handlers for various PVR vertex types (Packed color, Float color, Intensity, etc.)
  //(Non-Textured, Packed Color)
  __forceinline static void AppendPolyVertex0(TA_Vertex0 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);

    curVTX++;
  }

  //(Non-Textured, Floating Color)
  __forceinline static void AppendPolyVertex1(TA_Vertex1 *vtx)
  {
    vert_cvt_base;
    curVTX->col = FLCOL(&vtx->BaseA);

    curVTX++;
  }

  //(Non-Textured, Intensity)
  __forceinline static void AppendPolyVertex2(TA_Vertex2 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt);

    curVTX++;
  }

  //(Textured, Packed Color)
  __forceinline static void AppendPolyVertex3(TA_Vertex3 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);

    curVTX->u = vtx->u;
    curVTX->v = vtx->v;

    curVTX++;
  }

  //(Textured, Packed Color, 16bit UV)
  __forceinline static void AppendPolyVertex4(TA_Vertex4 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);

    curVTX++;
  }

  //(Textured, Floating Color)
  __forceinline static void AppendPolyVertex5A(TA_Vertex5A *vtx)
  {
    vert_cvt_base;

    curVTX->u = vtx->u;
    curVTX->v = vtx->v;
  }
  __forceinline static void AppendPolyVertex5B(TA_Vertex5B *vtx)
  {
    curVTX->col = FLCOL(&vtx->BaseA);
    curVTX++;
  }

  //(Textured, Floating Color, 16bit UV)
  __forceinline static void AppendPolyVertex6A(TA_Vertex6A *vtx)
  {
    vert_cvt_base;

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);
  }
  __forceinline static void AppendPolyVertex6B(TA_Vertex6B *vtx)
  {
    curVTX->col = FLCOL(&vtx->BaseA);
    curVTX++;
  }

  //(Textured, Intensity)
  __forceinline static void AppendPolyVertex7(TA_Vertex7 *vtx)
  {
    vert_cvt_base;
    curVTX->u = vtx->u;
    curVTX->v = vtx->v;

    curVTX->col = INTESITY(vtx->BaseInt);

    curVTX++;
  }

  //(Textured, Intensity, 16bit UV)
  __forceinline static void AppendPolyVertex8(TA_Vertex8 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt);

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);

    curVTX++;
  }

  //(Non-Textured, Packed Color, with Two Volumes)
  __forceinline static void AppendPolyVertex9(TA_Vertex9 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol0);

    curVTX++;
  }

  //(Non-Textured, Intensity,	with Two Volumes)
  __forceinline static void AppendPolyVertex10(TA_Vertex10 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt0);

    curVTX++;
  }

  //(Textured, Packed Color,	with Two Volumes)
  __forceinline static void AppendPolyVertex11A(TA_Vertex11A *vtx)
  {
    vert_cvt_base;

    curVTX->u = vtx->u0;
    curVTX->v = vtx->v0;

    curVTX->col = ABGR8888(vtx->BaseCol0);
  }
  __forceinline static void AppendPolyVertex11B(TA_Vertex11B *vtx)
  {
    curVTX++;
  }

  //(Textured, Packed Color, 16bit UV, with Two Volumes)
  __forceinline static void AppendPolyVertex12A(TA_Vertex12A *vtx)
  {
    vert_cvt_base;

    curVTX->u = CVT16UV(vtx->u0);
    curVTX->v = CVT16UV(vtx->v0);

    curVTX->col = ABGR8888(vtx->BaseCol0);
  }
  __forceinline static void AppendPolyVertex12B(TA_Vertex12B *vtx)
  {
    curVTX++;
  }

  //(Textured, Intensity,	with Two Volumes)
  __forceinline static void AppendPolyVertex13A(TA_Vertex13A *vtx)
  {
    vert_cvt_base;
    curVTX->u = vtx->u0;
    curVTX->v = vtx->v0;
    curVTX->col = INTESITY(vtx->BaseInt0);
  }
  __forceinline static void AppendPolyVertex13B(TA_Vertex13B *vtx)
  {
    curVTX++;
  }

  //(Textured, Intensity, 16bit UV, with Two Volumes)
  __forceinline static void AppendPolyVertex14A(TA_Vertex14A *vtx)
  {
    vert_cvt_base;
    curVTX->u = CVT16UV(vtx->u0);
    curVTX->v = CVT16UV(vtx->v0);
    curVTX->col = INTESITY(vtx->BaseInt0);
  }
  __forceinline static void AppendPolyVertex14B(TA_Vertex14B *vtx)
  {
    curVTX++;
  }

  // Sprites
  __forceinline static void AppendSpriteParam(TA_SpriteParam *spr)
  {
    TA_SpriteParam *pp = spr;
    glob_param_bdc;
  }

// Sprite Vertex Handlers
/*
__forceinline
static void AppendSpriteVertex0A(TA_Sprite0A* sv)
{

}
__forceinline
static void AppendSpriteVertex0B(TA_Sprite0B* sv)
{

}
*/
#define sprite_uv(indx, u_name, v_name) \
  curVTX[indx].u = CVT16UV(sv->u_name); \
  curVTX[indx].v = CVT16UV(sv->v_name);

  // Sprites are converted to 4-vertex triangle strips.
  __forceinline static void AppendSpriteVertexA(TA_Sprite1A *sv)
  {
    
    StartPolyStrip();
    curVTX[0].col = 0xFFFFFFFF;
    curVTX[1].col = 0xFFFFFFFF;
    curVTX[2].col = 0xFFFFFFFF;
    curVTX[3].col = 0xFFFFFFFF;

    {
      vert_base(2, sv->x0, sv->y0, sv->z0);
    }
    {
      vert_base(3, sv->x1, sv->y1, sv->z1);
    }

    curVTX[1].x = sv->x2;
  }
  __forceinline static void AppendSpriteVertexB(TA_Sprite1B *sv)
  {

    {
      vert_base(1, curVTX[1].x, sv->y2, sv->z2);
    }
    {
      vert_base(0, sv->x3, sv->y3, sv->z2);
    }

    sprite_uv(2, u0, v0);
    sprite_uv(3, u1, v1);
    sprite_uv(1, u2, v2);
    sprite_uv(0, u0, v2); // or sprite_uv(u2,v0); ?

    curVTX += 4;
    //			VertexCount+=4;

    // EndPolyStrip();
    curLST->count = 4;
    if (global_regd)
    {
      curLST->count |= 0x80000000;
      global_regd = false;
      curMod++;
    }
    curLST++;
  }

  // ModVolumes
  __forceinline static void AppendModVolParam(TA_ModVolParam *modv)
  {
  }

  // ModVol Strip handling
  __forceinline static void StartModVol(TA_ModVolParam *param)
  {
  }
  __forceinline static void ModVolStripEnd()
  {
  }

  // Mod Volume Vertex handlers
  __forceinline static void AppendModVolVertexA(TA_ModVolA *mvv)
  {
  }
  __forceinline static void AppendModVolVertexB(TA_ModVolB *mvv)
  {
  }
  __forceinline static void SetTileClip(u32 xmin, u32 ymin, u32 xmax, u32 ymax)
  {
  }
  __forceinline static void TileClipMode(u32 mode)
  {
  }
  // Misc
  __forceinline static void ListCont()
  {
  }
  __forceinline static void ListInit()
  {
    // reset_vtx_state();
  }
  __forceinline static void SoftReset()
  {
    // reset_vtx_state();
  }
};
// Setup related

// ============================
// Vertex Decoding-Converting
// ============================

void SetFpsText(char *text)
{
  strcpy(fps_text, text);
  printf(text);
  // if (!IsFullscreen)
  {
    // SetWindowText((HWND)emu.GetRenderTarget(), fps_text);
  }
}

// ============================
// Initialize the Wii Video and GX subsystem.
// ============================

bool InitRenderer()
{
  // Obtain the preferred video mode from the system
  // This will correspond to the settings in the Wii menu
  rmode = VIDEO_GetPreferredMode(NULL);

  // Adjust video mode based on region.
  switch (rmode->viTVMode >> 2)
  {
  case VI_NTSC: // 480 lines (NTSC 60hz)
    break;
  case VI_PAL: // 576 lines (PAL 50hz)
    rmode = &TVPal576IntDfScale;
    rmode->xfbHeight = 480;
    rmode->viYOrigin = (VI_MAX_HEIGHT_PAL - 480) / 2;
    rmode->viHeight = 480;
    break;
  default: // 480 lines (PAL 60Hz)
    break;
  }

  // Set up the video registers with the chosen mode (devkit wii example)
  VIDEO_Configure(rmode);

  // Allocate memory for the display in the uncached region (devkit wii example)
  // allocate 2 framebuffers for double buffering
  frameBuffer[0] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  frameBuffer[1] = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
  
  // Set up the video registers with the chosen mode (devkit wii example)
  VIDEO_Configure(rmode);
  // Tell the video hardware where our display memory is (devkit wii example)
  VIDEO_SetNextFramebuffer(frameBuffer[fb]);
  // Make the display visible or invisible (devkit wii example)
  VIDEO_SetBlack(TRUE); // FALSE doesn't seems to change anything here
  // Flush the video register changes to the hardware (devkit wii example)
  VIDEO_Flush();
  // Wait for Video setup to complete
  VIDEO_WaitVSync();
  if (rmode->viTVMode & VI_NON_INTERLACE)
    VIDEO_WaitVSync();

  // fb = fb ^ 1; (invert value)
  fb ^= 1;

  // setup the fifo and then init the flipper
  memset(gp_fifo, 0, DEFAULT_FIFO_SIZE);

  GX_Init(gp_fifo, DEFAULT_FIFO_SIZE);
  ApplyGraphismPreset();  // LOW/NORMAL/HIGH/EXTRA

  printf("vram_buffer: %08X\n", (u32)vram_buffer);

  // clears the bg to color and clears the z buffer
  // GX_SetCopyClear(background, 0x00ffffff);

  // other gx setup
  // Standard GX display/copy settings.
  GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
  f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
  u32 xfbHeight = GX_SetDispCopyYScale(yscale);
  GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
  GX_SetDispCopyDst(rmode->fbWidth, xfbHeight);
  GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
  GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

  if (rmode->aa)
    GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
  else
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  GX_SetCullMode(GX_CULL_NONE);
  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  GX_SetDispCopyGamma(GX_GM_1_0);

  // setup the vertex descriptor
  // tells the flipper to expect direct data

  printf("MEM1 free: %.2f MB\n", ((unat)SYS_GetArena1Hi() - (unat)SYS_GetArena1Lo()) / 1024.f / 1024);
  printf("MEM2 free: %.2f MB\n", ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / 1024.f / 1024.f);

  printf("sizeof TextureCacheDesc: %d\n", sizeof(TextureCacheDesc));
  printf("sizeof GXTexObj: %d\n", sizeof(GXTexObj));
  printf("sizeof GXTlutObj: %d\n", sizeof(GXTlutObj));

  return TileAccel.Init();
}

// ============================
// TERM RENDERER
// ============================

void TermRenderer()
{
  TileAccel.Term();
}

// ============================
// RESET RENDERER
// ============================

void ResetRenderer(bool Manual)
{
  TileAccel.Reset(Manual);
  VertexCount = 0;
  FrameCount = 0;
}

// Thread Start
bool ThreadStart()
{
  return true;
}

// Thread End
void ThreadEnd()
{
}

// List Cont
void ListCont()
{
  TileAccel.ListCont();
}

// List Init
void ListInit()
{
  TileAccel.ListInit();
}

// Soft Reset
void SoftReset()
{
  TileAccel.SoftReset();
}

// VRAM locked Write
void VramLockedWrite(vram_block *bl)
{
}

#include <vector>
#include <string>
using namespace std;

// ==============
// Attempts to find and boot a 'boot.cdi' image from the SD/USB root.
// ==============

int GetFile(char *szFileName, char *szParse = 0, u32 flags = 0)
{
  if (FILE *f = fopen("/boot.cdi", "rb"))
  {
    fclose(f);
    strcpy(szFileName, "/boot.cdi");
    return 1;
  }
  else
    return 0;
}