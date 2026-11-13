// plugs/drkPvr/gx2Rend.cpp
//
// GX2 renderer for the native Wii U build (REND_WIIU). Same role as gxRend.cpp
// on the Wii: it implements the rend_* plugin API (gxRend.h, which despite its
// name is the generic renderer interface) on top of the Wii U's GX2.
//
// This is the BASIC renderer of the first Wii U port. It is deliberately small
// and self-contained instead of a translation of gxRend.cpp's ~16K lines: none
// of gxRend's per-game presets/hacks apply here yet, and none of its
// GX-specific machinery (TEV, EFB copies, TLUTs, tiled 4x4 textures) has a GX2
// equivalent anyway. What it does:
//
//   TA decode   FifoSplitter<GX2Decoder>: vertices go straight into a
//               GPU-visible vertex buffer, in the raw TA form (screen x/y in
//               canvas pixels, z = 1/W, uv, packed colours). No CPU projection.
//   Projection  in the vertex shader (wiiu/shaders/pvr.vert): position
//               (ndc.xy * w, f(1/W) * w, w) with w = W, so texturing and
//               colours are perspective-correct, and f(x) = x / (1 + x) keeps
//               depth monotonic in 1/W - the PVR's own depth - so the ISP
//               DepthMode (NEVER..ALWAYS, compared on 1/W) maps 1:1 onto
//               GX2CompareFunction, which has the same order.
//   Passes      opaque -> punch-through (alpha test vs PT_ALPHA_REF) ->
//               translucent (TSP SrcInstr/DstInstr blending, submission order,
//               no Z write), per polygon header state.
//   Textures    all PVR formats except bump maps / paletted VQ, decoded to
//               RGBA8 linear GX2 textures, cached by TCW+size, re-decoded when a
//               cheap hash of the VRAM source (and palette) changes.
//   Target      a 640x480 offscreen colour + depth target, then scaled 4:3 onto
//               the TV and the GamePad (wiiu/shaders/blit.*) and swapped.
//
// Not done yet (all have a gxRend.cpp equivalent to port from): modifier
// volumes, fog, the ISP background polygon (the frame clears to black, depth
// clears to ISP_BACKGND_D), tile clipping, render-to-texture (bit-24 passes
// are skipped), the VRAM framebuffer present for games that draw 2D straight
// into VRAM, autosort, mipmaps, the X/Y scaler presets.
//
// Shaders: wiiu/shaders/pvr.{vert,frag} and blit.{vert,frag}, compiled to .gsh
// by CafeGLSL at build time (wiiu/Makefile) and linked in with bin2o.

#include "config.h"

#if HOST_OS == OS_WIIU   // Wii U only; wii/Makefile also filters this file out.

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <gx2/clear.h>
#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <coreinit/memdefaultheap.h>
#include <whb/gfx.h>

#include "gxRend.h"
#include "regs.h"
#include "wiiu/wiiu_config.h"
#include "wiiu/wiiu_exit.h"

// Embedded shader binaries (wiiu/Makefile: bin2o on the CafeGLSL output).
#include "pvr_gsh.h"
#include "blit_gsh.h"

using namespace TASplitter;   // FifoSplitter, TA_* structs, ListType_* (as gxRend.cpp)

// ============================================================================
// Globals other core files expect from the renderer (see gxRend.cpp)
// ============================================================================
// dc/mem/_vmem.cpp carves these out of the arena. gx2Rend reads textures
// straight from params.vram, so vram_buffer and the PLUS arena go unused.
u8  *vram_buffer         = 0;
u8  *plus_tex_arena      = 0;
u32  plus_tex_arena_size = 0;

extern "C" int get_render_delay_preset();

// ============================================================================
// TA output: vertices / strips / polygon headers for the current frame
// ============================================================================

// One vertex as the GPU fetches it (wiiu/shaders/pvr.vert attributes).
// col/spc are RGBA8 in memory order (R first), i.e. the u32 0xRRGGBBAA.
struct GVtx
{
  float x, y, z;   // canvas pixels, z = 1/W as the TA gives it
  float u, v;
  u32   col;       // base colour
  u32   spc;       // offset (specular) colour, alpha unused
  u32   pad;       // 32-byte stride
};

struct GParam
{
  u32 pcw, isp, tsp, tcw;
  u32 list;        // ListType_* at the time the header arrived
};

struct GStrip
{
  u32 first;       // index into s_vtx
  u32 count;
  u32 param;       // index into s_params
};

static GVtx   *s_vtx      = 0;   // GPU-visible, WIIU_TA_VTX_MAX entries
static GStrip  s_strips[WIIU_TA_STRIP_MAX];
static GParam  s_params[WIIU_TA_PARAM_MAX];
static u32     s_nvtx     = 0;
static u32     s_nstrips  = 0;
static u32     s_nparams  = 0;
static u32     s_cur_list = 0;
static u32     s_strip_start = 0;
static bool    s_overflow = false;

// Decoder scratch (same roles as gxRend.cpp's curFaceColor*/curSpriteCol...).
static float s_face_r = 1, s_face_g = 1, s_face_b = 1, s_face_a = 1;
static float s_offs_r = 0, s_offs_g = 0, s_offs_b = 0;
static u32   s_sprite_col = 0xFFFFFFFFu;
static u32   s_sprite_spc = 0;
static bool  s_param_has_offs = false;

static bool s_rd_list_init = false;   // a TA_LIST_INIT came since the last render

static void ta_reset()
{
  s_nvtx = s_nstrips = s_nparams = 0;
  s_strip_start = 0;
  s_overflow = false;
}

static inline u32 argb_to_rgba(u32 c) { return (c << 8) | (c >> 24); }

static inline u32 clamp255(float f)
{
  int i = (int)(f * 255.0f);
  return i < 0 ? 0u : (i > 255 ? 255u : (u32)i);
}

static inline u32 pack_rgba(float a, float r, float g, float b)
{
  return (clamp255(r) << 24) | (clamp255(g) << 16) | (clamp255(b) << 8) | clamp255(a);
}

static inline GVtx *vtx_next()
{
  if (s_nvtx >= WIIU_TA_VTX_MAX)
  {
    s_overflow = true;
    static GVtx s_sink;   // keep writing somewhere harmless
    return &s_sink;
  }
  return &s_vtx[s_nvtx];
}

// 16-bit UV (high half of a float)
static inline float cvt16uv(u32 uv)
{
  union { u32 i; float f; } c;
  c.i = uv << 16;
  return c.f;
}

struct GX2Decoder
{
  static void StartList(u32 ListType) { s_cur_list = ListType; }
  static void EndList(u32 ListType)   {}

  static void param(const PCW &pcw, const ISP_TSP &isp, const TSP &tsp, const TCW &tcw)
  {
    if (s_nparams >= WIIU_TA_PARAM_MAX)
    {
      s_overflow = true;
      return;
    }
    GParam &p = s_params[s_nparams++];
    p.pcw  = pcw.full;
    p.isp  = isp.full;
    p.tsp  = tsp.full;
    p.tcw  = tcw.full;
    p.list = s_cur_list;
    s_param_has_offs = (pcw.full >> 2) & 1;   // PCW.Offset
  }

  static void AppendPolyParam0(TA_PolyParam0 *pp)  { param(pp->pcw, pp->isp, pp->tsp, pp->tcw); }
  static void AppendPolyParam1(TA_PolyParam1 *pp)
  {
    param(pp->pcw, pp->isp, pp->tsp, pp->tcw);
    s_face_r = pp->FaceColorR; s_face_g = pp->FaceColorG;
    s_face_b = pp->FaceColorB; s_face_a = pp->FaceColorA;
    s_offs_r = s_offs_g = s_offs_b = 0.0f;
  }
  static void AppendPolyParam2A(TA_PolyParam2A *pp) { param(pp->pcw, pp->isp, pp->tsp, pp->tcw); }
  static void AppendPolyParam2B(TA_PolyParam2B *pp)
  {
    s_face_r = pp->FaceColorR; s_face_g = pp->FaceColorG;
    s_face_b = pp->FaceColorB; s_face_a = pp->FaceColorA;
    s_offs_r = pp->FaceOffsetR; s_offs_g = pp->FaceOffsetG; s_offs_b = pp->FaceOffsetB;
  }
  static void AppendPolyParam3(TA_PolyParam3 *pp)   { param(pp->pcw, pp->isp, pp->tsp, pp->tcw); }
  static void AppendPolyParam4A(TA_PolyParam4A *pp) { param(pp->pcw, pp->isp, pp->tsp, pp->tcw); }
  static void AppendPolyParam4B(TA_PolyParam4B *pp)
  {
    s_face_r = pp->FaceColor0R; s_face_g = pp->FaceColor0G;
    s_face_b = pp->FaceColor0B; s_face_a = pp->FaceColor0A;
    s_offs_r = s_offs_g = s_offs_b = 0.0f;
  }

  static void StartPolyStrip() { s_strip_start = s_nvtx; }

  static void EndPolyStrip()
  {
    const u32 n = s_nvtx - s_strip_start;
    if (n >= 3 && s_nparams > 0 && s_nstrips < WIIU_TA_STRIP_MAX && !s_overflow)
    {
      GStrip &s = s_strips[s_nstrips++];
      s.first = s_strip_start;
      s.count = n;
      s.param = s_nparams - 1;
    }
    s_strip_start = s_nvtx;
  }

  // --- vertex colour helpers ------------------------------------------------
  static u32 flcol(const float *argb)
  {
    return pack_rgba(argb[0], argb[1], argb[2], argb[3]);
  }
  static u32 intensity(float i)
  {
    return pack_rgba(s_face_a, s_face_r * i, s_face_g * i, s_face_b * i);
  }
  static u32 offs_intensity(float i)
  {
    return s_param_has_offs ? pack_rgba(0.0f, s_offs_r * i, s_offs_g * i, s_offs_b * i) : 0u;
  }
  static u32 packed(u32 argb)      { return argb_to_rgba(argb); }
  static u32 packed_offs(u32 argb) { return s_param_has_offs ? argb_to_rgba(argb) : 0u; }

#define VTX_BEGIN(_x, _y, _z)        \
  GVtx *cv = vtx_next();             \
  cv->x = (_x); cv->y = (_y); cv->z = (_z); \
  cv->u = cv->v = 0.0f;              \
  cv->spc = 0;

  // Non-textured, packed colour
  static void AppendPolyVertex0(TA_Vertex0 *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->col = packed(vtx->BaseCol); s_nvtx += !s_overflow; }
  // Non-textured, float colour
  static void AppendPolyVertex1(TA_Vertex1 *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->col = flcol(&vtx->BaseA); s_nvtx += !s_overflow; }
  // Non-textured, intensity
  static void AppendPolyVertex2(TA_Vertex2 *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->col = intensity(vtx->BaseInt); s_nvtx += !s_overflow; }
  // Textured, packed colour
  static void AppendPolyVertex3(TA_Vertex3 *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->col = packed(vtx->BaseCol); cv->spc = packed_offs(vtx->OffsCol);
    cv->u = vtx->u; cv->v = vtx->v;
    s_nvtx += !s_overflow;
  }
  // Textured, packed colour, 16-bit UV
  static void AppendPolyVertex4(TA_Vertex4 *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->col = packed(vtx->BaseCol); cv->spc = packed_offs(vtx->OffsCol);
    cv->u = cvt16uv(vtx->u); cv->v = cvt16uv(vtx->v);
    s_nvtx += !s_overflow;
  }
  // Textured, float colour (64-byte vertex: A half then B half)
  static void AppendPolyVertex5A(TA_Vertex5A *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->u = vtx->u; cv->v = vtx->v; }
  static void AppendPolyVertex5B(TA_Vertex5B *vtx)
  {
    GVtx *cv = vtx_next();
    cv->col = flcol(&vtx->BaseA);
    cv->spc = s_param_has_offs ? flcol(&vtx->OffsA) : 0u;
    s_nvtx += !s_overflow;
  }
  // Textured, float colour, 16-bit UV
  static void AppendPolyVertex6A(TA_Vertex6A *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->u = cvt16uv(vtx->u); cv->v = cvt16uv(vtx->v); }
  static void AppendPolyVertex6B(TA_Vertex6B *vtx)
  {
    GVtx *cv = vtx_next();
    cv->col = flcol(&vtx->BaseA);
    cv->spc = s_param_has_offs ? flcol(&vtx->OffsA) : 0u;
    s_nvtx += !s_overflow;
  }
  // Textured, intensity
  static void AppendPolyVertex7(TA_Vertex7 *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->u = vtx->u; cv->v = vtx->v;
    cv->col = intensity(vtx->BaseInt); cv->spc = offs_intensity(vtx->OffsInt);
    s_nvtx += !s_overflow;
  }
  // Textured, intensity, 16-bit UV
  static void AppendPolyVertex8(TA_Vertex8 *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->u = cvt16uv(vtx->u); cv->v = cvt16uv(vtx->v);
    cv->col = intensity(vtx->BaseInt); cv->spc = offs_intensity(vtx->OffsInt);
    s_nvtx += !s_overflow;
  }
  // Two-volume variants: volume 0 only (as gxRend.cpp)
  static void AppendPolyVertex9(TA_Vertex9 *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->col = packed(vtx->BaseCol0); s_nvtx += !s_overflow; }
  static void AppendPolyVertex10(TA_Vertex10 *vtx)
  { VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]); cv->col = intensity(vtx->BaseInt0); s_nvtx += !s_overflow; }
  static void AppendPolyVertex11A(TA_Vertex11A *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->u = vtx->u0; cv->v = vtx->v0;
    cv->col = packed(vtx->BaseCol0); cv->spc = packed_offs(vtx->OffsCol0);
  }
  static void AppendPolyVertex11B(TA_Vertex11B *vtx) { s_nvtx += !s_overflow; }
  static void AppendPolyVertex12A(TA_Vertex12A *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->u = cvt16uv(vtx->u0); cv->v = cvt16uv(vtx->v0);
    cv->col = packed(vtx->BaseCol0); cv->spc = packed_offs(vtx->OffsCol0);
  }
  static void AppendPolyVertex12B(TA_Vertex12B *vtx) { s_nvtx += !s_overflow; }
  static void AppendPolyVertex13A(TA_Vertex13A *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->u = vtx->u0; cv->v = vtx->v0;
    cv->col = intensity(vtx->BaseInt0); cv->spc = offs_intensity(vtx->OffsInt0);
  }
  static void AppendPolyVertex13B(TA_Vertex13B *vtx) { s_nvtx += !s_overflow; }
  static void AppendPolyVertex14A(TA_Vertex14A *vtx)
  {
    VTX_BEGIN(vtx->xyz[0], vtx->xyz[1], vtx->xyz[2]);
    cv->u = cvt16uv(vtx->u0); cv->v = cvt16uv(vtx->v0);
    cv->col = intensity(vtx->BaseInt0); cv->spc = offs_intensity(vtx->OffsInt0);
  }
  static void AppendPolyVertex14B(TA_Vertex14B *vtx) { s_nvtx += !s_overflow; }
#undef VTX_BEGIN

  // --- Sprites: one 4-vertex strip, same corner order as gxRend.cpp ----------
  static void AppendSpriteParam(TA_SpriteParam *spr)
  {
    param(spr->pcw, spr->isp, spr->tsp, spr->tcw);
    s_sprite_col = argb_to_rgba(spr->BaseCol);
    s_sprite_spc = s_param_has_offs ? argb_to_rgba(spr->OffsCol) : 0u;
  }

  static void AppendSpriteVertexA(TA_Sprite1A *sv)
  {
    StartPolyStrip();
    if (s_nvtx + 4 > WIIU_TA_VTX_MAX)
    {
      s_overflow = true;
      return;
    }
    GVtx *v = &s_vtx[s_nvtx];
    for (int i = 0; i < 4; i++)
    {
      v[i].col = s_sprite_col;
      v[i].spc = s_sprite_spc;
    }
    v[2].x = sv->x0; v[2].y = sv->y0; v[2].z = sv->z0;
    v[3].x = sv->x1; v[3].y = sv->y1; v[3].z = sv->z1;
    v[1].x = sv->x2;
  }

  static void AppendSpriteVertexB(TA_Sprite1B *sv)
  {
    if (s_overflow)
      return;
    GVtx *v = &s_vtx[s_nvtx];
    v[1].y = sv->y2; v[1].z = sv->z2;
    v[0].x = sv->x3; v[0].y = sv->y3; v[0].z = sv->z2;   // no z3 in the format
    v[2].u = cvt16uv(sv->u0); v[2].v = cvt16uv(sv->v0);
    v[3].u = cvt16uv(sv->u1); v[3].v = cvt16uv(sv->v1);
    v[1].u = cvt16uv(sv->u2); v[1].v = cvt16uv(sv->v2);
    v[0].u = cvt16uv(sv->u0); v[0].v = cvt16uv(sv->v2);
    s_nvtx += 4;
    EndPolyStrip();
  }

  // --- Modifier volumes: not rendered yet ------------------------------------
  static void AppendModVolParam(TA_ModVolParam *modv) {}
  static void StartModVol(TA_ModVolParam *param) {}
  static void ModVolStripEnd() {}
  static void AppendModVolVertexA(TA_ModVolA *mvv) {}
  static void AppendModVolVertexB(TA_ModVolB *mvv) {}

  // --- Tile clip: not applied yet --------------------------------------------
  static void SetTileClip(u32 xmin, u32 ymin, u32 xmax, u32 ymax) {}
  static void TileClipMode(u32 mode) {}

  static void ListCont() {}
  static void ListInit() {}
  static void SoftReset() {}
};

FifoSplitter<GX2Decoder> TileAccel;

// ============================================================================
// GPU objects
// ============================================================================

static bool              s_gfx_up = false;
static WHBGfxShaderGroup s_pvr_shader;
static WHBGfxShaderGroup s_blit_shader;
static u32               s_u_xform  = 0;   // vertex uniform register offsets
static u32               s_u_mode   = 0;   // pixel uniform register offset
static u32               s_pvr_tex_unit  = 0;
static u32               s_blit_tex_unit = 0;

static GX2ColorBuffer    s_off_cb;
static GX2DepthBuffer    s_off_db;
static GX2Texture        s_off_tex;        // the colour buffer, read back as a texture
static GX2ContextState  *s_off_state = 0;
static float            *s_blit_quad = 0;  // 4 x (x, y, u, v)
static GX2Sampler        s_blit_sampler;

static float s_fb_scale_x = 1.0f, s_fb_scale_y = 1.0f;
static char  s_fps_text[128] = "";

static void *gpu_alloc(u32 size, u32 align)
{
  return MEMAllocFromDefaultHeapEx(size, (int)align);
}

static void gpu_free(void *p)
{
  if (p)
    MEMFreeToDefaultHeap(p);
}

// ============================================================================
// Textures: PVR -> RGBA8, cached
// ============================================================================

// Morton/"twiddle" order, y bit first, rectangular sizes allowed (same
// function as gxRend.cpp twiddle_razi).
static u32 twiddle(u32 x, u32 y, u32 x_sz, u32 y_sz)
{
  u32 rv = 0, sh = 0;
  x_sz >>= 1;
  y_sz >>= 1;
  while (x_sz != 0 || y_sz != 0)
  {
    if (y_sz) { rv |= (y & 1) << sh; y_sz >>= 1; y >>= 1; sh++; }
    if (x_sz) { rv |= (x & 1) << sh; x_sz >>= 1; x >>= 1; sh++; }
  }
  return rv;
}

// VRAM holds host-endian u32 words of DC little-endian data: a DC u16 at byte
// offset a is at (a ^ 2), a DC byte at (a ^ 3) (host_ptr_xor in plugin_header.h).
static inline u16 vram16(u32 a) { return *(u16 *)&params.vram[(a & VRAM_MASK) ^ 2]; }
static inline u8  vram8(u32 a)  { return params.vram[(a & VRAM_MASK) ^ 3]; }
static inline u32 vram32(u32 a) { return *(u32 *)&params.vram[(a & VRAM_MASK) & ~3u]; }

static inline u32 rgba(u32 r, u32 g, u32 b, u32 a) { return (r << 24) | (g << 16) | (b << 8) | a; }

static inline u32 cvt1555(u16 p)
{
  u32 r = (p >> 10) & 31, g = (p >> 5) & 31, b = p & 31;
  return rgba((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2), (p & 0x8000) ? 255 : 0);
}
static inline u32 cvt565(u16 p)
{
  u32 r = p >> 11, g = (p >> 5) & 63, b = p & 31;
  return rgba((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2), 255);
}
static inline u32 cvt4444(u16 p)
{
  return rgba(((p >> 8) & 15) * 17, ((p >> 4) & 15) * 17, (p & 15) * 17, (p >> 12) * 17);
}
static inline u32 cvt8888(u32 p) { return argb_to_rgba(p); }

static inline u32 yuv(s32 Y, s32 U, s32 V)
{
  const s32 c = Y - 16, d = U - 128, e = V - 128;
  s32 r = (298 * c + 409 * e + 128) >> 8;
  s32 g = (298 * c - 100 * d - 208 * e + 128) >> 8;
  s32 b = (298 * c + 516 * d + 128) >> 8;
  r = r < 0 ? 0 : (r > 255 ? 255 : r);
  g = g < 0 ? 0 : (g > 255 ? 255 : g);
  b = b < 0 ? 0 : (b > 255 ? 255 : b);
  return rgba((u32)r, (u32)g, (u32)b, 255);
}

static inline u32 cvt16(u32 fmt, u16 p)
{
  switch (fmt)
  {
  case 1:  return cvt565(p);
  case 2:  return cvt4444(p);
  default: return cvt1555(p);   // 0 = 1555, 7 = reserved (1555 on hardware)
  }
}

static inline u32 pal_entry(u32 idx)
{
  const u32 e = PALETTE_RAM[idx & 1023];
  switch (PAL_RAM_CTRL & 3)
  {
  case 0:  return cvt1555((u16)e);
  case 1:  return cvt565((u16)e);
  case 2:  return cvt4444((u16)e);
  default: return cvt8888(e);
  }
}

// Byte offset from TCW.TexAddr to the full-size level of a mipmapped texture,
// in texels (gxRend.cpp OtherMipPoint / VQMipPoint, indexed by log2(width)).
static const u32 kMipTexels[11] = { 0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558 };
static const u32 kVqMipIdx[11]  = { 0x0, 0x1, 0x2, 0x6, 0x16, 0x56, 0x156, 0x556, 0x1556, 0x5556, 0x15556 };

struct TexInfo
{
  u32 fmt, w, h, base, stride;
  u32 lg;          // log2(width), the mip table index
  bool twiddled, vq, mip;
  u32 pal_base;    // palette index base for PAL4/PAL8
  u32 span;        // bytes of VRAM the texture reads (for the change hash)
};

static void tex_info(u32 tcw, u32 tsp, TexInfo &ti)
{
  ti.fmt      = (tcw >> 27) & 7;
  ti.vq       = (tcw >> 30) & 1;
  ti.mip      = (tcw >> 31) & 1;
  ti.w        = 8u << ((tsp >> 3) & 7);
  ti.h        = 8u << (tsp & 7);
  ti.base     = (tcw & 0x1FFFFF) << 3;
  ti.pal_base = 0;

  const bool pal = (ti.fmt == 5 || ti.fmt == 6);
  ti.twiddled = pal || !((tcw >> 26) & 1);   // ScanOrder; palette textures are always twiddled
  ti.stride   = ti.w;
  if (!ti.twiddled && ((tcw >> 25) & 1))     // StrideSel (planar only)
  {
    const u32 s = (TEXT_CONTROL & 31) * 32;
    if (s)
      ti.stride = s;
  }
  if (ti.mip && ti.twiddled)
    ti.h = ti.w;                             // mipmapped textures are square

  if (ti.fmt == 5)
    ti.pal_base = ((tcw >> 21) & 63) << 4;   // PalSelect, 16 entries per bank
  else if (ti.fmt == 6)
    ti.pal_base = ((tcw >> 21) & 0x30) << 4; // PalSelect[5:4], 256 entries per bank

  ti.lg = 3 + ((tsp >> 3) & 7);
  if (ti.vq)
  {
    // base stays on the codebook; tex_decode() finds the index map.
    ti.span = 2048 + ti.w * ti.h / 4;
    if (ti.mip)
      ti.span += kVqMipIdx[ti.lg];
  }
  else
  {
    const u32 bpp = (ti.fmt == 5) ? 4 : (ti.fmt == 6 ? 8 : 16);
    if (ti.mip && ti.twiddled)
      ti.base += kMipTexels[ti.lg] * bpp / 8;
    ti.span = ti.twiddled ? (ti.w * ti.h * bpp / 8) : (ti.stride * ti.h * 2);
  }
}

static u32 s_tx[WIIU_TEX_MAX_SIDE], s_ty[WIIU_TEX_MAX_SIDE];

// Decode into a linear RGBA8 surface with `pitch` texels per row.
static void tex_decode(const TexInfo &ti, u32 *dst, u32 pitch)
{
  const u32 w = ti.w, h = ti.h;

  if (ti.vq)
  {
    const u32 cb = ti.base;   // 256 codebook entries x 4 texels x 16 bit
    u32 idx_base = ti.base + 2048;
    if (ti.mip)
      idx_base += kVqMipIdx[ti.lg];
    const u32 bw = w / 2, bh = h / 2;
    for (u32 x = 0; x < bw; x++) s_tx[x] = twiddle(x, 0, bw, bh);
    for (u32 y = 0; y < bh; y++) s_ty[y] = twiddle(0, y, bw, bh);
    const bool pal = (ti.fmt == 5 || ti.fmt == 6);
    for (u32 by = 0; by < bh; by++)
      for (u32 bx = 0; bx < bw; bx++)
      {
        const u32 idx = vram8(idx_base + (s_tx[bx] | s_ty[by]));
        const u32 e   = cb + idx * 8;
        // codebook texel order is twiddled: (0,0) (0,1) (1,0) (1,1)
        const u16 p00 = vram16(e + 0), p01 = vram16(e + 2), p10 = vram16(e + 4), p11 = vram16(e + 6);
        u32 *row0 = dst + (by * 2) * pitch + bx * 2;
        u32 *row1 = row0 + pitch;
        if (pal || ti.fmt == 4)   // paletted VQ / bump map: not decoded yet
        {
          row0[0] = row0[1] = row1[0] = row1[1] = 0xFF00FFFFu;
        }
        else if (ti.fmt == 3)
        {
          row0[0] = yuv(p00 >> 8, p00 & 255, p10 & 255);
          row0[1] = yuv(p10 >> 8, p00 & 255, p10 & 255);
          row1[0] = yuv(p01 >> 8, p01 & 255, p11 & 255);
          row1[1] = yuv(p11 >> 8, p01 & 255, p11 & 255);
        }
        else
        {
          row0[0] = cvt16(ti.fmt, p00); row1[0] = cvt16(ti.fmt, p01);
          row0[1] = cvt16(ti.fmt, p10); row1[1] = cvt16(ti.fmt, p11);
        }
      }
    return;
  }

  if (!ti.twiddled)
  {
    // Planar: 16 bpp formats only (palette formats are always twiddled).
    for (u32 y = 0; y < h; y++)
    {
      u32 *row = dst + y * pitch;
      const u32 src = ti.base + y * ti.stride * 2;
      if (ti.fmt == 3)
      {
        for (u32 x = 0; x + 1 < w; x += 2)
        {
          // One u32 = U Y0 V Y1 (DC little-endian order, read as a host u32)
          const u32 wd = vram32(src + x * 2);
          const s32 U = wd & 255, Y0 = (wd >> 8) & 255, V = (wd >> 16) & 255, Y1 = wd >> 24;
          row[x]     = yuv(Y0, U, V);
          row[x + 1] = yuv(Y1, U, V);
        }
      }
      else
      {
        for (u32 x = 0; x < w; x++)
          row[x] = (ti.fmt == 4) ? 0xFFFFFFFFu : cvt16(ti.fmt, vram16(src + x * 2));
      }
    }
    return;
  }

  // Twiddled
  for (u32 x = 0; x < w; x++) s_tx[x] = twiddle(x, 0, w, h);
  for (u32 y = 0; y < h; y++) s_ty[y] = twiddle(0, y, w, h);

  if (ti.fmt == 5 || ti.fmt == 6)
  {
    for (u32 y = 0; y < h; y++)
    {
      u32 *row = dst + y * pitch;
      for (u32 x = 0; x < w; x++)
      {
        const u32 t = s_tx[x] | s_ty[y];
        u32 idx;
        if (ti.fmt == 5)
        {
          const u8 b = vram8(ti.base + (t >> 1));
          idx = (t & 1) ? (b >> 4) : (b & 15);
        }
        else
          idx = vram8(ti.base + t);
        row[x] = pal_entry(ti.pal_base + idx);
      }
    }
    return;
  }

  if (ti.fmt == 3)
  {
    // YUV422 twiddled: per 2x2 block, gxRend.cpp convYUV422_TW layout.
    for (u32 y = 0; y < h; y += 2)
      for (u32 x = 0; x < w; x += 2)
      {
        const u32 t = s_tx[x] | s_ty[y];   // block start; block texels are t..t+3
        const u16 p0 = vram16(ti.base + (t + 0) * 2);   // (x,   y)
        const u16 p1 = vram16(ti.base + (t + 1) * 2);   // (x,   y+1)
        const u16 p2 = vram16(ti.base + (t + 2) * 2);   // (x+1, y)
        const u16 p3 = vram16(ti.base + (t + 3) * 2);   // (x+1, y+1)
        u32 *r0 = dst + y * pitch + x;
        u32 *r1 = r0 + pitch;
        r0[0] = yuv(p0 >> 8, p0 & 255, p2 & 255);
        r0[1] = yuv(p2 >> 8, p0 & 255, p2 & 255);
        r1[0] = yuv(p1 >> 8, p1 & 255, p3 & 255);
        r1[1] = yuv(p3 >> 8, p1 & 255, p3 & 255);
      }
    return;
  }

  for (u32 y = 0; y < h; y++)
  {
    u32 *row = dst + y * pitch;
    for (u32 x = 0; x < w; x++)
      row[x] = (ti.fmt == 4) ? 0xFFFFFFFFu : cvt16(ti.fmt, vram16(ti.base + (s_tx[x] | s_ty[y]) * 2));
  }
}

// Change detection: FNV over the source (sampled past 8 KB) + the palette bank.
static u32 tex_hash(const TexInfo &ti)
{
  u32 hsh = 2166136261u;
  const u32 words = ti.span / 4;
  const u32 step  = (ti.span <= 8192) ? 1 : (words / 1024 + 1);
  for (u32 i = 0; i < words; i += step)
    hsh = (hsh ^ vram32(ti.base + i * 4)) * 16777619u;
  if (ti.fmt == 5 || ti.fmt == 6)
  {
    const u32 n = (ti.fmt == 5) ? 16 : 256;
    for (u32 i = 0; i < n; i++)
      hsh = (hsh ^ PALETTE_RAM[(ti.pal_base + i) & 1023]) * 16777619u;
    hsh ^= PAL_RAM_CTRL & 3;
  }
  return hsh;
}

struct TexEntry
{
  u32        key_tcw, key_tsp;   // tsp: size bits only
  u32        hash;
  u32        checked_frame;      // last frame the hash was verified
  u32        used_frame;
  bool       live;
  GX2Texture tex;
};

static TexEntry s_tex[WIIU_TEX_CACHE_SLOTS];
static u32      s_frame = 0;
static u32      s_tex_bytes = 0;
#define TEX_BUDGET (96u * 1024u * 1024u)

static void tex_free(TexEntry &e)
{
  if (e.live)
  {
    s_tex_bytes -= e.tex.surface.imageSize;
    gpu_free(e.tex.surface.image);
  }
  memset(&e, 0, sizeof(e));
}

static void tex_cache_clear()
{
  for (u32 i = 0; i < WIIU_TEX_CACHE_SLOTS; i++)
    tex_free(s_tex[i]);
  s_tex_bytes = 0;
}

static TexEntry *tex_evict_lru()
{
  TexEntry *best = 0;
  for (u32 i = 0; i < WIIU_TEX_CACHE_SLOTS; i++)
  {
    TexEntry &e = s_tex[i];
    if (!e.live)
      return &e;
    if (e.used_frame != s_frame && (!best || e.used_frame < best->used_frame))
      best = &e;
  }
  if (best)
    tex_free(*best);
  return best;
}

static GX2Texture *tex_get(u32 tcw, u32 tsp)
{
  const u32 key_tsp = tsp & 0x3F;   // TexU/TexV
  const u32 slot0   = ((tcw * 2654435761u) ^ key_tsp) % WIIU_TEX_CACHE_SLOTS;

  TexEntry *e = 0;
  for (u32 i = 0; i < 16; i++)   // short linear probe
  {
    TexEntry &c = s_tex[(slot0 + i) % WIIU_TEX_CACHE_SLOTS];
    if (c.live && c.key_tcw == tcw && c.key_tsp == key_tsp)
    {
      e = &c;
      break;
    }
  }

  TexInfo ti;
  tex_info(tcw, tsp, ti);
  if (ti.w > WIIU_TEX_MAX_SIDE || ti.h > WIIU_TEX_MAX_SIDE)
    return 0;

  if (e)
  {
    e->used_frame = s_frame;
    if (e->checked_frame == s_frame)
      return &e->tex;
    e->checked_frame = s_frame;
    const u32 hsh = tex_hash(ti);
    if (hsh == e->hash)
      return &e->tex;
    // Same texture slot, new contents: re-decode in place (same size).
    e->hash = hsh;
    tex_decode(ti, (u32 *)e->tex.surface.image, e->tex.surface.pitch);
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, e->tex.surface.image, e->tex.surface.imageSize);
    return &e->tex;
  }

  // Miss: find a free slot near the hash, else evict the least recently used.
  for (u32 i = 0; i < 16 && !e; i++)
  {
    TexEntry &c = s_tex[(slot0 + i) % WIIU_TEX_CACHE_SLOTS];
    if (!c.live)
      e = &c;
  }
  if (!e)
    e = tex_evict_lru();
  if (!e)
    return 0;

  GX2Texture &t = e->tex;
  memset(&t, 0, sizeof(t));
  t.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
  t.surface.width     = ti.w;
  t.surface.height    = ti.h;
  t.surface.depth     = 1;
  t.surface.mipLevels = 1;
  t.surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
  t.surface.aa        = GX2_AA_MODE1X;
  t.surface.use       = GX2_SURFACE_USE_TEXTURE;
  t.surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
  t.viewNumMips       = 1;
  t.viewNumSlices     = 1;
  t.compMap           = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
  GX2CalcSurfaceSizeAndAlignment(&t.surface);
  GX2InitTextureRegs(&t);

  while (s_tex_bytes + t.surface.imageSize > TEX_BUDGET)
    if (!tex_evict_lru())
      break;

  t.surface.image = gpu_alloc(t.surface.imageSize, t.surface.alignment);
  if (!t.surface.image)
  {
    memset(e, 0, sizeof(*e));
    return 0;
  }
  s_tex_bytes += t.surface.imageSize;

  e->live          = true;
  e->key_tcw       = tcw;
  e->key_tsp       = key_tsp;
  e->used_frame    = s_frame;
  e->checked_frame = s_frame;
  e->hash          = tex_hash(ti);
  tex_decode(ti, (u32 *)t.surface.image, t.surface.pitch);
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_TEXTURE, t.surface.image, t.surface.imageSize);
  return &t;
}

// ============================================================================
// Drawing
// ============================================================================

// TSP SrcInstr / DstInstr -> GX2 blend factors. "Other colour" is the
// destination colour for the source factor and the source colour for the
// destination factor.
static const GX2BlendMode kSrcBlend[8] = {
  GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_DST_COLOR, GX2_BLEND_MODE_INV_DST_COLOR,
  GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_MODE_DST_ALPHA, GX2_BLEND_MODE_INV_DST_ALPHA };
static const GX2BlendMode kDstBlend[8] = {
  GX2_BLEND_MODE_ZERO, GX2_BLEND_MODE_ONE, GX2_BLEND_MODE_SRC_COLOR, GX2_BLEND_MODE_INV_SRC_COLOR,
  GX2_BLEND_MODE_SRC_ALPHA, GX2_BLEND_MODE_INV_SRC_ALPHA, GX2_BLEND_MODE_DST_ALPHA, GX2_BLEND_MODE_INV_DST_ALPHA };

static inline GX2TexClampMode tex_clamp(u32 clamp, u32 flip)
{
  return clamp ? GX2_TEX_CLAMP_MODE_CLAMP : (flip ? GX2_TEX_CLAMP_MODE_MIRROR : GX2_TEX_CLAMP_MODE_WRAP);
}

// Per-header GPU state. Called only when the header differs from the last one.
static void bind_param(const GParam &p, u32 pass)
{
  const u32 isp = p.isp, tsp = p.tsp;

  // Depth: ISP DepthMode compares 1/W, and the shader keeps depth monotonic in
  // 1/W, so the 3-bit mode is the GX2 compare function as-is. Translucent
  // polys never write Z (the PVR sorts them instead of depth-writing).
  const u32 depth_mode = isp >> 29;
  const bool zwrite = (pass != ListType_Translucent) && !((isp >> 26) & 1);
  GX2SetDepthOnlyControl(TRUE, zwrite ? TRUE : FALSE, (GX2CompareFunction)depth_mode);

  if (pass == ListType_Translucent)
  {
    const u32 src = (tsp >> 29) & 7, dst = (tsp >> 26) & 7;
    GX2SetBlendControl(GX2_RENDER_TARGET_0,
                       kSrcBlend[src], kDstBlend[dst], GX2_BLEND_COMBINE_MODE_ADD,
                       TRUE, kSrcBlend[src], kDstBlend[dst], GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x01, FALSE, TRUE);
  }
  else
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);

  // Pixel shader mode: x = textured, y = shading instruction,
  // z = ignore texture alpha, w = use vertex alpha.
  const bool textured = (p.pcw >> 3) & 1;
  float mode[4] = { 0.0f, (float)((tsp >> 6) & 3), (float)((tsp >> 19) & 1), (float)((tsp >> 20) & 1) };

  if (textured)
  {
    GX2Texture *t = tex_get(p.tcw, tsp);
    if (t)
    {
      GX2Sampler smp;
      GX2InitSampler(&smp, GX2_TEX_CLAMP_MODE_WRAP,
                     ((tsp >> 13) & 3) ? GX2_TEX_XY_FILTER_MODE_LINEAR : GX2_TEX_XY_FILTER_MODE_POINT);
      GX2InitSamplerClamping(&smp, tex_clamp((tsp >> 16) & 1, (tsp >> 18) & 1),
                                   tex_clamp((tsp >> 15) & 1, (tsp >> 17) & 1),
                                   GX2_TEX_CLAMP_MODE_WRAP);
      GX2SetPixelTexture(t, s_pvr_tex_unit);
      GX2SetPixelSampler(&smp, s_pvr_tex_unit);
      mode[0] = 1.0f;
    }
  }
  GX2SetPixelUniformReg(s_u_mode, 4, mode);
}

static void draw_pass(u32 pass)
{
  // Punch-through: alpha test against PT_ALPHA_REF, no blending.
  GX2SetAlphaTest(pass == ListType_Punch_Through ? TRUE : FALSE,
                  GX2_COMPARE_FUNC_GEQUAL, (float)(PT_ALPHA_REF & 0xFF) / 255.0f);

  u32 bound = 0xFFFFFFFFu;
  for (u32 i = 0; i < s_nstrips; i++)
  {
    const GStrip &s = s_strips[i];
    const GParam &p = s_params[s.param];
    if (p.list != pass)
      continue;
    if (s.param != bound)
    {
      bind_param(p, pass);
      bound = s.param;
    }
    GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, s.count, s.first, 1);
  }
}

// f(x) = x / (1 + x): the depth the shader writes for 1/W = x. Used to clear
// the depth buffer to the background plane's depth.
static float depth_of_inv_w(float x)
{
  if (!(x > 0.0f))
    return 0.0f;
  return x / (1.0f + x);
}

static void render_scene()
{
  GX2SetContextState(s_off_state);
  GX2SetColorBuffer(&s_off_cb, GX2_RENDER_TARGET_0);
  GX2SetDepthBuffer(&s_off_db);

  union { u32 i; float f; } bgd;
  bgd.i = ISP_BACKGND_D;
  GX2ClearColor(&s_off_cb, 0.0f, 0.0f, 0.0f, 1.0f);
  GX2ClearDepthStencilEx(&s_off_db, depth_of_inv_w(bgd.f), 0, GX2_CLEAR_FLAGS_DEPTH);
  GX2SetContextState(s_off_state);   // the clears clobber state (libwhb does the same)

  GX2SetViewport(0.0f, 0.0f, (float)WIIU_DC_FB_WIDTH, (float)WIIU_DC_FB_HEIGHT, 0.0f, 1.0f);
  GX2SetScissor(0, 0, WIIU_DC_FB_WIDTH, WIIU_DC_FB_HEIGHT);
  GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);

  if (s_nstrips == 0)
    return;

  GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, s_vtx, s_nvtx * sizeof(GVtx));

  GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
  GX2SetFetchShader(&s_pvr_shader.fetchShader);
  GX2SetVertexShader(s_pvr_shader.vertexShader);
  GX2SetPixelShader(s_pvr_shader.pixelShader);
  GX2SetAttribBuffer(0, s_nvtx * sizeof(GVtx), sizeof(GVtx), s_vtx);

  // Canvas pixels -> NDC (y down on the DC, up in NDC).
  const float xform[4] = { 2.0f * s_fb_scale_x / (float)WIIU_DC_FB_WIDTH,
                           -2.0f * s_fb_scale_y / (float)WIIU_DC_FB_HEIGHT,
                           -1.0f, 1.0f };
  GX2SetVertexUniformReg(s_u_xform, 4, xform);

  draw_pass(ListType_Opaque);
  draw_pass(ListType_Punch_Through);
  draw_pass(ListType_Translucent);
}

// Draw the offscreen frame 4:3 into the currently bound TV/GamePad target.
static void blit_to(u32 tw, u32 th)
{
  u32 vw = th * 4 / 3, vh = th;
  if (vw > tw) { vw = tw; vh = tw * 3 / 4; }
  const u32 vx = (tw - vw) / 2, vy = (th - vh) / 2;

  GX2SetViewport((float)vx, (float)vy, (float)vw, (float)vh, 0.0f, 1.0f);
  GX2SetScissor(vx, vy, vw, vh);
  GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_ALWAYS);
  GX2SetColorControl(GX2_LOGIC_OP_COPY, 0x00, FALSE, TRUE);
  GX2SetAlphaTest(FALSE, GX2_COMPARE_FUNC_ALWAYS, 0.0f);
  GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);

  GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
  GX2SetFetchShader(&s_blit_shader.fetchShader);
  GX2SetVertexShader(s_blit_shader.vertexShader);
  GX2SetPixelShader(s_blit_shader.pixelShader);
  GX2SetPixelTexture(&s_off_tex, s_blit_tex_unit);
  GX2SetPixelSampler(&s_blit_sampler, s_blit_tex_unit);
  GX2SetAttribBuffer(0, 4 * 4 * sizeof(float), 4 * sizeof(float), s_blit_quad);
  GX2DrawEx(GX2_PRIMITIVE_MODE_TRIANGLE_STRIP, 4, 0, 1);
}

static void present()
{
  WHBGfxBeginRender();

  // The offscreen colour buffer is about to be sampled as a texture.
  GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_COLOR_BUFFER | GX2_INVALIDATE_MODE_TEXTURE),
                s_off_cb.surface.image, s_off_cb.surface.imageSize);

  WHBGfxBeginRenderTV();
  WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  {
    GX2ColorBuffer *cb = WHBGfxGetTVColourBuffer();
    blit_to(cb->surface.width, cb->surface.height);
  }
  WHBGfxFinishRenderTV();

  WHBGfxBeginRenderDRC();
  WHBGfxClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  {
    GX2ColorBuffer *cb = WHBGfxGetDRCColourBuffer();
    blit_to(cb->surface.width, cb->surface.height);
  }
  WHBGfxFinishRenderDRC();

  WHBGfxFinishRender();   // swap + GX2DrawDone: every buffer is free again
}

// ============================================================================
// Initialisation
// ============================================================================

static bool load_shader(WHBGfxShaderGroup &g, const u8 *gsh, bool pvr)
{
  memset(&g, 0, sizeof(g));
  if (!WHBGfxLoadGFDShaderGroup(&g, 0, gsh))
  {
    printf("[gx2Rend] shader group load failed (%s)\n", pvr ? "pvr" : "blit");
    return false;
  }
  if (pvr)
  {
    WHBGfxInitShaderAttribute(&g, "a_pos", 0, 0,  GX2_ATTRIB_FORMAT_FLOAT_32_32_32);
    WHBGfxInitShaderAttribute(&g, "a_uv",  0, 12, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(&g, "a_col", 0, 20, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8);
    WHBGfxInitShaderAttribute(&g, "a_spc", 0, 24, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8);
  }
  else
  {
    WHBGfxInitShaderAttribute(&g, "a_pos", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(&g, "a_uv",  0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32);
  }
  if (!WHBGfxInitFetchShader(&g))
  {
    printf("[gx2Rend] fetch shader init failed (%s)\n", pvr ? "pvr" : "blit");
    return false;
  }
  return true;
}

static bool init_offscreen()
{
  memset(&s_off_cb, 0, sizeof(s_off_cb));
  s_off_cb.surface.use       = (GX2SurfaceUse)(GX2_SURFACE_USE_TEXTURE | GX2_SURFACE_USE_COLOR_BUFFER);
  s_off_cb.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
  s_off_cb.surface.width     = WIIU_DC_FB_WIDTH;
  s_off_cb.surface.height    = WIIU_DC_FB_HEIGHT;
  s_off_cb.surface.depth     = 1;
  s_off_cb.surface.mipLevels = 1;
  s_off_cb.surface.format    = GX2_SURFACE_FORMAT_UNORM_R8_G8_B8_A8;
  s_off_cb.surface.aa        = GX2_AA_MODE1X;
  s_off_cb.surface.tileMode  = GX2_TILE_MODE_DEFAULT;
  s_off_cb.viewNumSlices     = 1;
  GX2CalcSurfaceSizeAndAlignment(&s_off_cb.surface);
  GX2InitColorBufferRegs(&s_off_cb);
  s_off_cb.surface.image = gpu_alloc(s_off_cb.surface.imageSize, s_off_cb.surface.alignment);
  if (!s_off_cb.surface.image)
    return false;
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_off_cb.surface.image, s_off_cb.surface.imageSize);

  memset(&s_off_db, 0, sizeof(s_off_db));
  s_off_db.surface.use       = GX2_SURFACE_USE_DEPTH_BUFFER;
  s_off_db.surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
  s_off_db.surface.width     = WIIU_DC_FB_WIDTH;
  s_off_db.surface.height    = WIIU_DC_FB_HEIGHT;
  s_off_db.surface.depth     = 1;
  s_off_db.surface.mipLevels = 1;
  s_off_db.surface.format    = GX2_SURFACE_FORMAT_FLOAT_R32;   // 32-bit float depth
  s_off_db.surface.aa        = GX2_AA_MODE1X;
  s_off_db.surface.tileMode  = GX2_TILE_MODE_DEFAULT;
  s_off_db.viewNumSlices     = 1;
  s_off_db.depthClear        = 0.0f;   // farthest: f(1/W) is 0 at infinity
  GX2CalcSurfaceSizeAndAlignment(&s_off_db.surface);
  GX2InitDepthBufferRegs(&s_off_db);
  s_off_db.surface.image = gpu_alloc(s_off_db.surface.imageSize, s_off_db.surface.alignment);
  if (!s_off_db.surface.image)
    return false;
  GX2Invalidate(GX2_INVALIDATE_MODE_CPU, s_off_db.surface.image, s_off_db.surface.imageSize);

  // The same pixels, viewed as a texture for the TV/GamePad blit.
  memset(&s_off_tex, 0, sizeof(s_off_tex));
  s_off_tex.surface       = s_off_cb.surface;
  s_off_tex.surface.use   = GX2_SURFACE_USE_TEXTURE;
  s_off_tex.viewNumMips   = 1;
  s_off_tex.viewNumSlices = 1;
  s_off_tex.compMap       = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_G, GX2_SQ_SEL_B, GX2_SQ_SEL_A);
  GX2InitTextureRegs(&s_off_tex);

  s_off_state = (GX2ContextState *)gpu_alloc(sizeof(GX2ContextState), GX2_CONTEXT_STATE_ALIGNMENT);
  if (!s_off_state)
    return false;
  GX2SetupContextStateEx(s_off_state, TRUE);
  GX2SetContextState(s_off_state);
  GX2SetColorBuffer(&s_off_cb, GX2_RENDER_TARGET_0);
  GX2SetDepthBuffer(&s_off_db);
  return true;
}

bool InitRenderer()
{
  printf("[gx2Rend] InitRenderer: GX2, %ux%u offscreen -> TV + GamePad\n",
         (unsigned)WIIU_DC_FB_WIDTH, (unsigned)WIIU_DC_FB_HEIGHT);
  fflush(stdout);

  if (!s_gfx_up)
  {
    if (!WHBGfxInit())
    {
      printf("[gx2Rend] WHBGfxInit failed\n");
      return false;
    }
    s_gfx_up = true;

    if (!load_shader(s_pvr_shader, pvr_gsh, true) || !load_shader(s_blit_shader, blit_gsh, false))
      return false;

    const GX2UniformVar *ux = GX2GetVertexUniformVar(s_pvr_shader.vertexShader, "u_xform");
    const GX2UniformVar *um = GX2GetPixelUniformVar(s_pvr_shader.pixelShader, "u_mode");
    if (!ux || !um)
    {
      printf("[gx2Rend] pvr shader uniforms missing (u_xform=%p u_mode=%p): "
             "rebuild the .gsh with --uniform-mode register\n", (const void *)ux, (const void *)um);
      return false;
    }
    s_u_xform = ux->offset;
    s_u_mode  = um->offset;
    s_pvr_tex_unit  = s_pvr_shader.pixelShader->samplerVarCount
                    ? s_pvr_shader.pixelShader->samplerVars[0].location : 0;
    s_blit_tex_unit = s_blit_shader.pixelShader->samplerVarCount
                    ? s_blit_shader.pixelShader->samplerVars[0].location : 0;

    if (!init_offscreen())
    {
      printf("[gx2Rend] offscreen target allocation failed\n");
      return false;
    }

    s_vtx = (GVtx *)gpu_alloc(WIIU_TA_VTX_MAX * sizeof(GVtx), GX2_VERTEX_BUFFER_ALIGNMENT);
    s_blit_quad = (float *)gpu_alloc(16 * sizeof(float), GX2_VERTEX_BUFFER_ALIGNMENT);
    if (!s_vtx || !s_blit_quad)
    {
      printf("[gx2Rend] vertex buffer allocation failed\n");
      return false;
    }
    // Triangle strip covering NDC, uv (0,0) = top-left of the DC frame.
    static const float kQuad[16] = {
      -1.0f, -1.0f, 0.0f, 1.0f,
       1.0f, -1.0f, 1.0f, 1.0f,
      -1.0f,  1.0f, 0.0f, 0.0f,
       1.0f,  1.0f, 1.0f, 0.0f };
    memcpy(s_blit_quad, kQuad, sizeof(kQuad));
    GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, s_blit_quad, sizeof(kQuad));
    GX2InitSampler(&s_blit_sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);

    memset(s_tex, 0, sizeof(s_tex));
  }

  ta_reset();
  printf("[gx2Rend] ready (pvr sampler unit %u, blit unit %u)\n",
         (unsigned)s_pvr_tex_unit, (unsigned)s_blit_tex_unit);
  fflush(stdout);
  return TileAccel.Init();
}

void TermRenderer()
{
  TileAccel.Term();
}

// wiiu/wiiu_exit.cpp, after the ProcUI hand-off. Safe when never initialised.
extern "C" void gx2Rend_Shutdown(void)
{
  if (!s_gfx_up)
    return;
  tex_cache_clear();
  WHBGfxFreeShaderGroup(&s_pvr_shader);
  WHBGfxFreeShaderGroup(&s_blit_shader);
  gpu_free(s_vtx);              s_vtx = 0;
  gpu_free(s_blit_quad);        s_blit_quad = 0;
  gpu_free(s_off_cb.surface.image);
  gpu_free(s_off_db.surface.image);
  gpu_free(s_off_state);        s_off_state = 0;
  WHBGfxShutdown();
  s_gfx_up = false;
}

void ResetRenderer(bool Manual)
{
  TileAccel.Reset(Manual);
  ta_reset();
  tex_cache_clear();
  VertexCount = 0;
  FrameCount  = 0;
}

// ============================================================================
// Per-frame entry points
// ============================================================================

void StartRender()
{
  const u32 vtx_cnt = s_nvtx;
  VertexCount += vtx_cnt;
  StripCount  += s_nstrips;

  // Render-done interrupt timing: the same three modes as gxRend.cpp
  // (RENDER_DELAY preset). SPG.cpp counts these down and raises the IRQs;
  // games pace off them, so they matter even though the GPU is done long
  // before.
  const int rd_mode = get_render_delay_preset();
  if (rd_mode == 1)
  {
    render_isp_pending_cycles = 800000;
    render_tsp_pending_cycles = 850000;
    render_vd_pending_cycles  = 900000;
    render_end_pending_cycles = 0;
  }
  else if (rd_mode == 2)
  {
    s32 rd = 4096;
    if (s_rd_list_init)
    {
      const u32 ta_bytes = 32u * (vtx_cnt + s_nstrips);
      rd = (ta_bytes >= (1500000u - 450000u) / 100u) ? 1500000 : 450000 + (s32)ta_bytes * 100;
    }
    render_isp_pending_cycles = 0;
    render_tsp_pending_cycles = 0;
    render_vd_pending_cycles  = 0;
    render_end_pending_cycles = rd;
  }
  else
  {
    render_end_pending_cycles = vtx_cnt * 15;
    if (render_end_pending_cycles < 50000)
      render_end_pending_cycles = 50000;
  }
  s_rd_list_init = false;

  // Render-to-texture (FB_W_SOF1 bit 24): not supported yet, drop the pass.
  // Exit requested: once ProcUI has let the foreground go ("Close software"),
  // GX2 must not be touched while the SH4 finishes its timeslice.
  if (!s_gfx_up || (FB_W_SOF1 & 0x1000000) || WiiU_ExitRequested())
  {
    ta_reset();
    return;
  }

  s_frame++;
  render_scene();
  present();
  ta_reset();
  FrameCount++;
}

void EndRender() {}

void VBlank()
{
  // ProcUI must be pumped on the main core: the HOME menu blocks in here, and
  // "Close software" turns into an exit request.
  WiiU_ProcPump();
}

bool ThreadStart() { return true; }
void ThreadEnd() {}

void ListCont()
{
  TileAccel.ListCont();
}

void ListInit()
{
  TileAccel.ListInit();
  s_rd_list_init = true;
}

void SoftReset()
{
  TileAccel.SoftReset();
}

void SetFpsText(char *text)
{
  strncpy(s_fps_text, text, sizeof(s_fps_text) - 1);
  s_fps_text[sizeof(s_fps_text) - 1] = 0;
}

void SetFbScale(float x, float y)
{
  s_fb_scale_x = x;
  s_fb_scale_y = y;
}

// SPG.cpp's once-a-second census hook (gxRend.cpp STRIP_DEDUP): nothing here.
void strip_dedup_dump(double tdiff) {}

#endif // HOST_OS == OS_WIIU
