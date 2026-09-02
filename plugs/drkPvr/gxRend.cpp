// Wii Rendering

#include <cstdio>

// RUNTIME - PRESET SELECTION

// This is defined in main.cpp
extern "C" int get_accuracy_preset();

// Helper macros to check current accuracy mode
#define FAST() (get_accuracy_preset() == 0)
#define BALANCED() (get_accuracy_preset() == 1)
#define ACCURATE() (get_accuracy_preset() == 2)

// This is defined in main.cpp
extern "C" int get_graphism_preset();

// Helper macros to check current graphism mode. GRAPHICS is now ONLY the
// texture filter: the old HIGH/EXTRA levels bundled a fixed LOD bias / bias
// clamp / aniso set on top of NORMAL, and those three are separate user
// presets now (get_gx_preset / get_lod_bias_preset / get_aniso_preset below).
#define LOW() (get_graphism_preset() == 0)
#define NORMAL() (get_graphism_preset() != 0)

// GX texture-LOD extras: the biasclamp + edgelod pair of GX_InitTexObjLOD().
// 0 = GX_DISABLE (default), 1 = GX_ENABLE.
extern "C" int get_gx_preset();
#define GX_LOD_EXTRAS() (get_gx_preset() == 1)

// Texture LOD bias step (index into s_lod_bias_steps) and anisotropy step
// (index into s_aniso_steps). Both default to their no-op entry.
extern "C" int get_lod_bias_preset();
extern "C" int get_aniso_preset();

// FULLSCREEN or 4:3 (4:3 still has bugs in some games)
extern "C" int get_ratio_preset();

#define ORIGINAL() (get_ratio_preset() == 0)
#define FULLSCREEN() (get_ratio_preset() == 1)
#define RATIO_AUTO() (get_ratio_preset() == 2)

// Effective "fill the full framebuffer width" decision, shared by the 3D
// viewport framing and the 2D framebuffer blit. Legacy modes honor the manual
// ORIGINAL/FULLSCREEN choice; RATIO_AUTO() reads the console's aspect setting
// (SKILL.md §3: query CONF_GetAspectRatio instead of hardcoding a TV shape):
//   * 4:3 console  -> the TV shows all 640 px as 4:3, so DC content fills width.
//   * 16:9 console -> the TV stretches 640 to 16:9, so pillarbox to a 4:3
//                     sub-rect to keep correct proportions.
// A CONF error (<0) is treated as not-4:3 -> pillarbox, a safe fallback.
#define RATIO_FULL_WIDTH() (RATIO_AUTO() ? (CONF_GetAspectRatio() == CONF_ASPECT_4_3) : FULLSCREEN())

// ADVANCED ALPHA (may be slower but haven't really noticed any difference)
extern "C" int get_advanced_alpha_preset();
#define ADVANCED_ALPHA() (get_advanced_alpha_preset() == 1)

bool use_adv_alpha = ADVANCED_ALPHA(); // compute once

// This is defined in main.cpp
extern "C" int get_debug_message();
#define DEBUG_MESSAGE() (get_debug_message() == 1)

extern "C" int get_debug_loop();

// Specific debug for FMV
int fmv_debug = 0;

// Frame skipping
extern "C" int get_frameskip_preset();

#define NO_FRAMESKIP() (get_frameskip_preset() == 0)
#define FRAMESKIP_1() (get_frameskip_preset() == 1)
#define FRAMESKIP_2() (get_frameskip_preset() == 2)
#define FRAMESKIP_AUTO() (get_frameskip_preset() == 3)
#define FRAMESKIP_AUTO_MAX() (get_frameskip_preset() == 4)

// 4BPP palette texture management
extern "C" int get_4bpp_preset();

#define TEXTURE_4BPP_I4_STUB() (get_4bpp_preset() == 0) // I4 Stub // 34 FPS VMU Menu Daytona
#define TEXTURE_4BPP_OPTIMIZED() (get_4bpp_preset() == 1) // 4BPP Optimized // 34 FPS VMU Menu Daytona (untested)
#define TEXTURE_4BPP_CI4_FAST() (get_4bpp_preset() == 2) // CI4 (supposed to be fast) // 34 FPS VMU Menu Daytona
#define TEXTURE_4BPP_CI4() (get_4bpp_preset() == 3) // CI4 (supposed to be accurate) // 27 FPS VMU Menu Daytona
#define TEXTURE_4BPP_RGB565() (get_4bpp_preset() == 4) // RGB565 // 33 FPS VMU Menu Daytona (worse display)

// 8BPP palette texture management
extern "C" int get_8bpp_preset();

#define TEXTURE_8BPP_I8_STUB() (get_8bpp_preset() == 0) // I8 Stub
#define TEXTURE_8BPP_OPTIMIZED() (get_8bpp_preset() == 1) // 8BPP Optimized
#define TEXTURE_8BPP_CI8_FAST() (get_8bpp_preset() == 2) // CI8 (supposed to be fast)
#define TEXTURE_8BPP_CI8() (get_8bpp_preset() == 3)      // CI8 (supposed to be accurate)
#define TEXTURE_8BPP_RGB565() (get_8bpp_preset() == 4) // RGB565

// Jojo's Bizarre Adventure texture-cache fix: TLUT-reupload-skip + CACHE_FAST
extern "C" int get_jojo_fix_preset();
#define JOJO_FIX() (get_jojo_fix_preset() != 0)

// VQ textures decoded to GX_TF_CMPR (DXT1) instead of 16bpp — see
// texture_VQ_CMPR(). 4 bits/texel instead of 16, which is what lets a VQ
// texture fit its address-derived cache slot instead of needing the overflow
// arena. 565 source only.
//
// UNPROVEN IDEA - extending this to 1555, and why it is not here.
// 1555 is 1-bit alpha, which is exactly what CMPR's second block mode carries:
// when the two endpoints compare c0 <= c1 the palette becomes c0, c1,
// (c0+c1)/2, and index 3 = TRANSPARENT. So punch-through VQ (foliage, fences,
// chain-link, signage) could shrink to 4 bits/texel too and fit its slot,
// instead of falling back to the overflow arena the way it does now.
//
// It was written and it REGRESSED on hardware - Power Stone 2 in particular.
// Prime suspect: a texel the DC marks transparent loses its RGB. CMPR index 3
// is transparent BLACK, while RGB5A3 keeps the colour alongside alpha 0, and a
// polygon drawn with TSP.IgnoreTexA displays that colour - so a punch-through
// texture gains black where it used to show colour. Power Stone 2 is full of
// punch-through effects, which fits. Not confirmed; it could equally be the
// endpoint-ordering issue noted above encode_cmpr_block().
//
// The work is parked on branch `vq_cmpr_1555` (encode_cmpr_block_a1() plus the
// case 0/7 wiring). Do not revive it without a way to tell IgnoreTexA polygons
// apart, or without first proving the regression is something else.
extern "C" int get_vq_cmpr_preset();
#define VQ_CMPR() (get_vq_cmpr_preset() == 1)

// Vertex-color Intensity (Gouraud) (cars in Crazy Taxi 1)
extern "C" int get_vertex_color_preset();
#define VERTEX_COLOR() (get_vertex_color_preset() != 0)

// Texture cache management
extern "C" int get_texture_cache_preset();

#define CACHE_VERY_FAST()   (get_texture_cache_preset() == 0) // Original nullDC4Wii/SKMP algorithm.
#define CACHE_FAST()        (get_texture_cache_preset() == 1) // Persistent bump-allocated cache (original, no shape-resize safety)
#define CACHE_NORMAL()      (get_texture_cache_preset() == 2) // Persistent bump-allocated cache (correct sizing, cross-frame, shape-resize safe)
#define CACHE_QUALITY()     (get_texture_cache_preset() == 3) // Quality with enhancements
#define CACHE_EXTRA()       (get_texture_cache_preset() == 4) // Perfect Result (to the cost of FPS) — For Debug only
#define CACHE_EXTRA_DEBUG() (get_texture_cache_preset() == 5) // Pure bump allocation, always re-decode — For Debug only
#define CACHE_VERY_FAST_PLUS() (get_texture_cache_preset() == 6) // VERY_FAST slots, sized fallback, content-validated

// CACHE_VERY_FAST_PLUS sits between VERY_FAST and FAST.
//
// Measured on Buggy Heat gameplay, 60 frames, same scene:
//   very_fast : 33960 binds,    0 decodes  (100% cached)
//   normal    : 34239 binds, 4437 decodes  ( 87% cached, 73/frame)
//   plus v4   : 19670 binds, 2593 decodes  ( 86% cached, 43/frame), 170 arena
//               wraps, 85% of binds routed to the arena — v4 carved the arena
//               out of vram_buffer, which shrank the slot address range and
//               pushed most textures into a 4 MB arena that could not hold them.
//               v5 gives the arena its own MEM2 block so the slot range is whole
//               again, and gives up on the arena entirely if it still thrashes.
// VERY_FAST is not really a cache: slot = vram_buffer[tex_addr * 2] gives every
// DC address a permanent private home (8 MB of VRAM spread over 16 MB of
// buffer), so there is no lookup, no capacity limit and no eviction. The 14 MB
// FAST/NORMAL arena cannot hold a real scene's decoded textures, so it evicts
// and re-decodes forever. 73 decodes a frame at roughly 330k cycles each is the
// whole FPS gap; no amount of tuning the lookup was ever going to close it.
//
// So PLUS keeps VERY_FAST's slot for every texture that provably fits it, and
// routes only the ones that would overrun into a sized bump arena living in its
// own MEM2 block (see tex_arena_base()). If that arena is absent or proves too
// small for the scene, PLUS uses the slot for everything and accepts VERY_FAST's
// overrun — never the thrash. On top of that it fixes three things VERY_FAST
// and FAST/NORMAL respectively get wrong:
//
//  1. STRIDE ALLOCATION SIZE. A stride-selected scan-order texture does not
//     decode at its declared width: norm_text() and the YUV path both raise w
//     to 512 first. But `decode_bytes` is computed from 8<<TexU, so the slot is
//     under-allocated by up to 16x and the decoder writes straight past its
//     end. In the PER-FRAME arenas (QUALITY/EXTRA) that overrun lands in space
//     the allocator has not handed out yet, so the next texture decoded that
//     frame simply overwrites it and nothing is ever visibly wrong. In the
//     PERSISTENT FAST/NORMAL arena it lands on a neighbour that was cached on
//     an earlier frame and will NOT be re-decoded — permanently corrupting its
//     pixels and its descriptor. A clobbered descriptor then fails its own
//     validity check forever, so it re-decodes every draw and can overrun the
//     NEXT neighbour in turn. That cascade is why 2D menus (which are exactly
//     what stride textures are used for) look wrong AND run slowly on
//     FAST/NORMAL while being fine on VERY_FAST and QUALITY.
//  2. STRIDE SURFACES ARE VALIDATED BY CONTENT, not by shape. VERY_FAST and
//     QUALITY end their validity check with !(StrideSel && ScanOrder), i.e.
//     they never cache these at all — correct, because shape_key only
//     describes the FORMAT and cannot notice that the game repainted the
//     surface, but expensive, because every draw re-converts 512*h texels.
//     PLUS hashes 8 words spread across the surface instead (see
//     stride_fingerprint()) and re-decodes only when the hash moves, so static
//     menu art caches and animated art still refreshes. No sentinel is written
//     for these: on a scan-order surface it lands on visible top-left pixels.
//  3. SENTINEL ADDRESS. FAST/NORMAL write the 0xDEADBEEF sentinel at
//     `tex_addr`, which the twiddled/YUV/4BPP/8BPP decoders have by then
//     advanced past the small mip levels (twidle_tex() does
//     `tex_addr += OtherMipPoint[...]`). The validity check reads `ptex_skmp`,
//     still pointing at the ORIGINAL address, so no mipmapped non-VQ texture
//     ever registers a hit — and the stomp lands in the full-size mip level
//     the decoder actually samples, which is visible corruption. VERY_FAST is
//     immune (it writes through the saved `ptex_skmp`); PLUS does the same.
//
// FAST and NORMAL are deliberately left exactly as they were, so the game
// presets tuned against them keep the behaviour they were tested with.
#define CACHE_NORMAL_FAMILY() (CACHE_NORMAL() || CACHE_VERY_FAST_PLUS())

// Per Polygon Z Write
extern "C" int get_ppz_write_preset();

#define PER_POLYGON_Z_WRITE() (get_ppz_write_preset() == 1) // Fix Test-drive 6 draw distance

// DecalAlpha shading fix (cars in Crazy Taxi 1)
extern "C" int get_decal_alpha_preset();
#define DECAL_ALPHA_FIX() (get_decal_alpha_preset() == 1)

// 2D Framebuffer Rendering
extern "C" int get_framebuffer_2d();

#define FRAMEBUFFER_2D() (get_framebuffer_2d() == 1) // Can get worse or better fps/visuals

// FMV Format Selection
extern "C" int get_fmv_format_preset();

#define FMV_FORMAT_CMPR()   (get_fmv_format_preset() == 0)
#define FMV_FORMAT_RGBA8()  (get_fmv_format_preset() == 1)
#define FMV_FORMAT_RGB565() (get_fmv_format_preset() == 2)

// Twiddled YUV422 texture decode fix. Only touches the TWIDDLED YUV422 source
// path (static YUV artwork); planar/scan-order sources — which is what the YUV
// converter produces for real FMV — are already correct and are left alone.
// See yuv_tw_fetch() for the layout and for what the legacy path got wrong.
extern "C" int get_yuv_twiddle_fix_preset();
#define YUV_TW_FIX() (get_yuv_twiddle_fix_preset() == 1)

// YUV422 source pitch: how the REAL per-row pixel pitch (and row count) of a
// YUV422 source is decided. It can be smaller than the declared power-of-two
// texture size. See the case-3 YUV branch in SetTextureParams.
//   0 = off    : always decode at the declared width (pre-"fmv fixed" behaviour)
//   1 = auto   : use the real size only for textures the YUV converter wrote
//   2 = always : take the size from TA_YUV_TEX_CTRL for every YUV422 texture
//                (the ec67c40 behaviour, kept for A/B: it blacks out games
//                 that leave that register unprogrammed)
extern "C" int get_yuv_stride_preset();
#define YUV_STRIDE_AUTO()   (get_yuv_stride_preset() == 1)
#define YUV_STRIDE_ALWAYS() (get_yuv_stride_preset() == 2)

// Blend mode: per-polygon TSP SrcInstr/DstInstr blending (necessary for Resident Evil 3)
extern "C" int get_blend_mode_preset();
#define BLEND_MODE() (get_blend_mode_preset() == 1)

// Force vertex alpha opaque for both ARGB1555 (fmt0) and RGB565 (fmt1).
// Off: only ARGB1555 (fmt0) is forced opaque; RGB565 (fmt1) uses vertex alpha as-is.
extern "C" int get_rgb565_opaque_alpha_preset();
#define RGB565_OPAQUE_ALPHA() (get_rgb565_opaque_alpha_preset() == 1)

// Blend FPS boost: under ADVANCED_ALPHA()+BLEND_MODE(), skip the alpha-test/
extern "C" int get_blend_fps_boost_preset();
#define BLEND_FPS_BOOST() (get_blend_fps_boost_preset() == 1)

// Translucent-list depth WRITE (see the g_trans_zwrite_preset note in
// wii/main.cpp). The TR range inherits the frame-wide
// GX_SetZMode(GX_TRUE, GX_GEQUAL, GX_TRUE), so the FIRST translucent strip
// submitted at a near depth stamps the Z buffer and depth-rejects every
// farther translucent strip behind it -- one near full-screen quad can hide
// the whole rest of the list. Real PVR2 blends the translucent list instead
// of using it as occluders. Depth TESTING is always kept, so opaque geometry
// drawn earlier still occludes translucent geometry behind it.
//
//   1 = on   legacy (default), bit-for-bit what the renderer did before this
//            preset existed.
//   0 = off  always drop the write, so translucent strips paint in
//            submission order and never occlude each other.
//
// DEBUG ONLY. `off` hides the Dreamcast BIOS boot logo, which needs the
// depth ordering this removes. It was written for Fighting Vipers 2's SEGA
// screen and turned out NOT to be the fix there either (that was
// sprite_color + vtx_alpha), so it survives only as an investigation tool
// for the next scene where translucent strips occlude each other wrongly.
//
// An AUTO mode keyed on ISP_FEED_CFG bit 0 (regs.h: "Translucent polygon
// sort mode") was tried and removed: nothing in the tree ever writes that
// register -- regs.cpp sets the reset value 0x00402000 and that is all -- so
// it read 0 in every scene and behaved identically to ON on hardware.
extern "C" int get_trans_zwrite_preset();
extern "C" const char* get_matched_preset_name(); // [LOGO] probe only
#define TRANS_NO_ZWRITE() (get_trans_zwrite_preset() == 0)

// Vertex alpha on ARGB1555 surfaces. The legacy rule rewrites vertex alpha to
// 0xFF for EVERY ARGB1555 polygon regardless of TSP.UseAlpha -- a deliberate
// hack for Test Drive 6's ARGB1555 cutout font, where games leave vertex
// alpha at 0 and rely on the TEXTURE alpha to gate visibility.
//
// It also destroys any game that FADES via vertex alpha on such a surface:
// ARGB1555 has only 1 bit of texture alpha, so a fade has to live in the
// 8-bit vertex/sprite-header alpha, and forcing that opaque pins the overlay
// permanently on screen. That is Fighting Vipers 2's SEGA screen, where a
// full-screen fade overlay covered everything behind it.
//
// 1 = honour TSP.UseAlpha and nothing else (the hardware's own rule).
// 0 = legacy (default): keep the Test Drive 6 hack, which other games need.
extern "C" int get_vtx_alpha_preset();
#define VTX_ALPHA_HONOR() (get_vtx_alpha_preset() == 1)

// Sprite base colour. A Sprite (PCW.ParaType 5) carries ONE packed Base
// Colour in its header that applies to all four corners -- sprites have no
// per-vertex colour. AppendSpriteVertexA hardcoded 0xFFFFFFFF and
// AppendSpriteParam read only OffsCol, so TA_SpriteParam::BaseCol was parsed
// and thrown away: every sprite in every game drew with WHITE vertex colour.
// With TSP.ShadInstr = ModulateAlpha (colour = texture x vertex) that turns a
// black sprite into a white one -- which is exactly Fighting Vipers 2's SEGA
// screen, where a full-screen black sprite came out as a white plate covering
// the whole display.
// Default OFF: this changes the colour of every sprite in every game.
extern "C" int get_sprite_color_preset();
#define SPRITE_COLOR() (get_sprite_color_preset() == 1)

// Punch-through list fix
extern "C" int get_punch_through_preset();
#define PUNCH_THROUGH_FIX() (get_punch_through_preset() == 1)

// hokuto_hack preset: layer-tiered translucent sort for games that submit
// their whole 2D scene at ONE depth. 
extern "C" int get_hokuto_hack_preset();
#define HOKUTO_HACK() (get_hokuto_hack_preset() == 1)

// hokuto_hack debug: flip to 1, rebuild, and play up to the next stage's
// battle to capture a [HOKUTO_STAGE] log the same way of stage 1 and 2
#define HOKUTO_DEBUG_LOG 0

// Multi-pass render debug ([SS] lines, see ss_dump_pass() just above
// StartRender). Flip to 1, rebuild, play up to the scene, capture, flip back
// to 0. Compile-time on purpose: the capture must not depend on getting a menu
// preset right, and it is a printf storm if left on. Dumps a burst of
// consecutive PVR render passes whenever a bit-24 (RTT) pass shows up, each
// with its FB_W_* / clip registers and a per-strip line (screen bbox, depth,
// texture, blend, user tile clip) — i.e. exactly which pass owns the scope
// view, the HUD, and the crosshair, and where each lands on screen. Also emits
// an unconditional [SS] alive heartbeat, so silence is distinguishable from
// "the logger never ran".
// Enabling it only ADDS the listTileClip[] store (normally split_screen-gated);
// nothing about the actual rendering changes, so the capture is honest.
// Off now that the Silent Scope capture is in and the pass structure is known
// (see RTT_KEEP_LIST below). Flip to 1 and rebuild to re-arm it for the next
// multi-pass game; nothing else needs changing.
#define SCOPE_DEBUG_LOG 0

// [RTT] probe: is the render-to-texture pass actually producing pixels? Prints
// one line per ~120 RTT passes with how many strips it drew/skipped, their
// screen bbox, the EFB source rect it lifted, how many of those pixels were
// non-zero, and the first word it wrote into emulated VRAM. It also poisons the
// grab buffer with 0xA5A5 first, so "poison=<all>" cleanly means GX_CopyTex
// never wrote RAM while "nonzero=0 poison=0" means the EFB really was black.
// Compile-time, not DEBUG_MESSAGE()-gated, on purpose: the per-strip bbox scan
// and the 64K-pixel sweep are far too expensive to leave in a shipping build,
// and at 0 none of it is compiled at all. Flip to 1 to re-arm.
//
// NOTE (Dolphin): with "Store EFB Copies to Texture Only" on — its default —
// Dolphin never writes EFB copies to emulated RAM, so every RTT read-back comes
// back empty no matter how correct this code is. Uncheck that and "Skip EFB
// Access from CPU" before concluding anything about render-to-texture there.
#define RTT_DEBUG_LOG 0

// ── [LOGO] boot/2D-screen probe ────────────────────────────────────────────
// Built for the "SEGA logo screen is solid white" class of bug (first seen in
// Fighting Vipers 2): the audio plays, the render pass runs, but every pixel
// comes out white instead of a black plate with one textured logo quad on it.
//
// Four different faults produce that, and the screen alone cannot tell them
// apart. One capture answers all four:
//
//   1. Does the pass reach the 3D path at all, or is the screen a 2D
//      framebuffer blit / an RTT pass / a skipped frame?
//        -> [LOGO] pass= header, plus the [PATH] lines (armed with this probe)
//   2. Is the BACKGROUND white? The BG plane's v0 colour goes straight to
//      GX_SetCopyClear, so it paints the whole screen wherever nothing else
//      covers it -- a bad decode there IS a white screen, with or without a
//      correct logo drawn on top of it.
//        -> [LOGO] bg lines
//   3. Is the logo strip submitted, and where does it land? A quad that should
//      be a small logo but spans 0..640 x 0..480 explains the white on its
//      own, and so does a UV range that collapsed onto a single texel (which
//      is the failure mode to expect from a broken 16-bit UV decode, and this
//      polygon has uv_16bit set).
//        -> [LOGO] #nnn (submitted) / draw#nnn (what GX was actually told)
//   4. Is the TEXTURE the logo, or a white/blank/garbage block?
//        -> [LOGO] tex= line + ASCII thumbnail, sampled straight out of
//           emulated VRAM with the same twiddle/mip/endian rules the real
//           decoder uses, so it is independent of the texture cache and of
//           whichever tex_cache preset is selected.
//
// Compile-time on purpose, exactly like SCOPE_DEBUG_LOG: the capture must not
// depend on getting a menu preset right, and the per-strip vertex scans plus
// the texture sampling are far too expensive to leave in a shipping build.
// Flip back to 0 and rebuild once the capture has been read.
//
// Output lands in /ndclog.txt on the SD card (InitRenderer freopens stdout
// there) -- nothing appears on screen. Every dump ends in fflush, so powering
// the console off on the stuck screen still leaves a readable tail.
#define LOGO_DEBUG_LOG 0

// Schedule, counted in RENDER PASSES rather than frames: a scene that stalls
// or frame-skips stops advancing FrameCount, and a frame-clocked probe would
// go quiet exactly where it is needed. Passes always tick.
#define LOGO_HEAD_PASSES 24   // dump every one of the first N passes (boot)
#define LOGO_EVERY       30   // then one pass in every N ...
#define LOGO_LAST_PASS   1500 // ... up to here (~25 s at 60 Hz), which brackets
                              //     the SEGA screen of any game
#define LOGO_HEARTBEAT   300  // proof-of-life line, always, even while quiet
#define LOGO_MAX_STRIPS  48   // per-strip lines per dumped pass
#define LOGO_MAX_TEX     6    // texture thumbnails per dumped pass (the
                              // expensive part: COLS*ROWS VRAM samples each)
#define LOGO_TEX_COLS    48
#define LOGO_TEX_ROWS    20

// VRAM address of the texture being hunted (0 = no signature trigger).
// Any pass whose parameter list references it is dumped IN FULL whatever
// the schedule above says, and its strip + thumbnail are printed even past
// the per-pass caps -- so the capture does not depend on the sampling
// happening to land on the right frame, and does not go missing because
// the scene submitted fifty strips ahead of the interesting one.
//
// 0x036500 is the SEGA logo in Fighting Vipers 2 (512x512 ARGB1555, the
// polygon a PVR viewer reports as isp=82400000 tsp=949004F6). Change it to
// whatever the viewer reports for the next screen under investigation.
#define LOGO_TRIGGER_TEX 0x036500

#if LOGO_DEBUG_LOG
// Set by logo_dump_pass() at the top of StartRender and read by the probes
// further down the frame (render-path lines, BG plane, per-strip GX state), so
// a pass is either fully logged or fully silent -- never half of each.
static bool s_logo_dump_now = false;
// (plain unsigned rather than u32: this block sits above the typedefs)
static unsigned s_logo_draw_n = 0; // per-pass counter for the draw-time lines
#define LOGO_ARMED() (s_logo_dump_now)
#else
#define LOGO_ARMED() (false)
#endif

// Where the [SS] output goes. InitRenderer() normally does
// freopen("/ndclog.txt", "w", stdout) the moment the renderer starts, so EVERY
// printf from gameplay onward disappears into a file on the SD card — which is
// why nothing shows up in Dolphin's log window during the scene: the messages
// you can see are the ones printed BEFORE the renderer inits (menu, presets).
//   1 = skip that redirect while SCOPE_DEBUG_LOG is on, leaving stdout where
//       libogc put it. Dolphin then shows [SS] live, which is how you test.
//   0 = keep the redirect; read /ndclog.txt off the SD card afterwards.
#define SCOPE_LOG_TO_CONSOLE 1

// Offset (specular) color
extern "C" int get_offset_color_preset();
#define OFFSET_COLOR_FIX() (get_offset_color_preset() == 1)

// 2D sprite seam fix — half-texel UV inset so GX_LINEAR filtering never samples
// past a sprite's own edge texels (the wrap/neighbour bleed that shows up as thin
// black "seam" lines between 2D tiles). GX_NEAR hides it for free by not
// interpolating; this keeps LINEAR. Applied per strip on the CPU by shrinking the
// strip's actual UV rectangle inward half a texel on every side — so it works for
// full-texture sprites (UV 0..1) AND atlas sub-rects, unlike a global tex matrix.
extern "C" int get_seam_fix_preset();
#define SEAM_FIX() (get_seam_fix_preset() == 1)

// ── FOG() ─────────────────────────────────────────────────────────────────
// PVR2 fog. Real hardware fogs every pixel in the TSP stage, before the
// blending unit, and each polygon picks its own mode through TSP.FogCtrl:
//   0 = Look Up Table   fog coefficient = FOG_TABLE[] sampled with the pixel's
//                       W scaled by FOG_DENSITY; colour = FOG_COL_RAM
//   1 = Per Vertex      fog coefficient = the vertex's OFFSET COLOUR ALPHA
//                       (the game computes the ramp itself); colour = FOG_COL_VERT
//   2 = No Fog          polygon is not fogged
//   3 = LUT Mode 2      the LUT coefficient becomes the pixel's ALPHA and the
//                       pixel's colour is replaced by FOG_COL_RAM outright
//                       ("fog as a blended layer", rare)
// None of that existed here: FogCtrl was decoded into the TSP struct and never
// read, so foggy games rendered with hard, un-hazed geometry and horizons that
// pop in (racers and anything outdoors).
//
// GX has a fog unit, but it derives its coefficient from the fragment's screen
// Z through a fixed linear/exponential curve, which cannot express an arbitrary
// 128-entry PVR table, and this renderer's projection is rebuilt every frame
// from the scene's own W range (see vtx_min_Z/vtx_max_Z) so its start/end
// planes would have to be re-derived per frame on top of that. So fog is
// evaluated per VERTEX on the CPU instead — inside the strip walk that is
// already submitting the vertices — and the coefficient rides into the pipeline
// in the ALPHA of the CLR1 vertex attribute, which is exactly where real PVR
// per-vertex fog keeps it. One extra TEV stage then performs the blend the TSP
// would have done:  colour = lerp(stage_result, fog_colour, CLR1.alpha).
// The ramp is Gouraud-interpolated across the strip rather than evaluated per
// pixel; for the smooth distance ramps fog is actually used for that is
// visually the same thing (mode 1 IS per-vertex on hardware too) and it costs
// no fill rate.
//
// Cost: +4 bytes/vertex (the CLR1 attribute is forced on even when
// OFFSET_COLOR_FIX() is off), one extra TEV stage on fogged polygons only, and
// a table lookup per vertex of a LUT-fogged strip (integer, ~10 ops, no libm).
// Polygons with FogCtrl==2 fall back to the exact legacy stage count.
//
// Off by default: it changes the colour of every fogged polygon in the scene,
// so it is a per-game opt-in like every other renderer preset here.
extern "C" int get_fog_preset();
#define FOG() (get_fog_preset() == 1)

// Translucent depth sort
extern "C" int get_trans_sort_preset();
#define TRANS_SORT() (get_trans_sort_preset() == 1)

// autosort preset: REAL per-pixel PVR autosort (order-independent
// transparency) via GX depth peeling. Value = max translucent depth LAYERS
// peeled per frame: 0 = off (legacy), 1..4 = on. Unlike TRANS_SORT() (a
// per-STRIP painter sort, wrong wherever strips interleave in depth or
// intersect), this reproduces what the PVR ISP actually does: each pixel
// composites its translucent fragments strictly back-to-front, regardless
// of submission order.
//
// GX has no programmable shaders, so each layer costs a full extra walk of
// the TR list (see the AUTOSORT machinery above DoRender):
//   select pass  color writes off, Z func LESS over a near-initialized Z
//                buffer -> Z ends holding the farthest not-yet-peeled
//                fragment per pixel. "Not yet peeled" is enforced by a TEV
//                16-bit depth compare (GX_TEV_COMP_GR16_GT) between the
//                fragment's own screen depth (projective texgen through a
//                1024-texel ramp texture) and a Z snapshot of the previous
//                peel (two 8-bit EFB Z copies, bits 23:16 + 15:8), with an
//                alpha-test kill. Peel 0 compares >= against the OPAQUE
//                depth instead, so fragments behind opaque geometry never
//                consume a layer (and coplanar decals still pass).
//   draw pass    normal textured/blended walk with Z func EQUAL, write off:
//                only the selected layer's fragments rasterize (identical
//                geometry rasterizes to identical Z), each with its own
//                TSP blend mode -- correct even for mixed blend modes,
//                which no front-to-back scheme can do on this hardware.
// Co-planar fragments (equal 24-bit Z) pass EQUAL together and blend in
// submission order, matching the painter tie-break real games expect.
//
// Costs and limits:
//   * ~2N walks of the TR geometry + 2 EFB Z copies per layer: heavy. Use
//     small N (2-3) and only in games that really need per-pixel sorting.
//   * Fragment depth is quantized to the 1024-texel ramp (about 10 bits of
//     the Z range): layers closer than 1/1024 of the depth range to the
//     PREVIOUS peel are dropped, not re-sorted (biased-down ramp; dropping
//     beats infinite re-selection). 2D stacks at EXACTLY equal depth are
//     safe (drawn together via EQUAL); "almost equal" sub-epsilon stacks
//     belong to trans_sort/hokuto_hack instead.
//   * Layers beyond N per pixel are dropped (farthest N win).
//   * Needs the Z24 EFB (rmode->aa off) and MEM2 buffers (~700 KB).
//   * RTT frames keep the legacy path. hokuto_hack overrides this preset
//     (those games stack everything at ONE depth, peeling can't help).
//     Recommended companions: punch_through=on (PT occludes TR correctly),
//     depth_clip=1 (default; keeps the near-parked Z init off-plane).
extern "C" int get_autosort_preset();
#define AUTOSORT() (get_autosort_preset())

// Render-to-texture. May behave differently depending on FRAMEBUFFER_2D()
//
//   0 = off     legacy. A bit-24 (RTT) render is dropped, but its geometry is
//               left sitting in the vertices/lists buffers, so the NEXT display
//               render draws it too — an accident, not a feature (see the
//               RTT_CARRY_OVERLAY() note below for why it misbehaves).
//   1 = on      real RTT: render the pass at the target size and copy the EFB
//               back into VRAM at FB_W_SOF1 so the game samples it as a texture.
//   2 = overlay the pass is still not rendered to a texture, but its geometry is
//               deliberately carried into the next display frame and drawn LAST
//               as a flat overlay. This is the mode-0 accident made correct —
//               see RTT_CARRY_OVERLAY().
//   3 = keep    real RTT like 1, but the render does NOT consume the TA list —
//               see RTT_KEEP_LIST().
extern "C" int get_render_to_texture_preset();
#define RENDER_TO_TEXTURE() (get_render_to_texture_preset() == 1 || \
                             get_render_to_texture_preset() == 3)

// render_to_texture=3 ("keep list"): on real hardware a render does NOT consume
// the TA list. The ISP/TSP walk the tile arrays that the TA built; those stay
// valid until TA_LIST_INIT. So a game can render ONE accumulated list twice
// with different FB_W_SOF1 + FB_X_CLIP/FB_Y_CLIP — first into a texture, then
// to the display — and the second render legitimately draws everything,
// including the geometry the first one already consumed.
//
// Silent Scope's sniper scope is built exactly this way, and the [SS] capture
// spells it out. One frame is:
//   * ~477 strips submitted: translucent scene extras, then the scope disc
//     (22 triangle wedges fanning out from screen centre 320,240 at W=1.6,
//     textured from 554000 — the RTT target itself, i.e. last frame's image),
//     then the 24x-magnified world confined by a mode-2 User Tile Clip to
//     tiles 0..8 (the 288x288 corner the 256x256 texture is lifted from);
//   * RENDER_START with FB_W_SOF1=01554000, XCLIP/YCLIP 0..255 -> the magnified
//     world lands in the scope texture;
//   * ~864 MORE strips submitted: the ordinary 1x world;
//   * RENDER_START with FB_W_SOF1=00200000, XCLIP 0..639 -> the WHOLE list
//     (both halves) goes to the screen: disc first at W=1.6, magnified corner
//     next, normal world last and farther, so the disc survives the GEQUAL
//     painter compare and the corner is overdrawn.
// Resetting the vertex buffers after the RTT pass loses the first half, so the
// disc never reaches the display — the exact "global view, no crosshair" that
// render_to_texture=on produced.
//
// The two renders are NOT meant to draw the same thing, and the game says which
// is which: the half destined for the texture carries a mode-2 (INSIDE) user
// tile clip confining it to the target window; the on-screen half (HUD, reticle,
// the disc itself) carries none. So this mode also SPLITS the shared list on
// that flag — the RTT render draws only the tile-clipped strips, the display
// render draws everything except the tile-clipped ones it already consumed.
// Without the split the magnified half lands in the screen's top-left corner and
// Z-fights the real scene (same W values, different XY — unwinnable), which is
// precisely what the tile clip was there to prevent.
//
// Kept separate from mode 1 because a game that DOES re-init the TA between its
// RTT pass and its display pass relies on the reset; mode 1 is untouched.
// split_screen is NOT needed here: this mode reads listTileClip[] itself.
#define RTT_KEEP_LIST() (get_render_to_texture_preset() == 3)

// render_to_texture=2 (overlay / "carry"): for a game whose scope, mirror or
// picture-in-picture pass we cannot render to a texture, draw that pass's
// geometry on top of the next display frame instead of silently leaking it.
//
// Mode 0 already leaks it, which is why some games (Silent Scope's sniper
// crosshair) show *something* with RTT off — but the leak is broken in three
// ways, all of which this mode fixes:
//   * The next pass's StartList() overwrites TransLST/PTLST/TransVTX/..., so
//     the carried pass's TR and PT strips are re-classified as opaque and drawn
//     first, in the wrong blend state. Here the carried boundaries are snapshot
//     at the drop and replayed on their own segment.
//   * The carried geometry is drawn BEFORE the scene, with Z-write on, so the
//     scene overdraws whatever does not win the GEQUAL painter compare — the
//     "crosshair survives, its black surround does not" symptom. Here it is
//     drawn last, re-parked onto the near plane, GX_ALWAYS + no Z-write.
//   * Its vertices feed the frame's dynamic vtx_min_Z / vtx_max_Z tracking, so
//     a scope pass at a completely different depth compresses the scene's whole
//     Z range. Here the range is reset at the drop, so the display frame tracks
//     only its own geometry.
#define RTT_CARRY_OVERLAY() (get_render_to_texture_preset() == 2)

// Hardware-like HOLLY IRQ delays (list-complete + staggered render-done, see SPG.cpp)
extern "C" int get_render_delay_preset();
#define RENDER_DELAY() (get_render_delay_preset() != 0)

// Split-screen viewport support (Slower)
extern "C" int get_split_screen_preset();
#define SPLIT_SCREEN() (get_split_screen_preset() == 1)

// GX mipmap generation
extern "C" int get_mipmap_preset();
#define MIPMAP_OFF()       (get_mipmap_preset() == 0) // Fast
#define MIPMAP_FAST()      (get_mipmap_preset() == 1) // Slow
#define MIPMAP_TRILINEAR() (get_mipmap_preset() == 2) // Slower

// 1. FIXED DEPTH: Use FIXED_DEPTH_TIGHT (2) for fine Z-precision.
//    Paired with HUD_PASS (1 or 2), this completely eliminates 3D Z-fighting
//    without clipping the UI.
extern "C" int get_fixed_depth_preset();
#define FIXED_DEPTH_OFF()   (get_fixed_depth_preset() == 0) // legacy dynamic range tracking
#define FIXED_DEPTH_WIDE()  (get_fixed_depth_preset() == 1) // fixed [0.0001 .. 100000] (safe, coarse Z)
#define FIXED_DEPTH_TIGHT() (get_fixed_depth_preset() == 2) // fixed [0.1 .. 25000] (finer Z, extremes clip)

// 1b. LEGACY DEPTH: reproduce the depth pipeline exactly as it stood at commit
//     1bb8c27 ("Optimize vertex processing with fixed-depth projection"), the
//     last state where Buggy Heat rendered its intro logo, its VMU screen and
//     gameplay all correctly. Three things differ from today and all three are
//     restored together, because that combination is what was verified - not
//     any one of them in isolation:
//       1. fixed planes NEAR=0.001 / FAR=10000 (FAR extended by 1.001)
//       2. vert_base clamps 1/W at 0.001f, not 0.0001f (see g_vert_z_clamp)
//       3. no per-vertex min/max tracking anywhere, and none of the newer
//          DEPTH_CLIP_MARGIN / HUD_PASS vertex fixups, which did not exist yet
//     Overrides FIXED_DEPTH_*(). Not a superset of FIXED_DEPTH_WIDE/TIGHT: the
//     clamp change (2) is what those presets cannot express, and it is why
//     fixed_depth alone never reproduced 1bb8c27.
extern "C" int get_legacy_depth_preset();
#define LEGACY_DEPTH() (get_legacy_depth_preset() == 1)

// 2. DEPTH CLIP: Use DEPTH_CLIP_MARGIN (1) to prevent near-plane clipping
//    or DEPTH_CLIP_NOCLIP (2) if running on Dolphin.
extern "C" int get_depth_clip_preset();
#define DEPTH_CLIP_OFF()    (get_depth_clip_preset() == 0) // legacy: XF clipping on, no near margin
#define DEPTH_CLIP_MARGIN() (get_depth_clip_preset() == 1) // pad vtx_min_Z by 0.1% so the nearest layer can't sit exactly on the near plane
#define DEPTH_CLIP_NOCLIP() (get_depth_clip_preset() == 2) // GX_SetClipMode(GX_CLIP_DISABLE): behave like Dolphin, out-of-range Z clamps instead of clipping

// 3. HUD PASS: Re-parks near-clipped HUD geometry back onto the near plane.
//    Mode 1 (OVERLAY) = GX_ALWAYS, Z-write OFF (Safest default).
//    Mode 2 (PROTECT) = GX_ALWAYS, Z-write ON (For HUDs being drawn over).
extern "C" int get_hud_pass_preset();
#define HUD_PASS() (get_hud_pass_preset())

// 4. ISP DEPTH FUNC: Set to 1 (Honor DepthMode on Opaque/PT lists) so 3D objects 
//    respect hardware Z-tests while Translucent objects fall back to PVR autosort.
extern "C" int get_isp_depth_func_preset();
#define ISP_DEPTH_FUNC() (get_isp_depth_func_preset())

// 5. PER-POLYGON Z-WRITE: Forces depth updates on depth-tested polygons.
extern "C" int get_isp_cull_preset();
#define ISP_CULL() (get_isp_cull_preset())

// Async render: don't block the CPU in GX_DrawDone() at the end of a 3D display
// frame. The frame is queued with GX_SetDrawDone() and the wait + VIDEO flip
// are deferred to the next render/present entry (gx_sync_pending), so the SH4
// core emulates the next frame while the GPU draws this one. Costs one frame
// of display latency; RTT passes stay fully synchronous (the game reads the
// result back immediately).
extern "C" int get_async_render_preset();
#define ASYNC_RENDER() (get_async_render_preset() == 1)

// TMEM cache: skip the unconditional per-frame GX_InvalidateTexAll() in
// DoRender and instead invalidate only when a texture is actually (re)decoded
// (see SetTextureParams) — unchanged textures then stay resident in the GPU's
// 1MB TMEM cache across frames instead of being re-fetched from main RAM
// every frame, freeing texture fill rate.
extern "C" int get_tmem_cache_preset();
#define TMEM_CACHE() (get_tmem_cache_preset() == 1)

// Background polygon (ISP_BACKGND_T) rendering: barycentrically extrapolate
// the 3 background vertices into a full-screen quad (see DoRender) instead of
// only using v0's color for the EFB clear. Needed for games whose background
// carries real gradient/texture data (Who Wants to Be a Millionaire), but the
// extra per-frame texture bind + polygon draw costs FPS in games that don't
// need it — off by default until enabled per-game.
extern "C" int get_bg_poly_preset();
#define BG_POLY_FIX() (get_bg_poly_preset() == 1)

// Forced canvas width for 240p modes. Some low-res games (Street Fighter
// III: 384, the CPS3 arcade width) draw their scene narrower than 640 and
// pad the rest of the framebuffer with filler polygons, so neither any PVR
// register (glob tile clip and FB_R_SIZE both read 640) nor measuring the
// submitted geometry reveals the real scene width. This forces it per-game;
// applied only in non-interlaced NTSC/PAL (240p) video modes so interlaced
// boot/logo screens keep the normal 640 canvas. 0 = off (legacy).
extern "C" int get_canvas_width_preset();
#define CANVAS_WIDTH() (get_canvas_width_preset())

// PVR vertical (Y) scaler - SCALER_CTL.vscalefactor, bits 15-0, the "Y" half of
// the "X & Y scaler control" register whose hscale bit X_SCALER() handles over
// in SPG.cpp. 6.10 fixed point, 0x400 = 1.0. A factor above 1.0 means the CORE
// renders the scene that many times TALLER and the video scaler shrinks it back
// down on framebuffer write (vertical SSAA / flicker filter); below 1.0 it
// renders shorter and the scaler stretches. Either way the projected canvas
// height must follow or only a slice of the scene reaches the screen - the
// vertical twin of the Omicron / Wacky Races "left half only" bug. 0 = off
// (legacy: the factor is ignored and the canvas stays at the video-mode
// height).
extern "C" int get_y_scaler_preset();
#define Y_SCALER() (get_y_scaler_preset() == 1)

// PVR horizontal (H) scaler at video output - VO_CONTROL.pixel_double, bit 8.
// Set by the low-res video modes: the framebuffer holds a HALF-width (320)
// image and the video DAC emits every pixel twice to fill the 640-pixel line,
// so the scene is submitted in a 320-wide screen space. With the bit ignored
// that scene is projected into a 640 canvas and fills only its left half. This
// is the register-driven counterpart of CANVAS_WIDTH() above (which exists for
// games like SF3 that render narrow WITHOUT setting the bit), so an explicit
// forced canvas width wins over it. 0 = off (legacy: bit ignored).
extern "C" int get_h_scaler_preset();
#define H_SCALER() (get_h_scaler_preset() == 1)

// 6. NATIVE POLYGON OFFSET (GX depth bias). Real hardware note: GX has no
//    glPolygonOffset-style register — no programmable shaders means there is
//    no way to nudge a fragment's post-transform Z from the pixel pipeline.
//    The real-hardware equivalent is a Z-TEXTURE in ADD mode (GX_SetZTexture):
//    every fragment drawn while it's bound gets a constant bias added to its
//    depth before the Z test/write, which is exactly a slope-independent
//    "units" depth bias. Applied to the Punch-Through (PT) list, since that's
//    where the Dreamcast pipes co-planar decals / shadows / road-markings
//    that PVR's tile order used to just sort "for free". 0 = off (legacy).
//    1..N = bias strength tier, see s_poly_offset_bias_w[] in DoRender().
extern "C" int get_poly_offset_preset();
#define POLY_OFFSET() (get_poly_offset_preset()) // 0, 1 2 or 3

// 7. REVERSED DEPTH MAPPING — NOTE: already the case in this renderer.
//    The projection built in DoRender() (p5/p6, see the algorithm comment
//    above the Mtx44 mtx) maps W=vtx_min_Z (near) -> viewport z 1.0 and
//    W=vtx_max_Z (far) -> viewport z 0.0 (see as_setup_frame()'s "0=far
//    1=near" note), paired with GX_SetZMode(..., GX_GEQUAL, ...) — i.e. the
//    24-bit integer Z buffer already spends most of its precision near the
//    camera, the way the PVR's 32-bit float W-buffer does natively. There is
//    no separate toggle added for this; kept here only so the numbered list
//    matches the mitigation write-up this file gets compared against.

// 8. SUB-PASS DEPTH-ONLY CLEAR: re-park the whole Z buffer at a given W
//    (near or far) WITHOUT touching color, so a geometry group drawn right
//    after (e.g. HUD_PASS() PROTECT-mode 3D overlays) starts from a known,
//    clean depth baseline instead of whatever the main scene left behind.
//    GX has no partial/depth-only EFB clear outside of a real copy-out, so
//    this is implemented the same way AUTOSORT() already parks the Z buffer:
//    a full-canvas quad, color/alpha writes off, Z func ALWAYS, Z write on.
//    0 = off (legacy, single shared depth pass — the current default).
extern "C" int get_subpass_zclear_preset();
#define SUBPASS_ZCLEAR() (get_subpass_zclear_preset() == 1)


int frame_counter;

#include "config.h"
#include "gxRend.h"
#include <gccore.h>
#include <ogc/conf.h> // CONF_GetAspectRatio() for RATIO_AUTO()
#include <malloc.h>
#include <stdlib.h> // qsort, for TRANS_SORT()
#include "regs.h"
#include "wii/wii_audio.h"
#include <stdio.h> // needed for log

// The FIFO is the command buffer for the GX hardware. 
// 256KB is a standard size for most homebrew applications. May need more for NullDC4Wii to avoid FIFO error ?
#define DEFAULT_FIFO_SIZE (256 * 1024)

// Regions the YUV converter has actually written - dc/pvr/pvr_if.cpp.
// Declared here rather than with the preset block at the top of the file:
// that block sits above every #include, so u32 does not exist yet up there.
extern "C" int yuv_conv_lookup(u32 addr, u32 *out_w, u32 *out_h);

using namespace TASplitter;

u8 *vram_buffer;

// CACHE_VERY_FAST_PLUS overflow arena, handed to us by _vmem_reserve(). PLUS
// keeps VERY_FAST's address-derived slots, which need every byte of
// vram_buffer, so the arena for textures that cannot fit such a slot lives in
// its own MEM2 block. Null when MEM2 was too tight to spare one — PLUS then
// uses the slot for everything, i.e. it behaves exactly like VERY_FAST.
u8 *plus_tex_arena = 0;
u32 plus_tex_arena_size = 0;

// Double buffering setup: frameBuffer[0] and [1] prevent screen tearing.
static void *frameBuffer[2] = {NULL, NULL};
static GXRModeObj *rmode;
static u8 gp_fifo[DEFAULT_FIFO_SIZE] __attribute__((aligned(32)));
static int fb = 0; // Current framebuffer index

// ============================================================================
// SOFTWARE XFB FPS TEXT RENDERER
// ============================================================================

extern char fps_text[512];
extern "C" int get_show_fps_overlay();

// 5x7 bitmap font used by the software FPS overlay.
static u8 GetXfbFontRow(char character, int row)
{
  if (row < 0 || row >= 7)
    return 0;

  if (character >= 'a' && character <= 'z')
    character = character - 'a' + 'A';

  static const u8 letters[26][7] =
  {
    {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // A
    {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}, // B
    {0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}, // C
    {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}, // D
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}, // E
    {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}, // F
    {0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F}, // G
    {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}, // H
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}, // I
    {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0E}, // J
    {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}, // K
    {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}, // L
    {0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}, // M
    {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}, // N
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // O
    {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}, // P
    {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}, // Q
    {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}, // R
    {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}, // S
    {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}, // T
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}, // U
    {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}, // V
    {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}, // W
    {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}, // X
    {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}, // Y
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}  // Z
  };

  static const u8 numbers[10][7] =
  {
    {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}, // 0
    {0x04, 0x0C, 0x14, 0x04, 0x04, 0x04, 0x1F}, // 1
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}, // 2
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E}, // 3
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}, // 4
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E}, // 5
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E}, // 6
    {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}, // 7
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}, // 8
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E}  // 9
  };

  if (character >= 'A' && character <= 'Z')
    return letters[character - 'A'][row];

  if (character >= '0' && character <= '9')
    return numbers[character - '0'][row];

  switch (character)
  {
    case ' ':
      return 0x00;

    case ':':
    {
      static const u8 glyph[7] =
      {
        0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00
      };
      return glyph[row];
    }

    case '.':
    {
      static const u8 glyph[7] =
      {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C
      };
      return glyph[row];
    }

    case '-':
    {
      static const u8 glyph[7] =
      {
        0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00
      };
      return glyph[row];
    }

    case '/':
    {
      static const u8 glyph[7] =
      {
        0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10
      };
      return glyph[row];
    }

    case '%':
    {
      static const u8 glyph[7] =
      {
        0x19, 0x19, 0x02, 0x04, 0x08, 0x13, 0x13
      };
      return glyph[row];
    }

    case '(':
    {
      static const u8 glyph[7] =
      {
        0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02
      };
      return glyph[row];
    }

    case ')':
    {
      static const u8 glyph[7] =
      {
        0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08
      };
      return glyph[row];
    }

    case '=':
    {
      static const u8 glyph[7] =
      {
        0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00, 0x00
      };
      return glyph[row];
    }

    default:
      return 0x00;
  }
}


// Writes one pair of horizontal pixels into the Wii framebuffer.
static void DrawXfbPixelPair(
  u32 *pixels,
  u32 framebuffer_width,
  u32 framebuffer_height,
  int x,
  int y,
  u32 color)
{
  if (pixels == NULL)
    return;

  if (x < 0 || y < 0)
    return;

  if ((u32)x >= framebuffer_width)
    return;

  if ((u32)y >= framebuffer_height)
    return;

  x &= ~1;

  const u32 words_per_row = framebuffer_width / 2;
  const u32 word_x = (u32)x / 2;

  if (word_x >= words_per_row)
    return;

  pixels[((u32)y * words_per_row) + word_x] = color;
}


// Draws one 5x7 character into the framebuffer.
static void DrawXfbCharacter(
  u32 *pixels,
  u32 framebuffer_width,
  u32 framebuffer_height,
  int start_x,
  int start_y,
  char character,
  u32 color)
{
  for (int row = 0; row < 7; row++)
  {
    const u8 row_bits = GetXfbFontRow(character, row);

    for (int column = 0; column < 5; column++)
    {
      const u8 bit_mask = (u8)(1 << (4 - column));

      if ((row_bits & bit_mask) == 0)
        continue;

      const int pixel_x = start_x + (column * 2);
      const int pixel_y = start_y + (row * 2);

      DrawXfbPixelPair(
        pixels,
        framebuffer_width,
        framebuffer_height,
        pixel_x,
        pixel_y,
        color);

      DrawXfbPixelPair(
        pixels,
        framebuffer_width,
        framebuffer_height,
        pixel_x,
        pixel_y + 1,
        color);
    }
  }
}


// Draws a complete text string into the framebuffer.
static void DrawXfbString(
  void *xfb,
  int start_x,
  int start_y,
  const char *text)
{
  if (xfb == NULL || rmode == NULL || text == NULL)
    return;

  u32 *pixels = (u32 *)xfb;

  const u32 framebuffer_width = rmode->fbWidth;
  const u32 framebuffer_height = rmode->xfbHeight;

  const u32 white_pair = 0xEB80EB80;
  const u32 black_pair = 0x10801080;

  const int character_advance = 12;
  const int line_advance = 18;

  int draw_x = start_x;
  int draw_y = start_y;

  for (int index = 0; index < 511; index++)
  {
    const char character = text[index];

    if (character == '\0')
      break;

    if (character == '\n')
    {
      draw_x = start_x;
      draw_y += line_advance;
      continue;
    }

    if (draw_x + 10 >= (int)framebuffer_width)
    {
      draw_x = start_x;
      draw_y += line_advance;
    }

    if (draw_y + 14 >= (int)framebuffer_height)
      break;

    DrawXfbCharacter(
      pixels,
      framebuffer_width,
      framebuffer_height,
      draw_x + 2,
      draw_y + 2,
      character,
      black_pair);

    DrawXfbCharacter(
      pixels,
      framebuffer_width,
      framebuffer_height,
      draw_x,
      draw_y,
      character,
      white_pair);

    draw_x += character_advance;
  }
}


// Draws the current live FPS string.
static void DrawXfbFpsText(void *xfb)
{
  if (xfb == NULL)
    return;

  if (!get_show_fps_overlay())
    return;

  if (fps_text[0] == '\0')
    return;

  DrawXfbString(
    xfb,
    20,
    20,
    fps_text);
}

// ── ASYNC_RENDER() state ─────────────────────────────────────────────────────
// s_gx_pending is true while a queued 3D frame (GX_SetDrawDone after its
// GX_CopyDisp) has not been waited on / presented yet. gx_sync_pending() must
// run before the CPU touches anything the GPU may still be reading — i.e. at
// the top of DoRender() (texture bump slots get re-decoded in place) and at
// the entry of every other present path, so the deferred VIDEO flip is applied
// before that path programs its own.
static bool s_gx_pending = false;
static int  s_gx_pending_fb = 0;

// Set from the GX draw-done interrupt (callback registered in InitRenderer).
// Lets VBlank() apply the deferred flip without blocking when the GPU already
// finished — closes the "game renders once then idles" case where the last
// queued frame would otherwise never reach the screen.
static volatile bool s_gx_done_irq = false;

static void gx_drawdone_cb(void)
{
  s_gx_done_irq = true;
}

static void gx_sync_pending()
{
  if (!s_gx_pending)
    return;
  GX_WaitDrawDone(); // GPU finished the queued frame's draws + display copy

  DrawXfbFpsText(frameBuffer[s_gx_pending_fb]);

  VIDEO_SetNextFramebuffer(frameBuffer[s_gx_pending_fb]);
  VIDEO_Flush();
  s_gx_pending = false;
}

// Non-blocking flavor, called every vblank: apply the queued frame's flip only
// if the GPU already signalled draw-done; otherwise leave it deferred (the
// next gx_sync_pending() or vblank picks it up).
static void gx_present_if_done()
{
  if (s_gx_pending && s_gx_done_irq)
  {
    DrawXfbFpsText(frameBuffer[s_gx_pending_fb]);

    VIDEO_SetNextFramebuffer(frameBuffer[s_gx_pending_fb]);
    VIDEO_Flush();
    s_gx_pending = false;
  }
}

// Set once at startup or when preset changes
static u8 min_filt, mag_filt, bias_clamp, edge_lod, aniso;
static f32 lod_bias;

// LOD bias steps offered by the menu / lod_bias= preset key, in menu order.
// Index 3 (0.0f) is the hardware default and a true no-op.
static const f32 s_lod_bias_steps[5] = { -1.0f, -0.75f, -0.5f, 0.0f, 0.5f };

// Anisotropy steps offered by the menu / aniso= preset key, in menu order
// (0X / 2X / 4X). Hollywood's TX unit only implements GX_ANISO_1/2/4, so 4X is
// the end of the scale - there is no 8X mode on this GPU to expose.
static const u8 s_aniso_steps[3] = { GX_ANISO_1, GX_ANISO_2, GX_ANISO_4 };

void ApplyGraphismPreset() {
  // GRAPHICS: texture filter only. LOW = GX_NEAR (point sampling, the right
  // choice for 240p games whose art is already at output resolution),
  // NORMAL = GX_LINEAR (bilinear).
  if (LOW()) { min_filt = GX_NEAR;   mag_filt = GX_NEAR;   }
  else       { min_filt = GX_LINEAR; mag_filt = GX_LINEAR; }

  // LOD BIAS: added to the computed LOD before the mip/filter decision.
  // Negative sharpens (samples a larger level than the footprint asks for),
  // positive blurs.
  int lod_idx = get_lod_bias_preset();
  if (lod_idx < 0 || lod_idx > 4) lod_idx = 3; // 0.0f
  lod_bias = s_lod_bias_steps[lod_idx];

  // ANISOTROPIC: extra TX filter cycles per texel (one per level of aniso).
  int aniso_idx = get_aniso_preset();
  if (aniso_idx < 0 || aniso_idx > 2) aniso_idx = 0; // GX_ANISO_1 = off
  aniso = s_aniso_steps[aniso_idx];

  // GX: biasclamp + edgelod, the two GX_DISABLE/GX_ENABLE arguments of
  // GX_InitTexObjLOD(). libogc requires edgelod whenever biasclamp is set OR
  // maxaniso is GX_ANISO_2/GX_ANISO_4, so aniso forces edgelod on by itself -
  // that is a hardware requirement, not an override of the user's GX choice
  // (which still owns biasclamp).
  bias_clamp = GX_LOD_EXTRAS() ? GX_ENABLE : GX_DISABLE;
  edge_lod   = (GX_LOD_EXTRAS() || aniso != GX_ANISO_1) ? GX_ENABLE : GX_DISABLE;
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

// May need that later ?
// #define ABGR1555_VQ(x) (x)  // Keep the original 1555 value, alpha bit intact
// or
// #define ABGR1555_VQ(x) ((x) & 0x8000 ? (x) : 0x8000) // Make transparent pixels fully transparent (alpha=1, RGB=0)

#define MAKE_565(r, g, b, a)
#define MAKE_1555(r, g, b, a)
#define MAKE_4444(r, g, b, a)

// Dreamcast = ABGR
// Wii GX = RGB
// These macros swap R and B to correct the color output.

// RGB565 (DC: R5G6B5) → GX RGB565: identical layout, no conversion needed
#define ABGR0565(x) (x)

// Old AGBR1555(x) definition (incorrect, faster ?). Check alpha0.39 for the preset switch
// #define ABGR1555(x) ((x) & 0x8000 ? (x) : 0x0000) // Works together with the coding routing introduced in alpha 0.13 (alpha_fmt stuff)
// New AGBR1555(x) definition :
#define ABGR1555(x) ((x) & 0x8000 ? (x) : \
    ((((x) >> 11) & 0xFu) << 8) | ((((x) >> 6) & 0xFu) << 4) | (((x) >> 1) & 0xFu))

// ARGB4444 (DC: A4 R4 G4 B4) → GX RGB5A3
// ARGB4444 has 4 alpha (transparency) bits, Wii's RGB5A3 has 3 bits.
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
  unsigned int col;  // Vertex color (base)
  unsigned int spc;  // Offset (specular) color, same ABGR layout as col.
                     // Always written by the decode handlers (0 when the poly
                     // has no offset color or OFFSET_COLOR_FIX() is off), only
                     // submitted to GX when OFFSET_COLOR_FIX() is on.
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

#if LOGO_DEBUG_LOG
// [LOGO] probe (see LOGO_DEBUG_LOG at the top): DoRender's inline probes run
// above the probe block that defines these.
extern u32 g_vtx_reset_seq;                                 // reset_vtx_state()
static INLINE bool logo_is_hunted_tex(const PolyParam *pp); // LOGO_TRIGGER_TEX
#endif

struct TextureCacheDesc
{
  GXTexObj  tex;
  GXTlutObj pal;
  u32  addr;            // orig_tex_addr this slot holds (0 = empty)
  bool has_pal;
  u32  vq_codebook_w0;  // VQ fingerprint (cb_w0 ^ idx_w0)
  u32  slot_size;       // bump allocator: total bytes of this allocation
  u32  shape_key;       // CACHE_FAST/CACHE_NORMAL: non-address TCW/TSP bits (format/scan/stride/mip/size)
};

// -- CACHE_VERY_FAST: direct nullDC4Wii slot -------------------------------------
// Slot position = vram_buffer[tex_addr * 2] - O(1), no lookup.
//
static INLINE TextureCacheDesc* skimp_slot(u32 tex_addr)
{
  u32 cache_offset = tex_addr * 2;
  cache_offset = (cache_offset + 31) & ~31u;
  if (cache_offset < 64) cache_offset = 64;
  return (TextureCacheDesc*)&vram_buffer[cache_offset] - 1;
}

// CACHE_EXTRA_DEBUG / CACHE_EXTRA / CACHE_QUALITY: per-frame bump allocator
static const u32 BUMP_TOTAL = 14u * 1024u * 1024u; // 14 MB bump arena

static u32 s_bump_offset = 0;
static void bump_reset() { s_bump_offset = 0; }

// ── O(1) Hash Map for CACHE_QUALITY bump_find ─────────────────────────────────
//
// Layout:
//   - 512 slots, power-of-two so index = hash & (TEXMAP_SIZE-1).
//   - Each slot stores the tex_addr key and the byte offset inside
//     vram_buffer[] where the TextureCacheDesc for that entry lives.
//     The pixel data immediately follows the descriptor (same layout as
//     bump_alloc), so pixel_out is trivially derived from the offset.
//   - TEXMAP_EMPTY (0xFFFFFFFF) marks a free slot. tex_addr 0xFFFFFFFF
//     is not a valid DC texture address (masked by VRAM_MASK = 0x7FFFFF),
//     so there is no false-positive risk.
//   - Collision resolution: linear probing — simple, cache-friendly, and
//     zero extra allocation. The probe wraps around with & mask.
//   - Reset cost: memset of 512*8 = 4 KB — fast on PPC with dcbz.

#define TEXMAP_SIZE  512u           // must be power-of-two
#define TEXMAP_MASK  (TEXMAP_SIZE - 1u)
#define TEXMAP_EMPTY 0xFFFFFFFFu    // sentinel: slot is free

struct TexMapSlot
{
  u32 key;          // tex_addr stored here, TEXMAP_EMPTY if free
  u32 bump_offset;  // byte offset of TextureCacheDesc in vram_buffer[]
};

// Static array: ~4 KB, lives in BSS — no heap, no malloc.
static TexMapSlot s_texmap[TEXMAP_SIZE];

// Knuth multiplicative hash — fast single-multiply on PowerPC.
// DC tex_addr is already a multiple of 8 (TexAddr<<3), so we shift
// right by 3 first to spread entropy into the low bits before hashing.
static INLINE u32 texmap_hash(u32 addr)
{
  return ((addr >> 3) * 2654435761u) >> (32u - 9u); // 9 bits → 0..511
}

// Clear the map in O(TEXMAP_SIZE). Called every frame and on full reset.
static void hash_map_reset()
{
  // memset to 0xFF fills every key field with TEXMAP_EMPTY (0xFFFFFFFF).
  memset(s_texmap, 0xFF, sizeof(s_texmap));
}

// Insert a newly allocated descriptor into the map.
// 'bump_off' is the byte offset returned by bump_alloc (s_bump_offset
// before the allocation advances).
static INLINE void hash_map_insert(u32 tex_addr, u32 bump_off)
{
  u32 idx = texmap_hash(tex_addr) & TEXMAP_MASK;
  // Linear probe: find first free slot.
  // In practice the map is reset every frame so load factor stays low.
  u32 probe = 0;
  while (s_texmap[idx].key != TEXMAP_EMPTY && probe < TEXMAP_SIZE)
  {
    idx = (idx + 1u) & TEXMAP_MASK;
    probe++;
  }
  // If map is somehow full (shouldn't happen with 512 slots per frame)
  // just silently drop the insert; bump_find will return nullptr and
  // the texture will be re-decoded — correct, just not cached this call.
  if (probe < TEXMAP_SIZE)
  {
    s_texmap[idx].key         = tex_addr;
    s_texmap[idx].bump_offset = bump_off;
  }
}

// O(1) replacement for the old O(n) bump_find linear scan.
static INLINE TextureCacheDesc* hash_map_find(u32 orig_tex_addr, u32 **pixel_out)
{
  u32 desc_sz = (sizeof(TextureCacheDesc) + 31) & ~31u;
  u32 idx     = texmap_hash(orig_tex_addr) & TEXMAP_MASK;

  u32 probe = 0;
  while (probe < TEXMAP_SIZE)
  {
    u32 k = s_texmap[idx].key;
    if (k == TEXMAP_EMPTY)
      return nullptr;  // empty slot → key not present
    if (k == orig_tex_addr)
    {
      u32 off = s_texmap[idx].bump_offset;
      *pixel_out = (u32*)&vram_buffer[off + desc_sz];
      return (TextureCacheDesc*)&vram_buffer[off];
    }
    idx = (idx + 1u) & TEXMAP_MASK;
    probe++;
  }
  return nullptr;  // full table, not found (should never happen)
}

static TextureCacheDesc* bump_alloc(u32 pixel_bytes, u32 **pixel_out)
{
  u32 desc_sz   = (sizeof(TextureCacheDesc) + 31) & ~31u;
  u32 pixel_sz  = (pixel_bytes + 31) & ~31u;
  u32 total     = desc_sz + pixel_sz;
  if (s_bump_offset + total > BUMP_TOTAL) s_bump_offset = 0;
  u32 alloc_off            = s_bump_offset;   // capture before advancing
  TextureCacheDesc *d      = (TextureCacheDesc*)&vram_buffer[alloc_off];
  *pixel_out               = (u32*)&vram_buffer[alloc_off + desc_sz];
  memset(d, 0, desc_sz);
  d->slot_size             = total;
  s_bump_offset           += total;
  return d;
}

// ── CACHE_FAST / CACHE_NORMAL: persistent bump arena + hash map ───────────────
//
// Fixes the "puzzled" texture corruption from the old skimp_slot scheme (decoded
// size could overrun into the next address's slot) by giving every texture an
// allocation sized to what it actually decodes to — the same allocator shape
// CACHE_QUALITY uses. Unlike CACHE_QUALITY's arena/map, this one is NEVER cleared
// on a per-frame basis (tex_frame_reset() only touches s_bump_offset/s_texmap
// above), so a static texture still skips re-decoding on every later frame —
// that cross-frame skip is what makes CACHE_FAST/CACHE_NORMAL fast. Reuses the
// same 16 MB vram_buffer budget as everything else; only one cache preset is
// ever active at a time, so there is no real sharing conflict, just a 14 MB
// sub-budget like CACHE_QUALITY's BUMP_TOTAL.
//
// Freshness is NOT implied by "found in the map" the way it is for CACHE_QUALITY
// (which redecodes every texture every frame): the caller still runs the usual
// CACHE_FAST/CACHE_NORMAL sentinel/shape-key (or VQ fingerprint) check against
// whatever this map returns before trusting it.
// Texture-cache instrumentation counters. Declared up here because
// fast_bump_alloc() below bumps the wrap counter; they are read and printed
// once a second by tex_frame_reset(). Temporary — see the report line there.
static u32 g_texc_binds   = 0;
static u32 g_texc_decodes = 0;
static u32 g_texc_frames  = 0;
// Why each decode happened, so a single log line says where the work is going.
static u32 g_texc_m_stride = 0; // dynamic stride surface, contents changed
static u32 g_texc_m_new    = 0; // slot is not holding this address (first use / evicted)
static u32 g_texc_m_sent   = 0; // sentinel gone: the game overwrote the texture
static u32 g_texc_m_shape  = 0; // shape_key or VQ fingerprint mismatch
static u32 g_texc_wraps    = 0; // persistent arena filled and discarded every entry
static u32 g_texc_mapfull  = 0; // 2048-slot map full: texture cannot be cached at all
static u32 g_texc_arena    = 0; // PLUS binds that did not fit their skimp slot
// ...and which formats those were, so the fix aims at the right one.
static u32 g_texc_a_vq     = 0; // VQ: 2 bits/texel source -> 16 bits decoded
static u32 g_texc_a_pal    = 0; // CI4/CI8 decoded to 16bpp (the OPTIMIZED presets)
static u32 g_texc_a_oth    = 0; // anything else
static u32 g_texc_wraps_frame = 0; // arena wraps inside the current frame
static u32 g_texc_allocs   = 0; // arena allocations, i.e. distinct slots handed out
static u32 g_texc_alloc_kb = 0; // KB handed out — tells us how big the arena must be
static u32 s_plus_wrap_frames = 0; // consecutive frames in which the arena wrapped

static const u32 FAST_BUMP_TOTAL = 14u * 1024u * 1024u; // 14 MB, mirrors BUMP_TOTAL

// Which block the persistent bump arena lives in. FAST/NORMAL use the front of
// vram_buffer, as they always have. PLUS cannot: skimp_slot() spreads DC
// addresses across the whole 16 MB (tex_addr * 2 over 8 MB of VRAM), so an
// arena carved out of the same buffer both overwrites slots and shrinks the
// address range the scheme depends on. PLUS uses its own MEM2 block instead.
static INLINE u8* tex_arena_base()
{
  return (CACHE_VERY_FAST_PLUS() && plus_tex_arena) ? plus_tex_arena : vram_buffer;
}
static INLINE u32 tex_arena_limit()
{
  return (CACHE_VERY_FAST_PLUS() && plus_tex_arena) ? plus_tex_arena_size
                                                    : FAST_BUMP_TOTAL;
}

// Cleared for the rest of the session if the arena turns out too small to hold
// the scene (it wrapped again and again, re-decoding everything each time).
// Thrashing it is strictly worse than accepting VERY_FAST's slot overrun, so
// PLUS stops routing to it and behaves like VERY_FAST from then on.
static bool s_plus_arena_ok = true;

static u32 s_fast_bump_offset = 0;

#define FASTMAP_SIZE  2048u // power-of-two; larger than TEXMAP_SIZE since this map
#define FASTMAP_MASK  (FASTMAP_SIZE - 1u) // persists across many frames instead of one
#define FASTMAP_EMPTY 0xFFFFFFFFu

static TexMapSlot s_fastmap[FASTMAP_SIZE]; // ~16 KB, lives in BSS

static void fast_cache_reset()
{
  s_fast_bump_offset = 0;
  memset(s_fastmap, 0xFF, sizeof(s_fastmap));
}

// Single linear probe that returns either the slot already holding tex_addr
// (*found = true) or the first free slot where it could be inserted
// (*found = false) — standard open-addressing lookup-or-insertion-point.
static INLINE u32 fast_map_locate(u32 tex_addr, bool *found)
{
  u32 idx = texmap_hash(tex_addr) & FASTMAP_MASK;
  u32 probe = 0;
  while (probe < FASTMAP_SIZE)
  {
    u32 k = s_fastmap[idx].key;
    if (k == tex_addr)      { *found = true;  return idx; }
    if (k == FASTMAP_EMPTY) { *found = false; return idx; }
    idx = (idx + 1u) & FASTMAP_MASK;
    probe++;
  }
  *found = false;
  g_texc_mapfull++;
  return FASTMAP_SIZE; // table completely full (shouldn't happen at 2048 slots)
}

// Allocates `pixel_bytes` worth of room (descriptor+pixels), sized exactly like
// CACHE_QUALITY's bump_alloc. Wraps (discarding every existing entry) if it
// doesn't fit — rare with a 14 MB budget reused across many frames of texture
// churn, and safe: a wrap means every previously handed-out offset may now be
// overwritten, so every old map entry must be dropped, not just the new one.
static TextureCacheDesc* fast_bump_alloc(u32 pixel_bytes, u32 **pixel_out, u32 *alloc_off_out)
{
  u32 desc_sz  = (sizeof(TextureCacheDesc) + 31) & ~31u;
  u32 pixel_sz = (pixel_bytes + 31) & ~31u;
  u32 total    = desc_sz + pixel_sz;
  u8 *abase = tex_arena_base(); // FAST/NORMAL: vram_buffer. PLUS: its own block.
  if (s_fast_bump_offset + total > tex_arena_limit())
  {
    s_fast_bump_offset = 0;
    memset(s_fastmap, 0xFF, sizeof(s_fastmap));
    g_texc_wraps++;
    g_texc_wraps_frame++;
  }
  g_texc_allocs++;
  g_texc_alloc_kb += total >> 10;
  u32 alloc_off       = s_fast_bump_offset;
  TextureCacheDesc *d = (TextureCacheDesc*)&abase[alloc_off];
  *pixel_out          = (u32*)&abase[alloc_off + desc_sz];
  memset(d, 0, desc_sz);
  d->slot_size        = total;
  s_fast_bump_offset += total;
  *alloc_off_out       = alloc_off;
  return d;
}

// ── Unified cache interface ───────────────────────────────────────────────────
static void tex_cache_init()
{
  s_bump_offset = 0;
  hash_map_reset();
  fast_cache_reset();
}

// CACHE_FAST/CACHE_NORMAL shape_key helper: for index-only GX formats (CI4/CI8 via the
// Fast/Normal presets), the decoded buffer holds palette *indices*, not
// colors — PalSelect only picks which bank the separately-managed TLUT
// samples (see the palette setup block above), so most of it must NOT be
// part of the shape key. Otherwise the same texture redrawn with a
// different PalSelect (e.g. one glyph atlas recolored per character) is
// wrongly treated as a different texture and the whole buffer gets
// redecoded on every single draw, even within the same frame.
//
// BUT: union TCW's PAL.PalSelect (bits 21-26) overlaps the exact same bits
// as NO_PAL.StrideSel/Reserved/ScanOrder (see ta_structs.h) — PalSelect's
// top bit (bit 26, weight 32) IS the ScanOrder bit the decode switch reads
// via mod->tcw.NO_PAL.ScanOrder to choose twiddled vs linear decoding.
// Dropping that bit too would let a PalSelect change that crosses the
// 0-31/32-63 boundary reuse an index buffer decoded under the *other*
// scan-order interpretation — scrambled output. So only bits 21-25 (the
// bank-within-half selector, which doesn't affect decode shape) are
// dropped; bit 26 stays in the key.
//
// JOJO_FIX()-gated: this masking is what lets a glyph atlas recolored per
// character hit the cache instead of redecoding every draw (Jojo's Bizarre
// Adventure). Off by default — every other game keeps the plain shape_key
// (full PalSelect included) it always had.
static INLINE u32 tex_shape_key(PolyParam *mod, u32 pixel_fmt)
{
  u32 key = (mod->tcw.full & ~0x1FFFFFu) ^ (mod->tsp.full & 0x3Fu);
  if (!JOJO_FIX()) return key;
  bool index_only = (pixel_fmt == 5) ? (TEXTURE_4BPP_CI4_FAST() || TEXTURE_4BPP_CI4())
                  : (pixel_fmt == 6) ? (TEXTURE_8BPP_CI8_FAST() || TEXTURE_8BPP_CI8())
                  : false;
  if (index_only) key &= ~(0x3Fu << 21); // drop all PalSelect bits 21-26: palette decode never reads ScanOrder (bit 26 is PalSelect bit 5, not a real ScanOrder flag)
  // old : if (index_only) key &= ~(0x1Fu << 21); // drop PalSelect bits 21-25 only, keep bit 26 (ScanOrder)
  return key;
}

// CACHE_VERY_FAST_PLUS change 2 (v3): content fingerprint for a stride-selected
// scan-order surface. VERY_FAST and QUALITY refuse to cache these at all, since
// they are the dynamically redrawn 2D/menu surfaces and no shape_key can notice
// that the game repainted one. Refusing to cache is correct but expensive: each
// draw re-decodes 512*h texels. Instead, hash 8 words spread across the source
// span the decoder will actually read (512 px wide, 16bpp) and re-decode only
// when that changes -- ~8 VRAM reads against a full surface conversion. Static
// menu art then caches; genuinely animated art still re-decodes, as it must.
static INLINE u32 stride_fingerprint(u32 base, u32 h)
{
  u32 span = 512u * h * 2u;      // bytes norm_text() reads for this surface
  u32 step = span >> 3;          // 8 evenly spaced probes
  u32 fp   = span;
  for (u32 i = 0; i < 8u; i++)
  {
    u32 off = (base + step * i) & (VRAM_MASK & ~3u);
    fp = fp * 31u + *(u32*)&params.vram[off];
  }
  return fp;
}

// CACHE_VERY_FAST_PLUS hit-rate diagnostic. Two u32 increments on the texture
// path (unconditional, so no preset getter call in the hot loop) and one line
// per second in /ndclog.txt — but ONLY while that experimental preset is
// selected, so nothing else in the build gets noisier. Reading it: a low
// cached% means the cache is still missing and re-decoding, so the cost is in
// texture conversion; a high cached% with bad FPS means the decode path is
// fine and the time is going somewhere else entirely.

static int s_last_cache_preset = -1;

static void tex_frame_reset()
{
  // The cache preset can change from the in-game menu. Every offset already in
  // s_fastmap was handed out by the previous preset's allocator, and PLUS uses
  // a different region of vram_buffer than FAST/NORMAL do, so carrying them
  // over would decode straight into another preset's memory.
  {
    int cp = get_texture_cache_preset();
    if (cp != s_last_cache_preset)
    {
      s_last_cache_preset = cp; fast_cache_reset();
      s_plus_arena_ok = true; s_plus_wrap_frames = 0;
    }
  }
  // Deciding whether the overflow arena is worth keeping. A scene load wraps it
  // a few times while the new working set streams in — that is a burst, and
  // tripping on it (as an earlier version did, at >=2 wraps in one frame) threw
  // away the arena on every race start for no reason. Real thrash is a working
  // set that does not fit, and it looks different: it wraps frame after frame
  // after frame. Give up on that, and on a single frame that wraps the whole
  // arena many times over, but ride out a burst.
  if (g_texc_wraps_frame) s_plus_wrap_frames++;
  else                    s_plus_wrap_frames = 0;
  if (s_plus_wrap_frames >= 8 || g_texc_wraps_frame >= 8) s_plus_arena_ok = false;
  g_texc_wraps_frame = 0;

  bump_reset();       // reset arena for new frame
  hash_map_reset();   // clear hash map — O(4 KB memset), fast on PPC
  // params.vram sentinels survive across frames.

  // Texture-cache report, once a second, for whichever preset is active. The
  // counters themselves are two increments on the bind path and are always on
  // (the arena give-up valve above reads them); only the printf is gated, so
  // turning DEBUG MESSAGES on is enough to get the numbers back. This is how
  // the whole very_fast/very_fast_plus investigation was settled - keep it.
  if (++g_texc_frames >= 60)
  {
    if (DEBUG_MESSAGE())
    {
      u32 cached_pct = g_texc_binds ? (100u * (g_texc_binds - g_texc_decodes)) / g_texc_binds : 0u;
      printf("[TEXC] p%d %uf binds=%u dec=%u (%u%% cached, %u/frame) miss: stride=%u new=%u sent=%u shape=%u | wraps=%u mapfull=%u arena=%u(vq=%u pal=%u oth=%u) allocs=%u allocKB=%u plusarena=%u\n",
             get_texture_cache_preset(), g_texc_frames, g_texc_binds, g_texc_decodes,
             cached_pct, g_texc_decodes / g_texc_frames,
             g_texc_m_stride, g_texc_m_new, g_texc_m_sent, g_texc_m_shape,
             g_texc_wraps, g_texc_mapfull, g_texc_arena,
             g_texc_a_vq, g_texc_a_pal, g_texc_a_oth,
             g_texc_allocs, g_texc_alloc_kb, s_plus_arena_ok ? 1u : 0u);
      fflush(stdout); // log is freopen'd to SD; without this the tail is lost
    }
    g_texc_frames = g_texc_binds = g_texc_decodes = 0;
    g_texc_m_stride = g_texc_m_new = g_texc_m_sent = g_texc_m_shape = 0;
    g_texc_wraps = g_texc_mapfull = g_texc_arena = 0;
    g_texc_a_vq = g_texc_a_pal = g_texc_a_oth = 0;
    g_texc_allocs = g_texc_alloc_kb = 0;
  }
}


// VBlank() is defined after PresentFramebuffer()/ShouldSkipFrame() below, so it
// can present the VRAM framebuffer on vblanks where no PVR render occurred.

// Static arrays for vertex data to avoid frequent heap allocations.
// Limited by the Wii's MEM1/MEM2 availability.
Vertex ALIGN16 vertices[42*1024]; // Wii memory limit
VertexList ALIGN16 lists[8*1024];
PolyParam ALIGN16 listModes[8*1024];
// Per-param user tile clip, parallel to listModes (same index). Kept OUT of
// PolyParam so the hot 16-byte param struct stays cache-aligned in the TA
// decode and draw loops. Written at decode time and read at draw time only
// when SPLIT_SCREEN() is on (g_split_screen_cached) — zero cost otherwise.
u32 listTileClip[8*1024]; // 32KB BSS

Vertex *curVTX = vertices;
VertexList *curLST = lists;
VertexList *TransLST = 0;
// Punch-through list boundary, plus the vertex/param cursors captured when the
// TR and PT lists start: lists/vertices/listModes are parallel streams that
// can only be walked from a known cursor, and PUNCH_THROUGH_FIX() draws the PT
// range out of submission order (OP → PT → TR).
VertexList *PTLST = 0;
Vertex *TransVTX = 0;
PolyParam *TransMod = 0;
Vertex *PT_VTX = 0;
PolyParam *PT_Mod = 0;
PolyParam *curMod = listModes;
bool global_regd;

// ── RTT_CARRY_OVERLAY() state ────────────────────────────────────────────────
// When a bit-24 render is dropped in carry mode, StartRender() snapshots that
// pass here and moves the frame origin past it. DoRender() then builds the
// display frame's segments from carry_frame_* instead of the buffer origin, and
// appends one extra segment for [lists .. carry_lst_end) drawn as an overlay.
// carry_lst_end == 0 means "nothing carried", the normal case.
VertexList *carry_lst_end   = 0; // one past the last carried strip
VertexList *carry_trans_lst = 0; // carried pass's own TR boundary (0 = none)
// The clip WINDOW the carried pass was rendering into, in DC screen pixels,
// from its FB_X_CLIP / FB_Y_CLIP. The pass submits ordinary full-screen-space
// geometry and relies on this window to confine it (Silent Scope's scope pass:
// window 0..255 x 0..255, geometry spanning x=-390..24188), so replaying it
// without the window splatters the zoomed view over the whole screen.
// carry_clip_w == 0 means the pass declared no usable window -> draw unclipped.
// RTT_KEEP_LIST(): where the RTT render stopped walking the shared list, plus a
// per-frame cache of the preset (glob_param_bdc needs it to decide whether to
// store listTileClip[], which is what splits the list — see the macro doc).
VertexList *rtt_lst_end = 0;
bool g_rtt_keep_cached = false;

u32 carry_clip_x = 0, carry_clip_y = 0;
u32 carry_clip_w = 0, carry_clip_h = 0;
VertexList *carry_frame_lst = lists;      // display frame's first strip
Vertex     *carry_frame_vtx = vertices;   //  "        "     first vertex
PolyParam  *carry_frame_mod = listModes;  //  "        "     first param

// PVR User Tile Clip state, latched from the TA stream (VertexDecoder::
// SetTileClip / TileClipMode) and captured into listTileClip[] by
// glob_param_bdc. The rect persists until the game sends a new User Tile Clip
// control parameter; the mode is only updated by global params with
// Group_En=1, like real hardware. Splitscreen games (Daytona USA multiplayer)
// draw BOTH players' viewports in one render pass and rely on inside-clipping
// to confine each camera to its half of the screen.
// ta_tileclip holds the packed effective clip (mode<<30 | xmin<<24 | ymin<<16
// | xmax<<8 | ymax, rect in 32px tile units, max inclusive) — recombined only
// in the rare latch calls, so glob_param_bdc just copies one u32.
u32 ta_tileclip_rect = 0; // rect bits of ta_tileclip
u32 ta_tileclip = 0;      // packed mode<<30 | rect
float vtx_min_Z;
float vtx_max_Z;

// Scratch face color for the polygon currently being decoded, used by
// Intensity (Col_Type==2) Gouraud shading: AppendPolyParam1/2B/4B set this
// from the TA stream, and AppendPolyVertex2/7/8/10/13A/14A consume it
// immediately while building each vertex's color, before the next polygon
// header overwrites it. Transient decode-time state only — NOT persisted
// per-entry in listModes[], which is replayed later at draw time and is
// already near the Wii's memory budget (see "Wii memory limit" above).
float curFaceColorR = 1.0f, curFaceColorG = 1.0f, curFaceColorB = 1.0f, curFaceColorA = 1.0f;

// Scratch face OFFSET color (specular), same lifetime rules as curFaceColor*:
// set by AppendPolyParam2B (the only intensity header type carrying one) and
// consumed by OFFS_INTESITY() while decoding that polygon's vertices.
float curFaceOffsR = 0.0f, curFaceOffsG = 0.0f, curFaceOffsB = 0.0f;

// 0xFFFFFFFF while the polygon being decoded has PCW.Offset set AND
// OFFSET_COLOR_FIX() is on, else 0. Lets the vertex handlers keep/drop the
// offset color with a mask (the TA vertex structs always reserve the field,
// but its content is garbage when PCW.Offset is 0).
u32 curPolyOffsMask = 0;

// Per-polygon offset color for sprites (they carry it in the header, not per
// vertex). Already ABGR-converted and masked by curPolyOffsMask.
u32 curSpriteSpc = 0;
// SPRITE_COLOR(): the sprite header's Base Colour, shared by all 4 corners.
// 0xFFFFFFFF (white = no tint under MODULATE) when the preset is off, which
// is exactly what the code hardcoded before the preset existed.
u32 curSpriteCol = 0xFFFFFFFF;
bool g_sprite_color_cached = false;

// Cached once per frame in reset_vtx_state() instead of calling
// VERTEX_COLOR() (an uncached extern getter) on every Intensity vertex —
// the preset can't change mid-frame anyway.
bool g_vertex_color_cached = false;
bool g_offset_color_fix_cached = false; // same idea, for OFFSET_COLOR_FIX()
bool g_fog_cached              = false; // same idea, for FOG(): makes the TA
                                        // decoder keep a polygon's offset-colour
                                        // ALPHA (the per-vertex fog coefficient)
                                        // even when the offset colour itself is
                                        // being dropped
bool g_split_screen_cached     = false; // same idea, for SPLIT_SCREEN(): gates
                                        // the listTileClip[] store per param
bool g_fixed_depth_cached      = false; // same idea, for FIXED_DEPTH_*(): skips
                                        // the per-vertex min/max W tracking
bool  g_legacy_depth_cached    = false; // same idea, for LEGACY_DEPTH()
float g_vert_z_clamp           = 0.0001f; // vert_base 1/W floor: 0.0001 normally,
                                        // 0.001 under LEGACY_DEPTH (1bb8c27).
                                        // A per-frame global rather than a
                                        // literal so the hot macro stays one
                                        // compare, not a branch on a preset.

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

// ============================================================================
// OPTIMIZED DEPTH & MAPPER HELPER FUNCTIONS
// ============================================================================

// Helper: Converts Dreamcast 1/W inverse depth to normalized GX Screen Z [0.0, 1.0]
static INLINE float TransformDCDepthToGX(float w, float near_plane, float far_plane)
{
    // Clamp W to valid bounds
    if (w < near_plane) w = near_plane;
    if (w > far_plane)  w = far_plane;

    // Linear translation from DC 1/W range into GX [0..1] range
    float z_normalized = (w - near_plane) / (far_plane - near_plane);
    return z_normalized;
}

// Global active plane definitions based on active preset
static float g_near_z = 0.1f;
static float g_far_z  = 25000.0f;

void UpdateDepthPlanes()
{
    if (FIXED_DEPTH_WIDE()) {
        g_near_z = 0.0001f;
        g_far_z  = 100000.0f;
    } else if (FIXED_DEPTH_TIGHT()) {
        g_near_z = 0.1f;
        g_far_z  = 25000.0f;
    }
}

// ============================================================================
// PVR2 FOG (see FOG() at the top of the file)
// ============================================================================

// Decoded once per render from the PVR fog registers.
static float s_fog_density  = 0.0f; // FOG_DENSITY as a plain float
static u32   s_fog_col_ram  = 0;    // FOG_COL_RAM,  0x00RRGGBB (LUT modes)
static u32   s_fog_col_vert = 0;    // FOG_COL_VERT, 0x00RRGGBB (per-vertex mode)

// Refreshes the decoded fog registers. Called once per DoRender() when the
// preset is on; the registers are written by the game outside the render, so
// per-frame is exactly as often as they can change.
static void FogFrameSetup()
{
    // FOG_DENSITY is a tiny float: [15:8] = 8-bit mantissa, [7:0] = signed
    // 8-bit exponent, value = mantissa/128 * 2^exponent.
    const u32 den  = FOG_DENSITY;
    const u32 mant = (den >> 8) & 0xFFu;
    const s32 expo = (s32)(s8)(den & 0xFFu);

    // 2^expo without libm: build the float straight from its exponent field.
    // Denormal/overflow ends are clamped rather than encoded, so a garbage
    // register can never hand the table lookup an inf/NaN.
    const s32 be = expo + 127;
    float p2;
    if (be <= 0)        p2 = 0.0f;
    else if (be >= 255) p2 = 3.4028235e38f;
    else { union { u32 i; float f; } u2; u2.i = (u32)be << 23; p2 = u2.f; }

    s_fog_density  = (float)mant * (1.0f / 128.0f) * p2;
    s_fog_col_ram  = FOG_COL_RAM  & 0x00FFFFFFu;
    s_fog_col_vert = FOG_COL_VERT & 0x00FFFFFFu;
}

// Evaluates the 128-entry PVR fog look-up table for one vertex, returning the
// fog coefficient as 0 (no fog) .. 255 (fully fogged).
//
// Hardware indexes the table by the FLOATING-POINT SHAPE of z = W*FOG_DENSITY
// clamped to [1, 256): the exponent (0..7) picks one of 8 octaves and the top
// 4 mantissa bits pick one of 16 steps inside it, so the ramp is fine near the
// fog start and coarse far away. That is why this needs no log2/pow — the
// exponent and mantissa fields ARE the index, so the whole lookup is integer
// bit work on the float's bit pattern.
//
// Each table entry packs two 8-bit values: bits [15:8] is the coefficient at
// the start of that step and bits [7:0] the coefficient at its end; the
// leftover mantissa bits interpolate between them.
//
// W here is this file's depth convention (see vtx_min_Z/vtx_max_Z): the real
// eye-space W, larger = farther, so a larger index means more fog.
//
// If a Wii test shows the ramp running the wrong way WITHIN each step (banded
// or sawtooth fog rather than smooth), the two bytes of an entry are the pair
// to swap — a and b below. If the whole scene is uniformly fogged or uniformly
// clear instead, the suspect is FogFrameSetup()'s FOG_DENSITY decode, not this.
static INLINE u32 FogTableLookup(float W)
{
    float z = W * s_fog_density;
    // Written as !(z > 1.0f) so NaN lands in the clamp too — same reasoning as
    // vert_base()'s depth guard: every comparison against NaN is false, so a
    // "<"-shaped test would let NaN through and index the table with garbage.
    if (!(z > 1.0f))        z = 1.0f;
    else if (z > 255.999f)  z = 255.999f;

    union { float f; u32 i; } u;
    u.f = z;
    const u32 e    = ((u.i >> 23) & 0xFFu) - 127u; // 0..7 for z in [1,256)
    const u32 mant = u.i & 0x7FFFFFu;
    const u32 idx  = (e << 4) | (mant >> 19);      // octave | step -> 0..127
    const u32 frac = (mant >> 11) & 0xFFu;         // position inside the step

    const u32 ent = FOG_TABLE[idx];
    const s32 a   = (s32)((ent >> 8) & 0xFFu);     // coefficient at frac = 0
    const s32 b   = (s32)(ent & 0xFFu);            // coefficient at frac = 255
    const s32 v   = a + (((b - a) * (s32)frac) >> 8);
    return (u32)(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// Primary decoder that translates raw PVR vertex data into the 'Vertex' struct.
void decode_pvr_vertex(u32 base, u32 ptr, Vertex *cv)
{
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
  cv->x = vrf(ptr);  ptr += 4;
  cv->y = vrf(ptr);  ptr += 4;
  cv->z = vrf(ptr);  ptr += 4;

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
      cv->u = vrf(ptr);  ptr += 4;
      cv->v = vrf(ptr);  ptr += 4;
    }
  }

  // Handle Vertex Color
  // Converted to the RGBA8 byte order (R/B swapped from the DC's native
  // ARGB8888) that the rest of the renderer uses for Vertex::col/spc — see
  // ABGR8888() and its other callers — so the background path can share the
  // same interpolation and GX_Color1u32 submission code as regular polys.
  u32 col = vri(ptr);  ptr += 4;
  cv->col = ABGR8888(col);
  cv->spc = 0;
  if (isp.Offset)
  {
    cv->spc = ABGR8888(vri(ptr));  ptr += 4;
  }
}

/*

What You Need to Verify (Potential Caveats)Coordinate Scaling Math: In the snippet, I included this math to snap HUD elements to the near plane:C++float scale = g_near_z / cv->z;
cv->x *= scale;
cv->y *= scale;
cv->z = g_near_z;
Check this: The Dreamcast Tile Accelerator (TA) usually receives pre-transformed screen-space coordinates. If your backend treats cv->x and cv->y as absolute 2D screen coordinates and bypasses the hardware perspective divide, you do not need to scale cv->x and cv->y. You would only scale them if your specific graphics backend (like the GX unit) is going to divide $X$ and $Y$ by your new $Z$ value later in the pipeline.

*/

// Updated PVR Vertex Decoder Depth Processing (seems to make a regression in MSR : Z-fighting)
/*
void decode_pvr_vertex(u32 base, u32 ptr, Vertex *cv)
{
    ISP_TSP isp;
    TSP tsp;
    TCW tcw;

    isp.full = vri(base);
    tsp.full = vri(base + 4);
    tcw.full = vri(base + 8);
    (void)tsp;
    (void)tcw;

    // Fetch coordinates
    cv->x = vrf(ptr); ptr += 4;
    cv->y = vrf(ptr); ptr += 4;
    cv->z = vrf(ptr); ptr += 4; // Raw DC 'W' coordinate

    // LEGACY_DEPTH: at 1bb8c27 this decoder did no depth work whatsoever -
    // neither the tracking below nor the margin/HUD re-park, both of which
    // came later. Skipping the whole thing is part of reproducing it.
    // (Only the background polygon reaches here, a few vertices per frame,
    // so the extra test costs nothing measurable.)
    // Track dynamic bounds if FIXED_DEPTH is OFF
    if (g_legacy_depth_cached) {
        // nothing: matches 1bb8c27
    } else if (!g_fixed_depth_cached) {
        if (cv->z < vtx_min_Z) vtx_min_Z = cv->z;
        if (cv->z > vtx_max_Z) vtx_max_Z = cv->z;
    } else {
        // Apply Margin Padding if enabled to prevent edge-case near plane clipping
        if (DEPTH_CLIP_MARGIN() && cv->z < g_near_z) {
            if (HUD_PASS() != 0) {
                // Prescale screen coords and push W directly onto the near plane
                float scale = g_near_z / cv->z;
                cv->x *= scale;
                cv->y *= scale;
                cv->z = g_near_z;
            }
        }
    }

    // Process Texture Coordinates (UVs)
    if (isp.Texture) {
        if (isp.UV_16b) {
            u32 uv = vri(ptr);
            cv->u = CVT16UV((u16)uv);
            cv->v = CVT16UV((u16)(uv >> 16));
            ptr += 4;
        } else {
            cv->u = vrf(ptr); ptr += 4;
            cv->v = vrf(ptr); ptr += 4;
        }
    }

    // Process Colors
    u32 col = vri(ptr); ptr += 4;
    cv->col = ABGR8888(col);
    cv->spc = 0;
    if (isp.Offset) {
        cv->spc = ABGR8888(vri(ptr)); ptr += 4;
    }
}
*/

// Interpolates a scalar attribute using barycentric weights that were derived
// from the 3 background vertices
static INLINE f32 bg_lerp_f32(f32 a0, f32 a1, f32 a2, f32 w0, f32 w1, f32 w2)
{
  return a0 * w0 + a1 * w1 + a2 * w2;
}

// Interpolates a packed 8/8/8/8 color component-wise (byte order doesn't
// matter as long as all 3 inputs share it — see bg_lerp_f32).
static u32 bg_lerp_color(u32 c0, u32 c1, u32 c2, f32 w0, f32 w1, f32 w2, bool force_opaque_alpha)
{
  u32 out = 0;
  for (int shift = 0; shift < 32; shift += 8)
  {
    f32 ch = ((c0 >> shift) & 0xFFu) * w0 + ((c1 >> shift) & 0xFFu) * w1 + ((c2 >> shift) & 0xFFu) * w2;
    colclamp(0.f, 255.f, ch);
    out |= ((u32)(ch + 0.5f)) << shift;
  }
  if (force_opaque_alpha)
    out = (out & 0x00FFFFFFu) | 0xFF000000u; // alpha occupies bits 24-31 in this byte order too
  return out;
}

#if LOGO_DEBUG_LOG
// Bumped on every buffer rewind so logo_dump_pass() can tell a genuinely
// empty pass from one that resubmitted exactly as much geometry as the
// last one. Comparing cursors cannot: both cases look identical.
u32 g_vtx_reset_seq = 0;
#endif

// Resets internal pointers for the next frame's vertex list.
void reset_vtx_state()
{
#if LOGO_DEBUG_LOG
  g_vtx_reset_seq++;
#endif
  curVTX = vertices;
  curLST = lists;
  curMod = listModes;
  // Clear the list boundaries so a frame without a TR (or PT) list doesn't
  // reuse a stale pointer from the previous frame.
  TransLST = 0;
  PTLST = 0;
  TransVTX = 0;
  TransMod = 0;
  PT_VTX = 0;
  PT_Mod = 0;
  // RTT_CARRY_OVERLAY(): a carried pass only ever survives into the ONE display
  // frame that follows it, so the frame origin goes back to the buffer origin
  // here like everything else.
  carry_lst_end   = 0;
  carry_trans_lst = 0;
  rtt_lst_end       = 0;
  g_rtt_keep_cached = RTT_KEEP_LIST();
  carry_clip_x    = 0;
  carry_clip_y    = 0;
  carry_clip_w    = 0;
  carry_clip_h    = 0;
  carry_frame_lst = lists;
  carry_frame_vtx = vertices;
  carry_frame_mod = listModes;
  global_regd = false;
  vtx_min_Z = 131072;
  vtx_max_Z = 0;
  tex_frame_reset(); // reset per-frame texture bump arena
  g_vertex_color_cached = VERTEX_COLOR();
  g_sprite_color_cached     = SPRITE_COLOR();
  g_offset_color_fix_cached = OFFSET_COLOR_FIX();
  g_fog_cached              = FOG();
  g_split_screen_cached     = SPLIT_SCREEN();
  g_legacy_depth_cached     = LEGACY_DEPTH();
  // 1bb8c27 tracked nothing per vertex, so legacy takes the "skip tracking" path
  // that FIXED_DEPTH_*() uses; DoRender() supplies the planes for both.
  g_fixed_depth_cached      = !FIXED_DEPTH_OFF() || g_legacy_depth_cached;
  g_vert_z_clamp            = g_legacy_depth_cached ? 0.001f : 0.0001f;
}

#define VTX_TFX(x) (x)
#define VTX_TFY(y) (y)

// The Dreamcast uses "Twiddled" (Morton Order) textures to improve cache
// input : yyyyyxxxxx // output : xyxyxyxy
// UV = x*y resolution
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

// Converts YUV422 data to RGB565 for Wii compatibility.
// static + always_inline: __forceinline is a no-op on this devkitPPC build
// (see ta.h: "#define __forceinline"), so without this attribute the compiler
// is free to leave it as a real call/ret — costly since this runs per pixel
// during FMV decode (the hottest texture-conversion path in the renderer).
//
// Split into chroma/luma halves because every YUV422 source pixel pair (and
// every 2x2 twiddled block) shares one U/V pair across 2-4 luma samples.
// Computing the chroma terms (Bc/Gc/Rc) once per shared U/V and reusing them
// drops the per-pixel cost from 7 multiplies (original, computed by every
// caller that re-derived B/G/R from scratch) to 1 (just the luma term) —
// callers that still want a single all-in-one conversion can use YUV422().
static inline void __attribute__((always_inline)) YUV422_chroma(s32 Yu, s32 Yv, s32 &Bc, s32 &Gc, s32 &Rc)
{
  s32 Uc = Yu - 128;
  s32 Vc = Yv - 128;
  Bc = 132252 * Uc;
  Gc = -53281 * Vc - 25624 * Uc;
  Rc = 104595 * Vc;
}

static inline u32 __attribute__((always_inline)) YUV422_luma(s32 Y, s32 Bc, s32 Gc, s32 Rc)
{
  s32 Yc = 76283 * (Y - 16);
  s32 B = (Yc + Bc) >> (16 + 3);
  s32 G = (Yc + Gc) >> (16 + 2);
  s32 R = (Yc + Rc) >> (16 + 3);

  colclamp(0, 0x1F, B);
  colclamp(0, 0x3F, G);
  colclamp(0, 0x1F, R);

  return (R << 11) | (G << 5) | (B);  // RGB565 format
}

static inline u32 __attribute__((always_inline)) YUV422(s32 Y, s32 Yu, s32 Yv)
{
  s32 Bc, Gc, Rc;
  YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
  return YUV422_luma(Y, Bc, Gc, Rc);
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


// FOR TEXTURE_4BPP_CI4()

// Write one 4-bit index into a GX CI4 block-layout buffer.
// GX CI4 tile: 8x8 = 32 bytes / tile
// High nibble = even x within tile, low nibble = odd x.
inline void ci4_prel(u8 *dst, u32 x, u32 y, u32 w, u8 idx)
{
  u32 tile     = (y / 8) * (w / 8) + (x / 8);
  u32 nibble   = tile * 64 + (y % 8) * 8 + (x % 8);
  u32 byte_off = nibble >> 1;
  if (nibble & 1)
    dst[byte_off] = (dst[byte_off] & 0xF0) | (idx & 0x0F); // low nibble
  else
    dst[byte_off] = (dst[byte_off] & 0x0F) | ((idx & 0x0F) << 4); // high nibble
}

// FOR TEXTURE_4BPP_CI8

// Write one 8-bit index into a GX CI8 block-layout buffer.
// GX CI8 tile: 8 pixels wide x 4 pixels tall = 32 bytes per tile.
inline void ci8_prel(u8 *dst, u32 x, u32 y, u32 w, u8 idx)
{
  u32 tile     = (y / 4) * (w / 8) + (x / 8);
  u32 byte_off = tile * 32 + (y % 4) * 8 + (x % 8);
  dst[byte_off] = idx;
}


// ===============
// Pixel Converters for Non-Twiddled (Planar) textures.
// ===============
pixelcvt_start(conv565_PL, 4, 1)
{
  // convert 4x1 565 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y, ABGR0565(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 1, y, ABGR0565(p_in[1])); // 1,0
  pb_prel(pb, pbw, x + 2, y, ABGR0565(p_in[2])); // 2,0
  pb_prel(pb, pbw, x + 3, y, ABGR0565(p_in[3])); // 3,0
}
pixelcvt_next(conv1555_PL, 4, 1)
{
  // convert 4x1 1555 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y, ABGR1555(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 1, y, ABGR1555(p_in[1])); // 1,0
  pb_prel(pb, pbw, x + 2, y, ABGR1555(p_in[2])); // 2,0
  pb_prel(pb, pbw, x + 3, y, ABGR1555(p_in[3])); // 3,0
}
pixelcvt_next(conv4444_PL, 4, 1)
{
  // convert 4x1 4444 to 4x1 8888
  u16 *p_in = (u16 *)data;
  pb_prel(pb, pbw, x + 0, y, ABGR4444(p_in[0])); // 0,0
  pb_prel(pb, pbw, x + 1, y, ABGR4444(p_in[1])); // 1,0
  pb_prel(pb, pbw, x + 2, y, ABGR4444(p_in[2])); // 2,0
  pb_prel(pb, pbw, x + 3, y, ABGR4444(p_in[3])); // 3,0
}
pixelcvt_next(convYUV_PL, 4, 1)
{
  // convert 4x1 4444 to 4x1 8888
  u32 *p_in = (u32 *)data;

  s32 Y0 = (p_in[0] >> 8) & 255;  //
  s32 Yu = (p_in[0] >> 0) & 255;  // p_in[0]
  s32 Y1 = (p_in[0] >> 24) & 255; // p_in[3]
  s32 Yv = (p_in[0] >> 16) & 255; // p_in[2]
  pb_prel(pb, pbw, x + 0, y, YUV422(Y0, Yu, Yv)); // 0,0  
  pb_prel(pb, pbw, x + 1, y, YUV422(Y1, Yu, Yv)); // 1,0

  p_in += 1; // next 4 bytes

  Y0 = (p_in[0] >> 8) & 255;  //
  Yu = (p_in[0] >> 0) & 255;  // p_in[0]
  Y1 = (p_in[0] >> 24) & 255; // p_in[3]
  Yv = (p_in[0] >> 16) & 255; // p_in[2]

    pb_prel(pb, pbw, x + 2, y, YUV422(Y0, Yu, Yv)); // 0,0
  pb_prel(pb, pbw, x + 3, y, YUV422(Y1, Yu, Yv)); // 1,0
}
pixelcvt_end;

// Pixel Converters for Twiddled textures.
// x,y are even (stepped by xpp/ypp = 2), so each 2x2 block lies inside one
// 4x4 GX tile: compute the tile offset ONCE instead of 4 pb_prel/GX_TexOffs
// calls per block — same trick texture_VQ already uses. Layout recap
// (GX_TexOffs): dst[((y>>2)*(pbw>>2) + (x>>2))*16 + (y&3)*4 + (x&3)],
// so within the tile x+1 is +1 and y+1 is +4.
#define TW_BLOCK_BASE ((((y) >> 2) * ((pbw) >> 2) + ((x) >> 2)) * 16 + ((y) & 3) * 4 + ((x) & 3))
pixelcvt_start(conv565_TW, 2, 2)
{
  u16 *p_in = (u16 *)data;
  u32 base = TW_BLOCK_BASE;
  pb[base + 0] = ABGR0565(p_in[0]); // 0,0
  pb[base + 4] = ABGR0565(p_in[1]); // 0,1
  pb[base + 1] = ABGR0565(p_in[2]); // 1,0
  pb[base + 5] = ABGR0565(p_in[3]); // 1,1
}
pixelcvt_next(conv1555_TW, 2, 2)
{
  u16 *p_in = (u16 *)data;
  u32 base = TW_BLOCK_BASE;
  pb[base + 0] = ABGR1555(p_in[0]); // 0,0
  pb[base + 4] = ABGR1555(p_in[1]); // 0,1
  pb[base + 1] = ABGR1555(p_in[2]); // 1,0
  pb[base + 5] = ABGR1555(p_in[3]); // 1,1
}
pixelcvt_next(conv4444_TW, 2, 2)
{
  u16 *p_in = (u16 *)data;
  u32 base = TW_BLOCK_BASE;
  pb[base + 0] = ABGR4444(p_in[0]); // 0,0
  pb[base + 4] = ABGR4444(p_in[1]); // 0,1
  pb[base + 1] = ABGR4444(p_in[2]); // 1,0
  pb[base + 5] = ABGR4444(p_in[3]); // 1,1
}
pixelcvt_next(convYUV422_TW, 2, 2)
{
  u16 *p_in = (u16 *)data;
  u32 base = TW_BLOCK_BASE;

  // Each row's pixel pair shares one U/V: compute the chroma terms once per
  // pair and reuse them (see YUV422_chroma above) instead of the full 7-mul
  // YUV422() per pixel.
  s32 Bc, Gc, Rc;
  YUV422_chroma((p_in[0] >> 0) & 255, (p_in[2] >> 0) & 255, Bc, Gc, Rc);
  pb[base + 0] = YUV422_luma((p_in[0] >> 8) & 255, Bc, Gc, Rc); // 0,0
  pb[base + 1] = YUV422_luma((p_in[2] >> 8) & 255, Bc, Gc, Rc); // 1,0

  YUV422_chroma((p_in[1] >> 0) & 255, (p_in[3] >> 0) & 255, Bc, Gc, Rc);
  pb[base + 4] = YUV422_luma((p_in[1] >> 8) & 255, Bc, Gc, Rc); // 0,1
  pb[base + 5] = YUV422_luma((p_in[3] >> 8) & 255, Bc, Gc, Rc); // 1,1
}
pixelcvt_end;

// VQ Pixel Converters - Convert() averages 4 codebook pixels (legacy),
// ConvertPixel() converts one pixel DC->GX for the new VQ decompressor.
pixelcvt_startVQ(conv565_VQ, 2, 2)
{
  u32 R = ABGR0565_R(data[0]) + ABGR0565_R(data[1]) + ABGR0565_R(data[2]) + ABGR0565_R(data[3]);
  u32 G = ABGR0565_G(data[0]) + ABGR0565_G(data[1]) + ABGR0565_G(data[2]) + ABGR0565_G(data[3]);
  u32 B = ABGR0565_B(data[0]) + ABGR0565_B(data[1]) + ABGR0565_B(data[2]) + ABGR0565_B(data[3]);
  R >>= 2; G >>= 2; B >>= 2;
  return R | (G << 5) | (B << 11);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) { return ABGR0565(p); }
};  // closes struct

pixelcvt_startVQ(conv1555_VQ, 2, 2)
{
  u32 R = ABGR1555_R(data[0]) + ABGR1555_R(data[1]) + ABGR1555_R(data[2]) + ABGR1555_R(data[3]);
  u32 G = ABGR1555_G(data[0]) + ABGR1555_G(data[1]) + ABGR1555_G(data[2]) + ABGR1555_G(data[3]);
  u32 B = ABGR1555_B(data[0]) + ABGR1555_B(data[1]) + ABGR1555_B(data[2]) + ABGR1555_B(data[3]);
  u32 A = ABGR1555_A(data[0]) + ABGR1555_A(data[1]) + ABGR1555_A(data[2]) + ABGR1555_A(data[3]);
  R >>= 2; G >>= 2; B >>= 2; A >>= 2;
  return R | (G << 5) | (B << 10) | (A << 15);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) { return ABGR1555(p); }
};  // closes struct

pixelcvt_startVQ(conv4444_VQ, 2, 2)
{
  u32 R = ABGR4444_R(data[0]) + ABGR4444_R(data[1]) + ABGR4444_R(data[2]) + ABGR4444_R(data[3]);
  u32 G = ABGR4444_G(data[0]) + ABGR4444_G(data[1]) + ABGR4444_G(data[2]) + ABGR4444_G(data[3]);
  u32 B = ABGR4444_B(data[0]) + ABGR4444_B(data[1]) + ABGR4444_B(data[2]) + ABGR4444_B(data[3]);
  u32 A = ABGR4444_A(data[0]) + ABGR4444_A(data[1]) + ABGR4444_A(data[2]) + ABGR4444_A(data[3]);
  R >>= 2; G >>= 2; B >>= 2; A >>= 2;
  return R | (G << 4) | (B << 8) | (A << 12);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) { return ABGR4444(p); }
};  // closes struct

pixelcvt_startVQ(convYUV422_VQ, 2, 2)
{
  u16 *p_in = (u16 *)data;
  s32 Y0 = (p_in[0] >> 8) & 255;
  s32 Yu = (p_in[0] >> 0) & 255;
  s32 Y1 = (p_in[2] >> 8) & 255;
  s32 Yv = (p_in[2] >> 0) & 255;
  return YUV422(16 + ((Y0 - 16) + (Y1 - 16)) / 2.0f, Yu, Yv);
}  // closes inner block
}  // closes Convert
  __forceinline static u16 ConvertPixel(u16 p) {
    s32 Y = (p >> 8) & 255; s32 U = (p >> 0) & 255;
    return YUV422(Y, U, 128);
  }
};  // closes struct

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

// ----------------------------------------------------------------------------
// Twiddle axis-table cache.
//
// Every twiddled texture path below needs table_x[x]=twop(x,0,w,h) and
// table_y[y]=twop(0,y,w,h) so the per-pixel loop only does a cheap
// "table_x[x] | table_y[y]" instead of calling twop() per pixel. A game
// reuses the same handful of texture sizes every frame, so the raw tables
// are cached globally, keyed by (w,h) — twop's interleave order depends on
// both dimensions, not just one, so a non-square texture needs its own
// entry. Decoders use the cached tables in place: block formats divide the
// OR'd value by their power-of-two divider at the use site, which is exact
// because the two tables occupy disjoint bit positions.
// ----------------------------------------------------------------------------
#define TWIDDLE_CACHE_SLOTS 24

struct TwiddleAxisEntry
{
  u32 w, h;           // 0,0 = unused slot
  u32 table_x[1024];  // raw twop(x,0,w,h) for x in [0,w)
  u32 table_y[1024];  // raw twop(0,y,w,h) for y in [0,h)
};

static TwiddleAxisEntry g_twiddle_cache[TWIDDLE_CACHE_SLOTS];
static u32 g_twiddle_cache_count = 0;

// Returns raw (undivided) per-axis twiddle tables for (w,h), building and
// caching them on first use. table_x[x] | table_y[y] == twop(x,y,w,h); both
// can be shifted (e.g. >>2 for 2x2-block formats) before or after the OR,
// since the two tables occupy disjoint bit positions.
static void get_twiddle_axis_tables(u32 w, u32 h, const u32 **out_x, const u32 **out_y)
{
  for (u32 i = 0; i < g_twiddle_cache_count; i++)
  {
    if (g_twiddle_cache[i].w == w && g_twiddle_cache[i].h == h)
    {
      *out_x = g_twiddle_cache[i].table_x;
      *out_y = g_twiddle_cache[i].table_y;
      return;
    }
  }

  // Not cached yet: claim a slot (evict slot 0 once full — in practice a
  // game's set of distinct texture sizes is small and stable, so eviction
  // essentially never triggers).
  u32 idx = (g_twiddle_cache_count < TWIDDLE_CACHE_SLOTS) ? g_twiddle_cache_count++ : 0;
  TwiddleAxisEntry &e = g_twiddle_cache[idx];
  e.w = w;
  e.h = h;
  for (u32 x = 0; x < w; x++) e.table_x[x] = twop(x, 0, w, h);
  for (u32 y = 0; y < h; y++) e.table_y[y] = twop(0, y, w, h);

  *out_x = e.table_x;
  *out_y = e.table_y;
}

// Texture untwiddling and conversion template. (Handler)
template <class PixelConvertor>
void fastcall texture_TW(u8 *p_in, u32 Width, u32 Height)
{
  u8 *pb = VramWork;
  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp;

  // Use the cached raw per-axis tables directly — divider is a power of two
  // and the two tables occupy disjoint bits, so dividing after the OR at the
  // use site is exact. No per-decode divided copies needed.
  const u32 *table_x, *table_y;
  get_twiddle_axis_tables(Width, Height, &table_x, &table_y);

  for (u32 y = 0; y < Height; y += PixelConvertor::ypp)
  {
    u32 offset_y = table_y[y];
    for (u32 x = 0; x < Width; x += PixelConvertor::xpp)
    {
      u8 *p = &p_in[((offset_y | table_x[x]) / divider) << 3];

      u16 buf[4];
      buf[0] = *host_ptr_xor((u16*)&p[0]);
      buf[1] = *host_ptr_xor((u16*)&p[2]);
      buf[2] = *host_ptr_xor((u16*)&p[4]);
      buf[3] = *host_ptr_xor((u16*)&p[6]);

      PixelConvertor::Convert((u16*)pb, x, y, Width, (u8*)buf);
    }
  }
  // NOTE: do NOT memcpy back to p_in (params.vram). Decoded pixels live in
  // VramWork (vram_buffer slot). Writing back would overwrite the raw DC
  // texture data, corrupting it for future scenes and making DEADBEEF
  // sentinel detection unreliable.
}

// VQ texture decompressor: expands compressed data into full RGB pixels in GX
// 4x4-block layout. Identical output format to texture_TW — no CI8/TLUT needed.
template <class PixelConvertor>
void fastcall texture_VQ(u8 *p_in, u32 Width, u32 Height, u8 *vq_codebook)
{
  u8 *pb = VramWork;
  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp; // 4 for 2x2

  const u32 *table_x, *table_y;
  get_twiddle_axis_tables(Width, Height, &table_x, &table_y);

  u16 *dst = (u16*)pb;

  for (u32 y = 0; y < Height; y += PixelConvertor::ypp)
  {
    u32 offset_y = table_y[y];
    for (u32 x = 0; x < Width; x += PixelConvertor::xpp)
    {

      u8 idx = *host_ptr_xor(&p_in[(offset_y | table_x[x]) / divider]);

      u8 *cb = &vq_codebook[idx * 8];
      u16 s0 = *host_ptr_xor((u16*)&cb[0]);
      u16 s1 = *host_ptr_xor((u16*)&cb[2]);
      u16 s2 = *host_ptr_xor((u16*)&cb[4]);
      u16 s3 = *host_ptr_xor((u16*)&cb[6]);

      // Calculate the tile base address ONCE for the 2x2 block!
      // This eliminates 4 calls to pb_prel (and GX_TexOffs) per iteration.
      u32 tile_offset = ((y >> 2) * (Width >> 2) + (x >> 2)) * 16;
      u32 base = tile_offset + (y & 3) * 4 + (x & 3);

      dst[base]     = PixelConvertor::ConvertPixel(s0); // (x+0, y+0)
      dst[base + 1] = PixelConvertor::ConvertPixel(s2); // (x+1, y+0)
      dst[base + 4] = PixelConvertor::ConvertPixel(s1); // (x+0, y+1)
      dst[base + 5] = PixelConvertor::ConvertPixel(s3); // (x+1, y+1)
    }
  }
}

// Planar (Linear) texture conversion.
template <int type>
void Plannar(u8 *praw, u32 w, u32 h)
{
  // host_ptr_xor(u16* p) = (u16*)((unat)p ^ (4-sizeof(u16))) = (u16*)((unat)p ^ 2).
  // This correctly reads a DC little-endian u16 from Wii big-endian VRAM where each
  // u32 word is stored byte-swapped: the FIRST DC pixel in a u32 lives at byte+2,
  // the SECOND at byte+0.  ^2 handles that swap correctly for a FIXED address.
  //
  // BUT: the original sequential ptr++ loop reads even pixels via ^2 (correct)
  // then advances ptr by 2 bytes, so the odd pixel is read at byte+2 via ^2
  // which lands on byte+0 of the NEXT word — reading the wrong pixel.
  // Net result: every adjacent pair of pixels is swapped.
  //
  // FIX: read both pixels of each u32 word at once, extract in correct order.
  // The u32 read on Wii BE gives the correct DC u32 value directly (host_ptr_xor
  // for u32 is ^0 = identity, so *(u32*)ptr is already correct).
  // Within that u32: low 16 bits = first DC pixel, high 16 bits = second DC pixel.

  u32 *src = (u32 *)praw;  // u32 reads give correct DC values directly
  u16 *dst = (u16 *)VramWork;
  u32 x, y;

  for (y = 0; y < h; y++)
  {
    for (x = 0; x < w; x += 2)  // two pixels per u32 word
    {
      u32 word = *src++;          // correct DC u32 value: low=pix0, high=pix1
      u16 pix0 = (u16)(word & 0xFFFF);
      u16 pix1 = (u16)(word >> 16);

      // x is even, so both pixels of the pair land in the same 4x4 GX tile
      // at consecutive offsets: one tile-base computation replaces the two
      // GX_TexOffs calls per pair (same trick as the TW/VQ decoders above).
      u32 base = ((y >> 2) * (w >> 2) + (x >> 2)) * 16 + (y & 3) * 4 + (x & 3);

      switch (type)
      {
      case 1555:
        dst[base + 0] = ABGR1555(pix0);
        dst[base + 1] = ABGR1555(pix1);
        break;
      case 565:
        dst[base + 0] = ABGR0565(pix0);
        dst[base + 1] = ABGR0565(pix1);
        break;
      case 4444:
        dst[base + 0] = ABGR4444(pix0);
        dst[base + 1] = ABGR4444(pix1);
        break;
      case 422:
      {
        // DC YUV422 planar is UYVY: U,Y0,V,Y1 at DC LE bytes 0-3.
        // u32 read on Wii BE gives correct DC LE value; extract accordingly.
        // The pair shares one U/V, so the chroma terms are computed once.
        s32 Yu = (word >>  0) & 255;  // U (Cb)
        s32 Y0 = (word >>  8) & 255;  // Y0
        s32 Yv = (word >> 16) & 255;  // V (Cr)
        s32 Y1 = (word >> 24) & 255;  // Y1
        s32 Bc, Gc, Rc;
        YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
        dst[base + 0] = YUV422_luma(Y0, Bc, Gc, Rc);
        dst[base + 1] = YUV422_luma(Y1, Bc, Gc, Rc);
      }
      break;
      }
    }
  }
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

// PVR Mipmap offsets (Dreamcast specific) — §3.6.2.4
//
// DC stores mipmaps small→large: TCW Texture Address = start of the 1×1 level.
// To reach the full-size level we must SKIP all the smaller levels.
//
// OtherMipPoint[n]: byte offset to add to tex_addr to reach the full-size level
//   for a texture whose log2(width) = n  (n = TexU + 3, width = 8 << TexU).
//   Index 0..2 (1x1,2x2,4x4) included for completeness; real DC textures use idx 3..10.
//   Closed form: OtherMipPoint[n] = (3 + (4^n - 1)/3) * bpp/8
//   where the "+3" accounts for the 3-texel reserved header before the 1x1 level.
//   The table below gives the TEXEL count (multiply by bpp/8 to get bytes).
//   Values: 0x3, 0x4, 0x8, 0x18, 0x58, 0x158, 0x558, 0x1558, 0x5558, 0x15558, 0x55558
static const u32 OtherMipPoint[11] = {
    0x00003, // idx 0:  1×1   (never used for real textures)
    0x00004, // idx 1:  2×2
    0x00008, // idx 2:  4×4
    0x00018, // idx 3:  8×8
    0x00058, // idx 4:  16×16
    0x00158, // idx 5:  32×32
    0x00558, // idx 6:  64×64
    0x01558, // idx 7:  128×128
    0x05558, // idx 8:  256×256
    0x15558, // idx 9:  512×512
    0x55558  // idx 10: 1024×1024
};

// VQMipPoint[n]: byte offset FROM tex_addr (= codebook base) to reach the full-size
//   VQ index map. Codebook is always 2048 bytes; index maps then follow small→large.
//   VQMipPoint[n] = 2048 + index-map offset to the full-size level.
static const u32 VQMipPoint[11] = {
    2048 + 0x00000, // idx 0:  1×1
    2048 + 0x00001, // idx 1:  2×2
    2048 + 0x00002, // idx 2:  4×4
    2048 + 0x00006, // idx 3:  8×8
    2048 + 0x00016, // idx 4:  16×16
    2048 + 0x00056, // idx 5:  32×32
    2048 + 0x00156, // idx 6:  64×64
    2048 + 0x00556, // idx 7:  128×128
    2048 + 0x01556, // idx 8:  256×256
    2048 + 0x05556, // idx 9:  512×512
    2048 + 0x15556  // idx 10: 1024×1024
};

// Macro to encapsulate the logic for twiddled textures (VQ or standard).
//
// MIPMAP note (§3.6.2.4): TCW Texture Address = start of the 1×1 level (smallest).
// Levels are packed small→large. We must offset forward to the full-size level.
// For VQ the codebook base is NEVER offset; only the index map base moves.
// For non-VQ: add OtherMipPoint[idx] * bpp/8 bytes (16bpp → *2).
// MipMapped textures are always square → height = width (ignore TexV).
#define twidle_tex(format)                                                                \
  if (mod->tcw.NO_PAL.VQ_Comp)                                                            \
  {                                                                                       \
    /* Codebook always at tex_addr — do NOT move this for mipmaps */                      \
    vq_codebook = (u8 *)&params.vram[tex_addr];                                           \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
    {                                                                                     \
      /* Jump to full-size VQ index map (codebook + small-level indices) */               \
      u32 mip_idx = mod->tsp.TexU + 3; /* log2(width): TexU 0..7 → idx 3..10 */         \
      tex_addr += VQMipPoint[mip_idx];                                                    \
      /* MipMapped = square: ignore TexV, use TexU for both dimensions */                 \
      h = w;                                                                              \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
      tex_addr += 2048; /* skip codebook to reach index map */                            \
    }                                                                                     \
    texture_VQ<conv##format##_VQ> /**/ ((u8 *)&params.vram[tex_addr], w, h, vq_codebook); \
    texVQ = 1;                                                                            \
  }                                                                                       \
  else                                                                                    \
  {                                                                                       \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
    {                                                                                     \
      /* Skip all smaller mip levels to reach the full-size level (16bpp → ×2 bytes) */  \
      u32 mip_idx = mod->tsp.TexU + 3; /* log2(width): TexU 0..7 → idx 3..10 */         \
      tex_addr += OtherMipPoint[mip_idx] * 2; /* 16bpp: texels × 2 bytes */              \
      /* MipMapped = square: ignore TexV, use TexU for both dimensions */                 \
      h = w;                                                                              \
    }                                                                                     \
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



// -----------------------------------------------------------------
// CMPR (DXT1) block encoder
// -----------------------------------------------------------------
// GX CMPR layout: 8x8 super-tile of four 4x4 DXT1 sub-blocks: [TL][TR][BL][BR]
// Each 4x4 DXT1 block = 8 bytes:
//   [0-1] color0 RGB565 big-endian   (must be > color1 for 4-color opaque mode)
//   [2-3] color1 RGB565 big-endian
//   [4-7] 16x 2-bit selectors, row-major, MSB-first (pixel0=bits[31:30])
//         index 0=color0, 1=color1, 2=(2c0+c1)/3, 3=(c0+2c1)/3
// Strategy: min/max luma anchor selection + nearest-palette assignment per pixel.
//
// SUSPECTED BUG, not fixed here, believed independent of everything else: the
// endpoints are chosen by LUMA, but the mode bit GX reads is the UNSIGNED
// comparison of the two packed RGB565 words, and those two orders disagree.
// Pure green (0x07E0) is brighter than pure red (0xF800) but packs far lower,
// so a block spanning those comes out c0 < c1 - which is CMPR's 3-colour +
// TRANSPARENT mode, and a quarter of its texels silently vanish. The fix is one
// line, `if (c0 < c1) swap(c0, c1)` right after the two anchors are picked: the
// 4-colour palette is symmetric, so the interpolants merely trade places and
// every texel still picks its nearest entry. Parked on branch `vq_cmpr_1555`
// alongside the 1555 work, so it has NOT been tested apart from it - if that
// branch's Power Stone 2 regression turns out to be this rather than the alpha
// handling, this is where to look. Affects fmv_format=cmpr as well.
// Exact for x < 768 (171 == floor(512/3) + 1); our callers pass x <= 189.
static inline u32 __attribute__((always_inline)) div3_u8(u32 x) { return (x * 171) >> 9; }
static void encode_cmpr_block(const u16 *src, u8 *dst)
{
  u32 best_min = 0xFFFFFFFF, best_max = 0;
  int idx_min = 0, idx_max = 0;
  for (int i = 0; i < 16; i++)
  {
    u16 p  = src[i];
    u32 r  = (p >> 11) & 0x1F;
    u32 g  = (p >>  5) & 0x3F;
    u32 b  = (p      ) & 0x1F;
    u32 lm = r * 2 + g + b * 2;
    if (lm < best_min) { best_min = lm; idx_min = i; }
    if (lm > best_max) { best_max = lm; idx_max = i; }
  }

  u16 c0 = src[idx_max]; // brightest  -> color0 (must be >= color1)
  u16 c1 = src[idx_min]; // darkest    -> color1

  // Solid-color block: nudge c0 up so GX stays in 4-color mode (c0 > c1).
  if (c0 == c1)
  {
    u32 r = (c0 >> 11) & 0x1F;
    u32 g = (c0 >>  5) & 0x3F;
    u32 b = (c0      ) & 0x1F;
    if (r < 0x1F) r++; else if (g < 0x3F) g++; else if (b < 0x1F) b++;
    c0 = (u16)((r << 11) | (g << 5) | b);
  }

  u32 r0 = (c0 >> 11) & 0x1F, g0 = (c0 >> 5) & 0x3F, b0 = c0 & 0x1F;
  u32 r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
  // Fast unsigned /3 via multiply+shift (exact for inputs < 768, our inputs
  // top out at ~189): PowerPC's divwu is far slower than a multiply+shift,
  // and this runs 6x per 4x4 block for every CMPR-encoded FMV frame.
  u32 r2 = div3_u8(2*r0+r1), g2 = div3_u8(2*g0+g1), b2 = div3_u8(2*b0+b1);
  u32 r3 = div3_u8(r0+2*r1), g3 = div3_u8(g0+2*g1), b3 = div3_u8(b0+2*b1);

  u32 selectors = 0;
  for (int i = 0; i < 16; i++)
  {
    u16 p   = src[i];
    s32 pr6 = ((p >> 11) & 0x1F) << 1; // scale R/B to 6-bit for fair distance
    s32 pg  = (p >>  5) & 0x3F;
    s32 pb6 = ((p      ) & 0x1F) << 1;

    s32 dr0=pr6-(s32)(r0<<1), dg0=pg-(s32)g0, db0=pb6-(s32)(b0<<1);
    s32 dr1=pr6-(s32)(r1<<1), dg1=pg-(s32)g1, db1=pb6-(s32)(b1<<1);
    s32 dr2=pr6-(s32)(r2<<1), dg2=pg-(s32)g2, db2=pb6-(s32)(b2<<1);
    s32 dr3=pr6-(s32)(r3<<1), dg3=pg-(s32)g3, db3=pb6-(s32)(b3<<1);

    u32 d0=(u32)(dr0*dr0+dg0*dg0+db0*db0);
    u32 d1=(u32)(dr1*dr1+dg1*dg1+db1*db1);
    u32 d2=(u32)(dr2*dr2+dg2*dg2+db2*db2);
    u32 d3=(u32)(dr3*dr3+dg3*dg3+db3*db3);

    u32 best_d=d0, best_idx=0;
    if (d1<best_d){best_d=d1;best_idx=1;}
    if (d2<best_d){best_d=d2;best_idx=2;}
    if (d3<best_d){best_d=d3;best_idx=3;}

    selectors |= (best_idx << (30 - i * 2));
  }

  dst[0]=(u8)(c0>>8); dst[1]=(u8)(c0&0xFF);
  dst[2]=(u8)(c1>>8); dst[3]=(u8)(c1&0xFF);
  dst[4]=(u8)(selectors>>24); dst[5]=(u8)(selectors>>16);
  dst[6]=(u8)(selectors>> 8); dst[7]=(u8)(selectors    );
}

// Real per-row pixel stride for the planar YUV422 source, independent of the
// destination CMPR block width (which must stay == the declared GX texture
// width). Rewritten on every decode by the case-3 YUV branch in
// SetTextureParams; 0 = no override, decode at the declared width. Only a
// texture the YUV converter wrote (an FMV frame) gets a non-zero value - see
// that branch for why trusting TA_YUV_TEX_CTRL for every YUV422 texture is
// wrong.
int g_yuv_src_stride_override = 0;
// Real source row COUNT (height), same idea as the stride above but for the
// vertical axis. 0 = no override, use the declared h. Rows at/after this are
// NOT real source data: treat them the same way rows past the declared height
// already are (zero-fill), instead of reading past the legitimately-written
// buffer.
int g_yuv_src_real_h = 0;

// YUV422 planar -> GX_TF_CMPR
// DC planar YUV422: UYVY layout — DC LE bytes [U, Y0, V, Y1] at addresses [0,1,2,3].
// Each pixel pair is two u16s (Y<<8)|chroma; LE storage puts chroma at even addresses.
// Wii BE u32 read gives the DC LE u32 value; bit-shift extraction recovers channels.
// VQ -> CMPR (DXT1), for CACHE_VERY_FAST_PLUS.
//
// Why this exists: a VQ texture is 2 bits/texel in VRAM (plus a 2 KB codebook)
// and decodes to 16 bits/texel, a 7.6x expansion. Its address-derived cache
// slot only has room for 2x, so every VQ texture overruns its neighbour — the
// corruption VERY_FAST is known for — and routing them to a sized arena instead
// needs ~314 KB each, far more than the Wii can spare. At 4 bits/texel CMPR is
// a 2x expansion, and the codebook the source carries provides the margin, so
// every VQ size from 8x8 to 1024x1024 fits its slot with room over. No extra
// memory, no overrun, no arena.
//
// It streams: each 4x4 CMPR block is built from four 2x2 codebook lookups on
// the stack. A full-size 16bpp intermediate would need up to 512 KB of scratch,
// and MEM1 has nothing like that to spare.
//
// Quality: DXT1 on top of VQ is a second lossy pass. VQ is already 2x2-block
// based so the two mostly agree, but smooth gradients (skies, fades) will band.
// That is why this is opt-in.
template <class PixelConvertor>
void fastcall texture_VQ_CMPR(u8 *p_in, u32 Width, u32 Height, u8 *vq_codebook)
{
  u8 *dst = VramWork;
  const u32 divider = PixelConvertor::xpp * PixelConvertor::ypp; // 4 for 2x2

  const u32 *table_x, *table_y;
  get_twiddle_axis_tables(Width, Height, &table_x, &table_y);

  // GX CMPR is an 8x8 super-tile of four 4x4 DXT1 blocks: [TL][TR][BL][BR].
  static const u32 sub_ox[4] = {0,4,0,4};
  static const u32 sub_oy[4] = {0,0,4,4};

  for (u32 ty = 0; ty < Height; ty += 8)
  for (u32 tx = 0; tx < Width;  tx += 8)
  {
    u8 *super_dst = dst + ((ty >> 3) * (Width >> 3) + (tx >> 3)) * 32;
    for (int sub = 0; sub < 4; sub++)
    {
      const u32 bx = tx + sub_ox[sub], by = ty + sub_oy[sub];
      u16 block[16]; // 4x4 pixels, row-major, as encode_cmpr_block wants
      for (u32 dy = 0; dy < 4; dy += 2)
      {
        const u32 offset_y = table_y[by + dy];
        for (u32 dx = 0; dx < 4; dx += 2)
        {
          // One index byte per 2x2 block, same addressing as texture_VQ.
          u8 idx = *host_ptr_xor(&p_in[(offset_y | table_x[bx + dx]) / divider]);
          u8 *cb = &vq_codebook[idx * 8];
          u16 s0 = *host_ptr_xor((u16*)&cb[0]);
          u16 s1 = *host_ptr_xor((u16*)&cb[2]);
          u16 s2 = *host_ptr_xor((u16*)&cb[4]);
          u16 s3 = *host_ptr_xor((u16*)&cb[6]);
          block[ dy      * 4 + dx    ] = PixelConvertor::ConvertPixel(s0);
          block[ dy      * 4 + dx + 1] = PixelConvertor::ConvertPixel(s2);
          block[(dy + 1) * 4 + dx    ] = PixelConvertor::ConvertPixel(s1);
          block[(dy + 1) * 4 + dx + 1] = PixelConvertor::ConvertPixel(s3);
        }
      }
      encode_cmpr_block(block, super_dst + sub * 8);
    }
  }
}

// VQ branch of twidle_tex(), routed through texture_VQ_CMPR instead. Kept as a
// macro for the same reason twidle_tex is one: it needs tex_addr/w/h/texVQ and
// the conv##format##_VQ token from the caller.
#define twidle_tex_vq_cmpr(format)                                                        \
  {                                                                                       \
    /* Codebook always at tex_addr - do NOT move this for mipmaps */                      \
    vq_codebook = (u8 *)&params.vram[tex_addr];                                           \
    if (mod->tcw.NO_PAL.MipMapped)                                                        \
    {                                                                                     \
      /* Jump to the full-size VQ index map, exactly as twidle_tex does */                \
      tex_addr += VQMipPoint[mod->tsp.TexU + 3];                                          \
      h = w; /* MipMapped = square: ignore TexV */                                        \
    }                                                                                     \
    else                                                                                  \
    {                                                                                     \
      tex_addr += 2048; /* skip codebook to reach index map */                            \
    }                                                                                     \
    texture_VQ_CMPR<conv##format##_VQ>((u8 *)&params.vram[tex_addr], w, h, vq_codebook);  \
    texVQ = 1;                                                                            \
  }

static void YUV422_to_CMPR_Planar(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 src_stride = g_yuv_src_stride_override ? (u32)g_yuv_src_stride_override : w;
  u32 real_h = g_yuv_src_real_h ? (u32)g_yuv_src_real_h : h;
  static const u32 sub_ox[4] = {0,4,0,4};
  static const u32 sub_oy[4] = {0,0,4,4};

  for (u32 ty = 0; ty < h; ty += 8)
  for (u32 tx = 0; tx < w; tx += 8)
  {
    u8 *super_dst = dst + ((ty/8)*(w/8) + (tx/8)) * 32;
    for (int sub = 0; sub < 4; sub++)
    {
      u32 bx=tx+sub_ox[sub], by=ty+sub_oy[sub];
      u16 block_pixels[16];
      for (u32 row = 0; row < 4; row++)
      {
        u32 y = by + row;
        if (y >= h || y >= real_h) { for (int c=0;c<4;c++) block_pixels[row*4+c]=0; continue; }
        for (u32 col = 0; col < 4; col += 2)
        {
          // DC YUV422 planar is YUYV: Y0,U,Y1,V at DC LE bytes 0-3.
          // u32 read gives the correct DC LE value on Wii BE, so bit-shift
          // extraction recovers the right bytes. Byte-positional reads are
          // WRONG on Wii: the Wii stores bytes in reversed order within each
          // u32 (to satisfy the u32-read-equals-DC-LE invariant), so p[0]
          // would be V (DC byte 3), not U (DC byte 0).
          u32 word = *(u32*)&src_vram[(y*src_stride + bx+col) * 2];
          s32 Yu = (word >>  0) & 255;  // U (Cb) = DC UYVY byte 0
          s32 Y0 = (word >>  8) & 255;  // Y0
          s32 Yv = (word >> 16) & 255;  // V (Cr) = DC UYVY byte 2
          s32 Y1 = (word >> 24) & 255;  // Y1
          s32 Bc, Gc, Rc;
          YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
          block_pixels[row*4+col  ] = (u16)YUV422_luma(Y0, Bc, Gc, Rc);
          block_pixels[row*4+col+1] = (u16)YUV422_luma(Y1, Bc, Gc, Rc);
        }
      }
      encode_cmpr_block(block_pixels, super_dst + sub*8);
    }
  }
}

// -----------------------------------------------------------------
// Twiddled YUV422 source fetch (shared by the CMPR/RGBA8/RGB565 decoders)
// -----------------------------------------------------------------
// A twiddled YUV422 texture stores each 2x2 pixel block as one 8-byte chunk of
// four DC u16s, every u16 being (Y << 8) | chroma:
//   dc16[0] : Y(col0,row0) | U(row0)
//   dc16[1] : Y(col0,row1) | U(row1)
//   dc16[2] : Y(col1,row0) | V(row0)
//   dc16[3] : Y(col1,row1) | V(row1)
// (column-major inside the block, which is just how DC twiddling interleaves
// the axes — this is exactly what the legacy convYUV422_TW converter reads
// through host_ptr_xor in texture_TW, and it is the reference for ON.)
//
// VRAM on this big-endian host is byte-swapped inside every u32 so that a u32
// read returns the DC little-endian u32 verbatim, and host_ptr_xor for u16 is
// ^2. So dc16[0] is the LOW half of the first u32 and dc16[1] the HIGH half:
//   w0 = *(u32*)&p[0]  ->  dc16[0] = w0 & 0xFFFF,  dc16[1] = w0 >> 16
//   w1 = *(u32*)&p[4]  ->  dc16[2] = w1 & 0xFFFF,  dc16[3] = w1 >> 16
//
// The legacy (fix OFF) extraction had BOTH of those backwards: it took the u32
// halves the other way round, and it read luma from the low byte and chroma
// from the high byte. Net effect: every luma slot received a chroma sample
// (~128, near constant over a normal picture) while both chroma slots received
// luma. The image keeps its geometry but collapses onto the green<->magenta
// axis, dark->green and bright->magenta, with 1-column striping because U and V
// then come from two different columns' luma. That is the Virtua Fighter 3tb
// "FIRST MATCH" loading screen, and any other game using static twiddled YUV422
// artwork. Real FMV is unaffected because the YUV converter writes planar
// (scan-order) data, which goes through the separate — and correct — planar
// decoders.
//
// OFF is kept byte-for-byte identical to that legacy behaviour (including its
// single chroma pair shared by both rows) so the preset is a true no-op.
struct yuv_tw_block { s32 Y00, Y01, Y10, Y11, Yu0, Yv0, Yu1, Yv1; };

static inline void __attribute__((always_inline))
yuv_tw_fetch(const u8 *p, int fix, yuv_tw_block &b)
{
  u32 w0 = *(const u32*)&p[0];
  u32 w1 = *(const u32*)&p[4];

  if (fix)
  {
    u16 d0 = (u16)(w0      ); // Y(col0,row0) | U(row0)
    u16 d1 = (u16)(w0 >> 16); // Y(col0,row1) | U(row1)
    u16 d2 = (u16)(w1      ); // Y(col1,row0) | V(row0)
    u16 d3 = (u16)(w1 >> 16); // Y(col1,row1) | V(row1)

    b.Y00 = (d0 >> 8) & 255; b.Yu0 = d0 & 255;
    b.Y10 = (d1 >> 8) & 255; b.Yu1 = d1 & 255;
    b.Y01 = (d2 >> 8) & 255; b.Yv0 = d2 & 255;
    b.Y11 = (d3 >> 8) & 255; b.Yv1 = d3 & 255;
  }
  else
  {
    u16 buf0 = (u16)(w0 >> 16);
    u16 buf1 = (u16)(w0      );
    u16 buf2 = (u16)(w1 >> 16);
    u16 buf3 = (u16)(w1      );

    b.Y00 = buf0 & 255; b.Yv0 = b.Yv1 = (buf0 >> 8) & 255;
    b.Y10 = buf1 & 255;
    b.Y01 = buf2 & 255; b.Yu0 = b.Yu1 = (buf2 >> 8) & 255;
    b.Y11 = buf3 & 255;
  }
}

// YUV422 twiddled -> GX_TF_CMPR
// Source layout, the u32-half mapping and what the legacy path got wrong are
// all documented on yuv_tw_fetch() above.
static void YUV422_to_CMPR_Twiddled(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  const int tw_fix = YUV_TW_FIX() ? 1 : 0; // read once per texture, not per block
  static const u32 sub_ox[4] = {0,4,0,4};
  static const u32 sub_oy[4] = {0,0,4,4};

  // Precompute twiddle block indices per axis instead of recomputing the
  // morton code in the innermost loop: twiddle_razi(x,y,..) ==
  // twiddle_razi(x,0,..) | twiddle_razi(0,y,..) because the two axes
  // interleave into disjoint bit positions. Same trick texture_TW already
  // uses below — turns w*h/4 twiddle_razi calls into just w+h of them.
  const u32 *table_x, *table_y;
  get_twiddle_axis_tables(w, h, &table_x, &table_y);

  for (u32 ty = 0; ty < h; ty += 8)
  for (u32 tx = 0; tx < w; tx += 8)
  {
    u8 *super_dst = dst + ((ty/8)*(w/8) + (tx/8)) * 32;
    for (int sub = 0; sub < 4; sub++)
    {
      u32 bx=tx+sub_ox[sub], by=ty+sub_oy[sub];
      u16 block_pixels[16];
      for (u32 row = 0; row < 4; row += 2)
      for (u32 col = 0; col < 4; col += 2)
      {
        u32 x   = bx + col;
        u32 y   = by + row;
        u32 tw  = (table_y[y] | table_x[x]) / 4; // 2x2 block index (divider = xpp*ypp = 4)
        u8 *p   = &src_vram[tw * 8];
        yuv_tw_block b;
        yuv_tw_fetch(p, tw_fix, b);

        s32 Bc, Gc, Rc;
        YUV422_chroma(b.Yu0, b.Yv0, Bc, Gc, Rc); // row 0 shares one U/V pair
        block_pixels[row*4+col    ] = (u16)YUV422_luma(b.Y00, Bc, Gc, Rc);
        block_pixels[row*4+col+1  ] = (u16)YUV422_luma(b.Y01, Bc, Gc, Rc);
        YUV422_chroma(b.Yu1, b.Yv1, Bc, Gc, Rc); // row 1 has its own U/V pair
        block_pixels[(row+1)*4+col  ] = (u16)YUV422_luma(b.Y10, Bc, Gc, Rc);
        block_pixels[(row+1)*4+col+1] = (u16)YUV422_luma(b.Y11, Bc, Gc, Rc);
      }
      encode_cmpr_block(block_pixels, super_dst + sub*8);
    }
  }
}

// -----------------------------------------------------------------
// RGBA8 encoder
// -----------------------------------------------------------------
// GX RGBA8 tile layout: 4x4 tile = 64 bytes, split into two 32-byte sub-tiles:
//   Sub-tile 0 [0-31] : AR pairs  (A0,R0, A1,R1 ... A15,R15)
//   Sub-tile 1 [32-63]: GB pairs  (G0,B0, G1,B1 ... G15,B15)
// Pixels in row-major order within each sub-tile.

// Inline RGB565 -> RGB888 expansion (replicates MSBs into LSBs for full range).
#define RGB565_R8(p) ((u8)((((p)>>11)&0x1F)<<3|(((p)>>13)&0x7)))
#define RGB565_G8(p) ((u8)((((p)>> 5)&0x3F)<<2|(((p)>> 9)&0x3)))
#define RGB565_B8(p) ((u8)((((p)    )&0x1F)<<3|(((p)>> 2)&0x7)))

// YUV422 planar -> GX_TF_RGBA8
static void YUV422_to_RGBA8_Planar(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 src_stride = g_yuv_src_stride_override ? (u32)g_yuv_src_stride_override : w;
  u32 real_h  = g_yuv_src_real_h ? (u32)g_yuv_src_real_h : h;
  u32 tiles_x = w / 4, tiles_y = h / 4;
  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u8 *tile = dst + (ty*tiles_x + tx) * 64;
    u8 *ar   = tile;
    u8 *gb   = tile + 32;
    for (u32 row = 0; row < 4; row++)
    {
      u32 y = ty*4 + row;
      for (u32 col = 0; col < 4; col += 2)
      {
        u32 p0 = row*4+col, p1 = row*4+col+1;
        if (y >= real_h)
        {
          ar[p0*2]=0xFF; ar[p0*2+1]=0; ar[p1*2]=0xFF; ar[p1*2+1]=0;
          gb[p0*2]=0;    gb[p0*2+1]=0; gb[p1*2]=0;    gb[p1*2+1]=0;
          continue;
        }
        u32 x    = tx*4 + col;
        u32 word = *(u32*)&src_vram[(y*src_stride + x) * 2];
        s32 Yu = (word >>  0) & 255;  // U
        s32 Y0 = (word >>  8) & 255;  // Y0
        s32 Yv = (word >> 16) & 255;  // V
        s32 Y1 = (word >> 24) & 255;  // Y1
        s32 Bc, Gc, Rc;
        YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
        u16 c0 = (u16)YUV422_luma(Y0, Bc, Gc, Rc);
        u16 c1 = (u16)YUV422_luma(Y1, Bc, Gc, Rc);
        ar[p0*2  ]=0xFF; ar[p0*2+1]=RGB565_R8(c0);
        ar[p1*2  ]=0xFF; ar[p1*2+1]=RGB565_R8(c1);
        gb[p0*2  ]=RGB565_G8(c0); gb[p0*2+1]=RGB565_B8(c0);
        gb[p1*2  ]=RGB565_G8(c1); gb[p1*2+1]=RGB565_B8(c1);
      }
    }
  }
}

// YUV422 twiddled -> GX_TF_RGBA8
// See yuv_tw_fetch() for the 2x2 twiddled YUV layout and the host_ptr_xor (^2)
// byte-swap replication via raw u32 reads.
static void YUV422_to_RGBA8_Twiddled(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  const int tw_fix = YUV_TW_FIX() ? 1 : 0; // read once per texture, not per block
  u32 tiles_x = w / 4, tiles_y = h / 4;

  // Same per-axis twiddle precompute as YUV422_to_CMPR_Twiddled — see there.
  const u32 *table_x, *table_y;
  get_twiddle_axis_tables(w, h, &table_x, &table_y);

  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u8 *tile = dst + (ty*tiles_x + tx) * 64;
    u8 *ar   = tile;
    u8 *gb   = tile + 32;
    for (u32 row = 0; row < 4; row += 2)
    for (u32 col = 0; col < 4; col += 2)
    {
      u32 x  = tx*4 + col;
      u32 y  = ty*4 + row;
      u32 tw = (table_y[y] | table_x[x]) / 4;
      u8 *p  = &src_vram[tw * 8];
      yuv_tw_block b;
      yuv_tw_fetch(p, tw_fix, b);

      s32 Bc, Gc, Rc;
      YUV422_chroma(b.Yu0, b.Yv0, Bc, Gc, Rc); // row 0 shares one U/V pair
      u16 c00 = (u16)YUV422_luma(b.Y00, Bc, Gc, Rc);
      u16 c01 = (u16)YUV422_luma(b.Y01, Bc, Gc, Rc);
      YUV422_chroma(b.Yu1, b.Yv1, Bc, Gc, Rc); // row 1 has its own U/V pair
      u16 c10 = (u16)YUV422_luma(b.Y10, Bc, Gc, Rc);
      u16 c11 = (u16)YUV422_luma(b.Y11, Bc, Gc, Rc);

      u32 idx00 = row*4+col,         idx01 = row*4+col+1;
      u32 idx10 = (row+1)*4+col,     idx11 = (row+1)*4+col+1;

      ar[idx00*2  ]=0xFF; ar[idx00*2+1]=RGB565_R8(c00);
      ar[idx01*2  ]=0xFF; ar[idx01*2+1]=RGB565_R8(c01);
      ar[idx10*2  ]=0xFF; ar[idx10*2+1]=RGB565_R8(c10);
      ar[idx11*2  ]=0xFF; ar[idx11*2+1]=RGB565_R8(c11);

      gb[idx00*2  ]=RGB565_G8(c00); gb[idx00*2+1]=RGB565_B8(c00);
      gb[idx01*2  ]=RGB565_G8(c01); gb[idx01*2+1]=RGB565_B8(c01);
      gb[idx10*2  ]=RGB565_G8(c10); gb[idx10*2+1]=RGB565_B8(c10);
      gb[idx11*2  ]=RGB565_G8(c11); gb[idx11*2+1]=RGB565_B8(c11);
    }
  }
}

#undef RGB565_R8
#undef RGB565_G8
#undef RGB565_B8

// -----------------------------------------------------------------
// RGB565 encoder (fmv_format == 2)
// -----------------------------------------------------------------
// GX RGB565 tile layout: 4x4 tile = 32 bytes, 16 pixels in row-major order,
// no AR/GB split like RGBA8 and no block compression like CMPR. YUV422()
// already returns a packed RGB565 value, so each pixel is a direct store —
// this is the cheapest of the three paths per pixel converted.

// YUV422 planar -> GX_TF_RGB565
static void YUV422_to_RGB565_Planar(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  u32 src_stride = g_yuv_src_stride_override ? (u32)g_yuv_src_stride_override : w;
  u32 real_h  = g_yuv_src_real_h ? (u32)g_yuv_src_real_h : h;
  u32 tiles_x = w / 4, tiles_y = h / 4;
  u16 *dst16  = (u16 *)dst;
  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u16 *tile = dst16 + (ty*tiles_x + tx) * 16;
    for (u32 row = 0; row < 4; row++)
    {
      u32 y = ty*4 + row;
      if (y >= real_h)
      {
        tile[row*4+0]=0; tile[row*4+1]=0; tile[row*4+2]=0; tile[row*4+3]=0;
        continue;
      }
      for (u32 col = 0; col < 4; col += 2)
      {
        u32 x    = tx*4 + col;
        u32 word = *(u32*)&src_vram[(y*src_stride + x) * 2];
        s32 Yu = (word >>  0) & 255;  // U
        s32 Y0 = (word >>  8) & 255;  // Y0
        s32 Yv = (word >> 16) & 255;  // V
        s32 Y1 = (word >> 24) & 255;  // Y1
        s32 Bc, Gc, Rc;
        YUV422_chroma(Yu, Yv, Bc, Gc, Rc);
        tile[row*4+col  ] = (u16)YUV422_luma(Y0, Bc, Gc, Rc);
        tile[row*4+col+1] = (u16)YUV422_luma(Y1, Bc, Gc, Rc);
      }
    }
  }
}

// YUV422 twiddled -> GX_TF_RGB565
// See yuv_tw_fetch() for the twiddled YUV layout, and YUV422_to_CMPR_Twiddled
// for the twiddle-table precompute.
static void YUV422_to_RGB565_Twiddled(u8 *src_vram, u32 w, u32 h, u8 *dst)
{
  const int tw_fix = YUV_TW_FIX() ? 1 : 0; // read once per texture, not per block
  u32 tiles_x = w / 4, tiles_y = h / 4;
  u16 *dst16  = (u16 *)dst;

  const u32 *table_x, *table_y;
  get_twiddle_axis_tables(w, h, &table_x, &table_y);

  for (u32 ty = 0; ty < tiles_y; ty++)
  for (u32 tx = 0; tx < tiles_x; tx++)
  {
    u16 *tile = dst16 + (ty*tiles_x + tx) * 16;
    for (u32 row = 0; row < 4; row += 2)
    for (u32 col = 0; col < 4; col += 2)
    {
      u32 x  = tx*4 + col;
      u32 y  = ty*4 + row;
      u32 tw = (table_y[y] | table_x[x]) / 4;
      u8 *p  = &src_vram[tw * 8];
      yuv_tw_block b;
      yuv_tw_fetch(p, tw_fix, b);

      s32 Bc, Gc, Rc;
      YUV422_chroma(b.Yu0, b.Yv0, Bc, Gc, Rc); // row 0 shares one U/V pair
      tile[(row  )*4+col  ] = (u16)YUV422_luma(b.Y00, Bc, Gc, Rc);
      tile[(row  )*4+col+1] = (u16)YUV422_luma(b.Y01, Bc, Gc, Rc);
      YUV422_chroma(b.Yu1, b.Yv1, Bc, Gc, Rc); // row 1 has its own U/V pair
      tile[(row+1)*4+col  ] = (u16)YUV422_luma(b.Y10, Bc, Gc, Rc);
      tile[(row+1)*4+col+1] = (u16)YUV422_luma(b.Y11, Bc, Gc, Rc);
    }
  }
}

// =========================
// GX mip-chain generation
// =========================
//
// DC mipmapped textures ship their own mip levels (small→large), but every
// decode path above only converts the full-size level. Instead of teaching
// each of the ~15 format paths to decode the DC mip levels too, we box-filter
// the already-decoded base level (GX 4x4-block 16bpp — the output format of
// texture_TW/texture_VQ and the palette-baking paths alike) into a proper GX
// mip chain: levels packed contiguously after the base, smallest level 4x4
// (so every level is a whole 32-byte block — no sub-block padding games).
// This kills distant-poly shimmer and improves texture-cache locality.

// Extra bytes the generated chain needs beyond the base level for a square
// 16bpp texture: levels w/2 .. 4, each w*h*2 bytes.
static u32 MipChainExtraBytes16(u32 w)
{
  u32 bytes = 0;
  for (u32 lw = w >> 1; lw >= 4; lw >>= 1)
    bytes += lw * lw * 2;
  return bytes;
}

static INLINE void RGB5A3_Decode(u16 p, u32 *a, u32 *r, u32 *g, u32 *b)
{
  if (p & 0x8000) // opaque RGB555
  {
    *a = 255;
    *r = ((p >> 10) & 31) << 3;
    *g = ((p >>  5) & 31) << 3;
    *b = ( p        & 31) << 3;
  }
  else            // A3RGB444
  {
    *a = ((p >> 12) &  7) << 5;
    *r = ((p >>  8) & 15) << 4;
    *g = ((p >>  4) & 15) << 4;
    *b = ( p        & 15) << 4;
  }
}

static INLINE u16 RGB5A3_Encode(u32 a, u32 r, u32 g, u32 b)
{
  if (a >= 0xE0) // effectively opaque → use the 555 encoding
    return (u16)(0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
  return (u16)(((a >> 5) << 12) | ((r >> 4) << 8) | ((g >> 4) << 4) | (b >> 4));
}

// Box-filters `base` (square, GX-tiled 16bpp, width w) into successive mip
// levels written directly after it. Returns the number of levels generated
// (0 for w <= 4). Caller must have allocated MipChainExtraBytes16(w) bytes
// of room past the base level.
static u32 GenerateMipChain16(u16 *base, u32 w, bool is565)
{
  u32 levels = 0;
  u16 *src = base;
  u16 *dst = base + w * w;

  for (u32 sw = w; sw > 4; sw >>= 1)
  {
    u32 dw = sw >> 1;
    for (u32 y = 0; y < dw; y++)
    {
      for (u32 x = 0; x < dw; x++)
      {
        u16 p0 = src[GX_TexOffs(x * 2,     y * 2,     sw)];
        u16 p1 = src[GX_TexOffs(x * 2 + 1, y * 2,     sw)];
        u16 p2 = src[GX_TexOffs(x * 2,     y * 2 + 1, sw)];
        u16 p3 = src[GX_TexOffs(x * 2 + 1, y * 2 + 1, sw)];

        u16 o;
        if (is565)
        {
          u32 r = ((p0 >> 11) & 31) + ((p1 >> 11) & 31) + ((p2 >> 11) & 31) + ((p3 >> 11) & 31);
          u32 g = ((p0 >>  5) & 63) + ((p1 >>  5) & 63) + ((p2 >>  5) & 63) + ((p3 >>  5) & 63);
          u32 b = ( p0        & 31) + ( p1        & 31) + ( p2        & 31) + ( p3        & 31);
          o = (u16)(((r >> 2) << 11) | ((g >> 2) << 5) | (b >> 2));
        }
        else // RGB5A3: mixed 555/A3444 encodings — average in 8-bit ARGB
        {
          u32 a0, r0, g0, b0, a1, r1, g1, b1, a2, r2, g2, b2, a3, r3, g3, b3;
          RGB5A3_Decode(p0, &a0, &r0, &g0, &b0);
          RGB5A3_Decode(p1, &a1, &r1, &g1, &b1);
          RGB5A3_Decode(p2, &a2, &r2, &g2, &b2);
          RGB5A3_Decode(p3, &a3, &r3, &g3, &b3);
          o = RGB5A3_Encode((a0 + a1 + a2 + a3) >> 2,
                            (r0 + r1 + r2 + r3) >> 2,
                            (g0 + g1 + g2 + g3) >> 2,
                            (b0 + b1 + b2 + b3) >> 2);
        }
        dst[GX_TexOffs(x, y, dw)] = o;
      }
    }
    src  = dst;
    dst += dw * dw;
    levels++;
  }
  return levels;
}

// =================================
// Dreamcast Texture => Wii textures
// =================================
// Processes the Dreamcast's TCW (Texture Control Word) to initialize Wii TexObjects.
// 4 steps

// SEAM_FIX(): half a texel of the texture currently bound to TEXMAP0, in UV units
// (0.5/w, 0.5/h). Stashed on every texture bind so the draw loop can inset each
// strip's UV rectangle by this much. Zero when the texture size is unknown.
static f32 g_seam_half_u = 0.0f;
static f32 g_seam_half_v = 0.0f;
static inline void SeamFixStashTexSize(const GXTexObj *tex)
{
  const u16 w = GX_GetTexObjWidth(tex);
  const u16 h = GX_GetTexObjHeight(tex);
  g_seam_half_u = w ? 0.5f / (f32)w : 0.0f;
  g_seam_half_v = h ? 0.5f / (f32)h : 0.0f;
}

static void SetTextureParams(PolyParam *mod, bool decal_alpha_fix)
{
  // decal_alpha_fix off: keep the original unconditional GX_MODULATE here so this
  // path costs exactly what it did before the fix existed. On: the caller sets
  // TEVSTAGE0's op itself based on TSP.ShadInstr right after this returns.
  if (!decal_alpha_fix)
    GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

  u32 tex_addr = (mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
  const u32 orig_tex_addr = tex_addr;
  u32 *ptex_skmp = (u32 *)&params.vram[tex_addr];
  g_texc_binds++; // see tex_frame_reset()

  // Declare all variables needed by section 1 upfront.
  bool is_vq = mod->tcw.NO_PAL.VQ_Comp;
  // Stride-selected scan-order surface: decodes 512 px wide and is redrawn by
  // the game. Same test VERY_FAST and QUALITY use, deliberately without a
  // PixelFmt exclusion so PLUS treats exactly the same set of textures they do.
  bool stride_dyn = mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder;
  u32 FMT    = GX_TF_RGB565;
  u32 texVQ  = 0;
  u8 *vq_codebook;
  u32 w = 8 << mod->tsp.TexU;
  u32 h = 8 << mod->tsp.TexV;

  //// 1. Memory Management ////

  // Worst-case allocation:
  //   RGBA8   (YUV fmv_format=1) : w*h*4 bytes (32bpp)
  //   RGB565  (YUV fmv_format=2) : w*h*2 bytes (16bpp)
  //   CMPR    (YUV fmv_format=0) : w*h/2 bytes (4bpp DXT1)
  //   All other formats           : w*h*2 bytes (16bpp)
  // We check the pixel format (tcw.PixelFmt==3 => YUV422) here so the bump
  // allocator always gets enough room before any conversion runs.
  u32 decode_bytes;
  {
    bool is_yuv422 = (mod->tcw.NO_PAL.PixelFmt == 3);
    // Mipmapped textures decode square: every mip path forces h = w, which
    // can exceed the h = 8<<TexV this sizing would otherwise use.
    u32 eff_h = mod->tcw.NO_PAL.MipMapped ? w : h;
    // CACHE_VERY_FAST_PLUS change 1 (see the macro block at the top of this
    // file): stride-selected scan-order textures decode at w = 512, not at
    // 8<<TexU, so size the slot for 512 or the decoder runs off the end of it.
    // PixelFmt 5/6 are excluded because a palettised TCW has no StrideSel or
    // ScanOrder field — those bits are PalSelect, and reading them here would
    // over-allocate a 16x-too-large slot for ordinary palette textures.
    u32 eff_w = w;
    if (CACHE_VERY_FAST_PLUS() && w < 512
        && mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder
        && mod->tcw.NO_PAL.PixelFmt != 5 && mod->tcw.NO_PAL.PixelFmt != 6)
      eff_w = 512;
    if (is_vq && VQ_CMPR() && mod->tcw.NO_PAL.PixelFmt == 1)
    {
      // VQ decoded to CMPR is 4 bits/texel, a quarter of the 16bpp assumption
      // below, and this is an exact figure rather than a worst case. Saying so
      // matters for the presets that actually ALLOCATE (fast/normal/quality):
      // otherwise they reserve 4x what the decode writes and throw away the
      // memory saving that is the whole point of vq_cmpr. CMPR never gets a
      // generated mip chain either (GenerateMipChain16 only runs for
      // RGB565/RGB5A3), so there is no tail to add.
      decode_bytes = (u32)w * eff_h / 2u;
    }
    else
    {
      if (is_yuv422 && FMV_FORMAT_RGBA8())
        decode_bytes = (u32)eff_w * eff_h * 4; // RGBA8: 4 bytes/pixel
      else
        decode_bytes = (u32)eff_w * eff_h * 2; // 16bpp worst-case (covers CMPR and RGB565)
      // 16bpp mipmapped textures grow a generated GX mip chain after the base
      // level (~1/3 extra) — see GenerateMipChain16 above. MIPMAP_OFF skips the
      // chain entirely, so no extra room is needed.
      if (mod->tcw.NO_PAL.MipMapped && !is_yuv422 && !MIPMAP_OFF())
        decode_bytes += MipChainExtraBytes16(w);
    }
  }
  // CACHE_VERY_FAST_PLUS routing. VERY_FAST does not use a cache in the usual
  // sense: slot = vram_buffer[tex_addr * 2], so every DC address has a
  // permanent, private home (DC VRAM is 8 MB, vram_buffer is 16 MB, so the 2x
  // spread covers all of it). Nothing is ever evicted and nothing is ever
  // looked up — which is why it measures 100% cached / 0 decodes per frame on
  // scenes where the 14 MB FAST/NORMAL arena has to keep throwing textures out
  // and re-decoding them. Its one real flaw is that the slot has no size: the
  // room available is 2x the texture's VRAM footprint (the gap to the next
  // texture), and formats that expand by more than 2x on decode run into their
  // neighbour. VQ is the bad case (2 bits/texel source, 16 bits/texel decoded).
  //
  // So PLUS keeps the free, eviction-proof slot for every texture that provably
  // fits in it, and sends only the ones that would overrun to the sized bump
  // arena. Best of both: VERY_FAST's hit rate without VERY_FAST's corruption.
  bool plus_skimp = false;
  if (CACHE_VERY_FAST_PLUS())
  {
    u32 pf = mod->tcw.NO_PAL.PixelFmt;
    // Every mip path forces h = w (mipmapped DC textures are square), so use
    // that height here too or the sums below describe the wrong texture.
    u32 eh = mod->tcw.NO_PAL.MipMapped ? w : h;
    u32 src_bytes;                                     // VRAM footprint of this texture
    if      (is_vq)      src_bytes = 2048u + (w * eh) / 4u; // codebook + 2bpp indices
    else if (stride_dyn) src_bytes = 512u * eh * 2u;        // 512 px wide, 16bpp
    else if (pf == 5)    src_bytes = (w * eh) / 2u;         // CI4
    else if (pf == 6)    src_bytes = w * eh;                // CI8
    else                 src_bytes = w * eh * 2u;           // 16bpp / YUV422
    // Mipmapped textures actually occupy MORE than this (the chain below the
    // full-size level), so using the base level alone is the conservative
    // choice: it can only push a texture to the arena, never into an overrun.
    // decode_bytes is deliberately pessimistic: it assumes 16bpp for everything
    // that is not YUV/RGBA8, because the allocators must never under-reserve.
    // A skimp slot is not allocated though — it only has to be big enough — so
    // testing against that figure would route every CI4/CI8 texture to the arena
    // even under the CI presets, which decode to 4 and 8 bits per texel and fit
    // the slot comfortably. Use what the decoder will really write.
    // VQ-as-CMPR needs no correction here: decode_bytes is already exact for it.
    u32 dec_for_slot = decode_bytes;
    if (pf == 5 && !(TEXTURE_4BPP_OPTIMIZED() || TEXTURE_4BPP_RGB565()))
      dec_for_slot = (w * eh) / 2u;  // GX_TF_I4 / GX_TF_CI4
    else if (pf == 6 && !(TEXTURE_8BPP_OPTIMIZED() || TEXTURE_8BPP_RGB565()))
      dec_for_slot = w * eh;         // GX_TF_I8 / GX_TF_CI8
    // (both are indexed/intensity outputs, which never get a generated mip
    //  chain, so there is no extra tail to account for here.)
    u32 need = ((sizeof(TextureCacheDesc) + 31) & ~31u) + ((dec_for_slot + 31) & ~31u);
    // The slot is used whenever the decode fits the gap to the next texture.
    // It is also used when there is no arena to fall back on, or when the arena
    // has already proved too small for this scene: an overrun costs correctness
    // on some other texture, but arena thrash costs a full re-decode of
    // everything, every frame, which is the slower and more visible failure.
    plus_skimp = (need <= src_bytes * 2u)
              || !plus_tex_arena || !s_plus_arena_ok;
    if (!plus_skimp)
    {
      g_texc_arena++;
      if      (is_vq)                  g_texc_a_vq++;
      else if (pf == 5 || pf == 6)     g_texc_a_pal++;
      else                             g_texc_a_oth++;
    }
  }

  u32 *pixel_buf   = nullptr;
  TextureCacheDesc *pbuff = nullptr;
  bool already_decoded_this_frame = false;

  if (CACHE_VERY_FAST() || plus_skimp)
  {
    // Direct-slot cache: slot derived from tex_addr. O(1), no scan, no map,
    // no arena, and therefore no eviction — see the routing note above.
    pbuff     = skimp_slot(tex_addr);
    pixel_buf = (u32*)&pbuff[1];
  }
  else if (CACHE_FAST())
  {
    // Persistent bump arena + hash map: O(1) lookup like skimp_slot, but the
    // allocation is sized to what this texture actually decodes to, so it
    // can never overrun into a neighboring texture's data.
    bool found;
    u32  fidx   = fast_map_locate(tex_addr, &found);
    u32  desc_sz = (sizeof(TextureCacheDesc) + 31) & ~31u;
    if (found)
    {
      u32 off   = s_fastmap[fidx].bump_offset;
      u8 *abase = tex_arena_base();
      pbuff     = (TextureCacheDesc*)&abase[off];
      pixel_buf = (u32*)&abase[off + desc_sz];
    }
    else
    {
      u32 alloc_off;
      pbuff = fast_bump_alloc(decode_bytes, &pixel_buf, &alloc_off);
      if (fidx != FASTMAP_SIZE) // table had a free slot for it
      {
        s_fastmap[fidx].key         = tex_addr;
        s_fastmap[fidx].bump_offset = alloc_off;
      }
      // pbuff->addr stays 0 from fast_bump_alloc's memset, so the validity
      // check below naturally treats this as a cache miss and decodes it.
    }
  }
  else if (CACHE_NORMAL_FAMILY())
  {
    // Persistent bump arena + hash map: O(1) lookup like skimp_slot, but the
    // allocation is sized to what this texture actually decodes to, so it
    // can never overrun into a neighboring texture's data.
    bool found;
    u32  fidx   = fast_map_locate(tex_addr, &found);
    u32  desc_sz = (sizeof(TextureCacheDesc) + 31) & ~31u;
    if (found)
    {
      u32 off   = s_fastmap[fidx].bump_offset;
      u8 *abase = tex_arena_base();
      pbuff     = (TextureCacheDesc*)&abase[off];
      pixel_buf = (u32*)&abase[off + desc_sz];
      // The slot was sized for whatever shape this address held when it was
      // allocated. If the texture at this address changed shape and now
      // decodes bigger (e.g. non-mip → mipmapped, which appends a generated
      // mip chain), decoding in place would overrun the next slot — grab a
      // fresh, correctly sized allocation instead.
      if (pbuff->slot_size < desc_sz + ((decode_bytes + 31) & ~31u))
      {
        u32 alloc_off;
        pbuff = fast_bump_alloc(decode_bytes, &pixel_buf, &alloc_off);
        // fast_bump_alloc may have wrapped and cleared the map — re-locate.
        bool refound;
        fidx = fast_map_locate(tex_addr, &refound);
        if (fidx != FASTMAP_SIZE)
        {
          s_fastmap[fidx].key         = tex_addr;
          s_fastmap[fidx].bump_offset = alloc_off;
        }
        // pbuff->addr is 0 from the alloc memset → validity check below
        // treats this as a miss and re-decodes into the new slot.
      }
    }
    else
    {
      u32 alloc_off;
      pbuff = fast_bump_alloc(decode_bytes, &pixel_buf, &alloc_off);
      if (fidx != FASTMAP_SIZE) // table had a free slot for it
      {
        s_fastmap[fidx].key         = tex_addr;
        s_fastmap[fidx].bump_offset = alloc_off;
      }
      // pbuff->addr stays 0 from fast_bump_alloc's memset, so the validity
      // check below naturally treats this as a cache miss and decodes it.
    }
  }
  else
  {
    // QUALITY: O(1) hash map dedup (replaces old O(n) bump_find linear scan).
    if (CACHE_QUALITY())
    {
      pbuff = hash_map_find(orig_tex_addr, &pixel_buf);
      if (pbuff) already_decoded_this_frame = true;
    }
    // EXTRA: still uses the old linear scan so it can be tuned separately.
    else if (CACHE_EXTRA())
    {
      // NOTE: bump_find removed. EXTRA reuses hash_map_find for now;
      // split into its own path later if needed.
      pbuff = hash_map_find(orig_tex_addr, &pixel_buf);
      if (pbuff) already_decoded_this_frame = true;
    }
    // EXTRA_DEBUG: pure bump allocation, always re-decode (no dedup).
    if (!pbuff)
    {
      u32 alloc_start = s_bump_offset; // capture offset BEFORE bump_alloc advances it
      pbuff = bump_alloc(decode_bytes, &pixel_buf);
      // Register the new slot in the hash map so the next draw call
      // referencing the same tex_addr hits the O(1) fast path.
      if (CACHE_QUALITY() || CACHE_EXTRA())
        hash_map_insert(orig_tex_addr, alloc_start);
    }
  }
  u32 *dst_base = pixel_buf;

  // ── Palette TLUT setup (fmt 5 = CI4, fmt 6 = CI8) ──────────────────────────

  // Ring of TLUT staging buffers. GX_LoadTlut only queues a TMEM-load command
  // in the GX FIFO — the GP reads the source buffer asynchronously, possibly
  // long after this function returns. With a single static buffer, the CPU
  // could rewrite it for the next CI4/CI8 palette before the GP consumed the
  // previous load, making two palettized textures drawn back-to-back
  // intermittently share the second palette. 16 slots (512B each, 8KB total)
  // keeps every buffer untouched long enough for the FIFO to drain.
  #define TLUT_RING_SLOTS 16
  static u16 ATTRIBUTE_ALIGN(32) s_tlut_ring[TLUT_RING_SLOTS][256];
  static u32 s_tlut_ring_idx = 0;

  u32 pixel_fmt = mod->tcw.NO_PAL.PixelFmt;
  // Only the CI4/CI8 presets sample through a GX TLUT. Every other 4/8BPP
  // preset (I4/I8 stubs, OPTIMIZED, RGB565 full-decode) bakes the palette
  // into the pixel data, so converting + flushing + GX_LoadTlut'ing here on
  // every single draw was pure waste for them.
  bool tlut_needed = (pixel_fmt == 5) ? (TEXTURE_4BPP_CI4_FAST() || TEXTURE_4BPP_CI4())
                   : (pixel_fmt == 6) ? (TEXTURE_8BPP_CI8_FAST() || TEXTURE_8BPP_CI8())
                   : false;
  if (tlut_needed)
  {
    u32 pal_fmt = PAL_RAM_CTRL & 3; // 0=ARGB1555 1=RGB565 2=ARGB4444 3=ARGB8888
    u8  gx_tlut_fmt = (pal_fmt == 1) ? GX_TL_RGB565 : GX_TL_RGB5A3; // u8, not a named enum

    u32 n_entries, pal_base;
    if (pixel_fmt == 5) { // 4BPP: 16-entry palette
      // PalSelect[5:0] selects one of 64 possible 16-entry groups.
      // Absolute PALETTE_RAM entry index = PalSelect * 16 = PalSelect << 4.
      // (Old code `& ~15u` zeroed bits[3:0], discarding sub-bank selection.)
      n_entries = 16;
      pal_base  = mod->tcw.PAL.PalSelect << 4;
    } else {              // 8BPP: 256-entry palette
      n_entries = 256;
      pal_base  = (mod->tcw.PAL.PalSelect >> 4) << 8;
    }
    u32 *pal = PALETTE_RAM + pal_base;

    // JOJO_FIX(): GX_TLUT0 is the only TLUT slot in use, so re-uploading is
    // only ever needed when the bytes actually bound for it differ from
    // whatever is already sitting in that slot. Cheap source-side checksum
    // lets us skip the conversion loop + DCFlushRange + GX_LoadTlut entirely
    // on a repeat — this is what eliminates the bulk of GX_LoadTlut calls
    // when a scene hammers many draws that reuse the same palette bank
    // back-to-back (e.g. text/sprite glyphs sharing one font palette).
    // Off by default: every other game keeps the old behavior of reloading
    // the TLUT unconditionally on every textured-and-palettized draw.
    bool need_reupload = true;
    static u32 s_last_tlut_checksum = 0xFFFFFFFFu;
    if (JOJO_FIX())
    {
      u32 checksum = pal_fmt ^ (n_entries << 16) ^ pal_base;
      for (u32 i = 0; i < n_entries; i++)
        checksum = checksum * 31u + pal[i];
      need_reupload = (checksum != s_last_tlut_checksum);
      if (need_reupload) s_last_tlut_checksum = checksum;
    }

    if (need_reupload)
    {
      // Each 256-entry row is 512 bytes, a multiple of 32, so every slot in
      // the 32-byte-aligned ring is itself 32-byte aligned (required by both
      // DCFlushRange and GX_InitTlutObj).
      u16 *tlut = s_tlut_ring[s_tlut_ring_idx];
      s_tlut_ring_idx = (s_tlut_ring_idx + 1) & (TLUT_RING_SLOTS - 1);

      for (u32 i = 0; i < n_entries; i++)
      {
        u32 pe = pal[i];
        u16 px;
        switch (pal_fmt)
        {
          case 1:  px = (u16)(pe & 0xFFFF); break;                   // RGB565  -> RGB565
          case 2:  px = ABGR4444((u16)(pe & 0xFFFF)); break;         // ARGB4444-> RGB5A3
          case 3:  {                                                  // ARGB8888-> RGB5A3
            u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
            px = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
            break;
          }
          default: px = ABGR1555((u16)(pe & 0xFFFF)); break;         // ARGB1555-> RGB5A3
        }
        tlut[i] = px;
      }

      DCFlushRange(tlut, n_entries * sizeof(u16));
      GXTlutObj tlut_obj;
      GX_InitTlutObj(&tlut_obj, tlut, gx_tlut_fmt, (u16)n_entries);
      GX_LoadTlut(&tlut_obj, GX_TLUT0);
    }
  }

  // === End of Palette TLUT setup ===


  //// 2. Cache Check ////

  if (already_decoded_this_frame)
  {
    GX_LoadTexObj(&pbuff->tex, GX_TEXMAP0);
    if (SEAM_FIX())
      SeamFixStashTexSize(&pbuff->tex);
    return;
  }

  bool cache_valid = false;

  if (CACHE_VERY_FAST() || plus_skimp)
  {
    if (!CACHE_VERY_FAST_PLUS())
    {
      cache_valid = (*ptex_skmp == 0xDEADBEEF) && (pbuff->addr == tex_addr)
                  && !stride_dyn;
    }
    else if (is_vq)
    {
      // Non-destructive codebook+index fingerprint. The slot is fine for a
      // small VQ texture, but stomping a sentinel at tex_addr would corrupt
      // codebook entry 0 the way VERY_FAST does.
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 fp = *(u32*)&params.vram[orig_tex_addr] ^ *(u32*)&params.vram[idx_addr];
      cache_valid = (pbuff->addr == tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else if (stride_dyn)
    {
      cache_valid = (pbuff->addr == tex_addr)
                  && (pbuff->vq_codebook_w0 == stride_fingerprint(orig_tex_addr, h));
    }
    else
    {
      // Exactly VERY_FAST's test. No shape_key: the slot is address-derived,
      // so the only texture that can alias it is one at the same address, and
      // writing it would have cleared the sentinel.
      cache_valid = (*ptex_skmp == 0xDEADBEEF) && (pbuff->addr == tex_addr);
    }
  }
  else if (CACHE_FAST())
  {
    // VQ: the codebook lives at tex_addr itself, so the VERY_FAST trick of
    // stomping tex_addr with the sentinel would corrupt codebook entry 0,
    // and watching only the codebook misses index-only updates (animated
    // VQ textures reusing the same palette). Use the same non-destructive
    // codebook+index fingerprint as CACHE_QUALITY instead — a couple of
    // extra word reads, still O(1), no VRAM writes for VQ.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      // Same sentinel trick as VERY_FAST, plus a shape key (format, scan
      // order, stride, mip, size bits) so a different texture reusing the
      // same address/stride combo can't be served stale data — this is
      // the gap VERY_FAST plugs by disabling its cache entirely for
      // stride+scan-order textures. Validating the shape instead lets
      // CACHE_FAST keep caching those textures safely.
      u32 shape_key = tex_shape_key(mod, pixel_fmt);
      cache_valid = (*ptex_skmp == 0xDEADBEEF) && (pbuff->addr == tex_addr)
                  && (pbuff->shape_key == shape_key);
    }
  }
  else if (CACHE_NORMAL_FAMILY())
  {
    // VQ: the codebook lives at tex_addr itself, so the VERY_FAST trick of
    // stomping tex_addr with the sentinel would corrupt codebook entry 0,
    // and watching only the codebook misses index-only updates (animated
    // VQ textures reusing the same palette). Use the same non-destructive
    // codebook+index fingerprint as below instead — a couple of extra word
    // reads, still O(1), no VRAM writes for VQ.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      // Same sentinel trick as VERY_FAST, plus a shape key (format, scan
      // order, stride, mip, size bits) so a different texture reusing the
      // same address/stride combo can't be served stale data — this is
      // the gap VERY_FAST plugs by disabling its cache entirely for
      // stride+scan-order textures. Validating the shape instead lets
      // CACHE_NORMAL keep caching those textures safely.
      u32 shape_key = tex_shape_key(mod, pixel_fmt);
      // CACHE_VERY_FAST_PLUS change 2 (v3): shape_key describes the FORMAT, so
      // it cannot tell that the game redrew a stride surface's contents.
      // VERY_FAST and QUALITY answer that by never caching them, which is
      // correct but re-converts 512*h texels on every draw. Hash the surface
      // instead and re-decode only when the hash moves. The sentinel is not
      // used (and not written) for these: it sits on the visible top-left
      // pixels of a scan-order surface, and it would poison probe 0 anyway.
      if (CACHE_VERY_FAST_PLUS() && stride_dyn)
      {
        cache_valid = (pbuff->addr == tex_addr)
                    && (pbuff->shape_key == shape_key)
                    && (pbuff->vq_codebook_w0 == stride_fingerprint(orig_tex_addr, h));
      }
      else
      {
        cache_valid = (*ptex_skmp == 0xDEADBEEF) && (pbuff->addr == tex_addr)
                    && (pbuff->shape_key == shape_key);
      }
    }
  }
  else if (CACHE_QUALITY())
  {
    // QUALITY: corrected sentinel at orig_tex_addr.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == orig_tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      u32 *ptex = (u32*)&params.vram[orig_tex_addr];
      cache_valid = (*ptex == 0xDEADBEEF)
                   && (pbuff->addr == orig_tex_addr)
                   && !(mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder);
    }
  }
  else if (CACHE_EXTRA())
  {
    // EXTRA: duplicate of QUALITY for now.
    if (is_vq)
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      u32 fp = cb_w0 ^ idx_w0;
      cache_valid = (pbuff->addr == orig_tex_addr) && (pbuff->vq_codebook_w0 == fp);
    }
    else
    {
      u32 *ptex = (u32*)&params.vram[orig_tex_addr];
      cache_valid = (*ptex == 0xDEADBEEF)
                   && (pbuff->addr == orig_tex_addr)
                   && !(mod->tcw.NO_PAL.StrideSel && mod->tcw.NO_PAL.ScanOrder);
    }
  }
  // CACHE_EXTRA_DEBUG: cache_valid stays false, always re-decode.

  if (!cache_valid)
  {
    g_texc_decodes++; // see tex_frame_reset()
    // Attribute the miss. Only runs on misses, so the cost is irrelevant.
    if (stride_dyn)                                  g_texc_m_stride++;
    else if (pbuff->addr != tex_addr && pbuff->addr != orig_tex_addr)
                                                     g_texc_m_new++;
    else if (!is_vq && *ptex_skmp != 0xDEADBEEF)     g_texc_m_sent++;
    else                                             g_texc_m_shape++;
    u32 *dst = dst_base;
    VramWork  = (u8*)dst;
    pbuff->has_pal = false;
    pbuff->addr    = (CACHE_VERY_FAST() || CACHE_FAST() || CACHE_NORMAL_FAMILY()) ? tex_addr : orig_tex_addr;

    if (is_vq && !CACHE_VERY_FAST())
    {
      u32 idx_addr = (orig_tex_addr + 2048) & VRAM_MASK;
      if (mod->tcw.NO_PAL.MipMapped)
        idx_addr = (orig_tex_addr + VQMipPoint[mod->tsp.TexU + 3]) & VRAM_MASK;
      u32 cb_w0  = *(u32*)&params.vram[orig_tex_addr];
      u32 idx_w0 = *(u32*)&params.vram[idx_addr];
      pbuff->vq_codebook_w0 = cb_w0 ^ idx_w0;
    }
    else if (CACHE_FAST() || CACHE_NORMAL_FAMILY())
    {
      // Non-VQ: remember the format/scan/stride/mip/size bits alongside the
      // sentinel so a future draw at the same address with a different
      // shape (e.g. a stride-selected texture) can't be served this slot's
      // stale data — see the CACHE_FAST/CACHE_NORMAL validity check above.
      pbuff->shape_key = tex_shape_key(mod, pixel_fmt);
      // PLUS: remember what the stride surface looked like, so the next draw
      // can tell whether the game repainted it. h is still the declared
      // height here — the decode switch below is what rewrites w/h.
      if (CACHE_VERY_FAST_PLUS() && stride_dyn)
        pbuff->vq_codebook_w0 = stride_fingerprint(orig_tex_addr, h);
    }

    if(DEBUG_MESSAGE()) {
      printf("[TEX] addr=%06X fmt=%d vq=%d scan=%d mip=%d w=%d h=%d\n",
        orig_tex_addr, (int)mod->tcw.NO_PAL.PixelFmt, (int)mod->tcw.NO_PAL.VQ_Comp,
        (int)mod->tcw.NO_PAL.ScanOrder, (int)mod->tcw.NO_PAL.MipMapped, w, h);
    }

    //// 3. Format Conversion ////

    switch (mod->tcw.NO_PAL.PixelFmt)
    {
    case 0:
    case 7:
      // 0	1555 value: 1 bit; RGB values: 5 bits each
      // 7	Reserved	Regarded as 1555
      // No VQ_CMPR() branch here, unlike case 1. 1555 could use CMPR's
      // transparent mode, but it regressed on hardware - see the UNPROVEN IDEA
      // note by the VQ_CMPR() macro, and branch `vq_cmpr_1555`.
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
        FMT = GX_TF_RGB565;
      }
      else if (VQ_CMPR() && mod->tcw.NO_PAL.VQ_Comp)
      {
        // 4 bits/texel instead of 16, so the decode fits the address-derived
        // cache slot. 565 is the only format this is offered for: CMPR has no
        // usable alpha, and 1555/4444 decode to RGB5A3, which encode_cmpr_block
        // would misread as RGB565. See texture_VQ_CMPR.
        twidle_tex_vq_cmpr(565);
        FMT = GX_TF_CMPR;
      }
      else
      {
        // verify(tsp.TexU==tsp.TexV);
        twidle_tex(565);
        FMT = GX_TF_RGB565;
      }
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
    {
      // YUV422: 32 bits per 2 pixels (UYVY - U, Y0, V, Y1, 8 bits each).
      // The address of the texture as the game declared it, i.e. before the
      // mipmap fixup below moves it. That is the address the YUV converter
      // would have written, so it is the one to look up.
      const u32 yuv_decl_addr = tex_addr;

      if (mod->tcw.NO_PAL.MipMapped)
      {
        tex_addr += OtherMipPoint[mod->tsp.TexU + 3] * 2; // YUV422: 2 bytes/pixel
        h = w; // mipmapped textures are always square (ignore TexV), as elsewhere
      }
      if (mod->tcw.NO_PAL.StrideSel)
        w = 512;

      // The declared texture width (w) is a power of two, and the source's real
      // per-row pixel pitch can be smaller than it - but ONLY for a texture the
      // YUV converter produced. An FMV frame is written row by row at the
      // video's native width (e.g. 320) with no padding, so decoding it at the
      // declared width shears the picture into repeated slices. A YUV422
      // texture the game uploaded to VRAM itself is stored at its declared
      // width and must be decoded at that width.
      //
      // This used to read the pitch straight out of TA_YUV_TEX_CTRL for EVERY
      // YUV422 texture (bits[5:0] = real_width/16 - 1, bits[13:8] the same for
      // the height). That register belongs to the converter unit and describes
      // whatever it was last programmed for; it says nothing about a texture
      // the game uploaded directly. A game that never programs it leaves it at
      // 0, which reads back as a real size of (0+1)*16 = 16x16: the pitch
      // collapses to 16 pixels and every row from 16 down is zero-filled by the
      // decoders below - Soul Calibur's character select went mostly black.
      // Ask the converter which regions it actually wrote instead.
      u32 yuv_real_w = w, yuv_real_h = h;
      g_yuv_src_stride_override = 0; // 0 = no override, decode at declared size
      g_yuv_src_real_h          = 0;

      if (YUV_STRIDE_ALWAYS())
      {
        // Legacy path, kept only so the old result can be reproduced for A/B
        // on a game where the address gate below turns out to miss.
        u32 cw = ((TA_YUV_TEX_CTRL & 0x3F) + 1) * 16;
        u32 ch = (((TA_YUV_TEX_CTRL >> 8) & 0x3F) + 1) * 16;
        if (cw <= w) { yuv_real_w = cw; g_yuv_src_stride_override = (int)cw; }
        if (ch <= h) { yuv_real_h = ch; g_yuv_src_real_h          = (int)ch; }
      }
      else if (YUV_STRIDE_AUTO())
      {
        u32 cw = 0, ch = 0;
        if (yuv_conv_lookup(yuv_decl_addr, &cw, &ch))
        {
          if (cw && cw <= w) { yuv_real_w = cw; g_yuv_src_stride_override = (int)cw; }
          if (ch && ch <= h) { yuv_real_h = ch; g_yuv_src_real_h          = (int)ch; }
        }
      }

      u8 *yuv_src = &params.vram[tex_addr];

      // FMV Debug
      if (fmv_debug)
      {
        printf("[YUV] ---- new YUV422 texture ----\n");
        printf("[YUV] tex_addr=%06X w=%u h=%u scan=%u mip=%u stride=%u fmv_format=%d "
               "TA_YUV_TEX_CTRL=%08X yuv_stride=%d conv=%d real_w=%u real_h=%u\n",
               tex_addr, w, h, (unsigned)mod->tcw.NO_PAL.ScanOrder,
               (unsigned)mod->tcw.NO_PAL.MipMapped,
               (unsigned)mod->tcw.NO_PAL.StrideSel, get_fmv_format_preset(),
               (u32)TA_YUV_TEX_CTRL, get_yuv_stride_preset(),
               g_yuv_src_stride_override != 0, yuv_real_w, yuv_real_h);

        // Dump raw bytes at the start of the source (first 16 bytes = 4 u32 words).
        u32 *raw32 = (u32*)yuv_src;
        printf("[YUV] raw words: %08X %08X %08X %08X\n",
               raw32[0], raw32[1], raw32[2], raw32[3]);

        // Decode the first 2 pixels exactly the way the planar path does,
        // so we can see if U/V/Y extraction and the YUV422() formula look sane.
        {
          u32 w0 = raw32[0];
          s32 Yu = (w0 >>  0) & 255;  // U
          s32 Y0 = (w0 >>  8) & 255;  // Y0
          s32 Yv = (w0 >> 16) & 255;  // V
          s32 Y1 = (w0 >> 24) & 255;  // Y1
          u16 c0 = (u16)YUV422(Y0, Yu, Yv);
          u16 c1 = (u16)YUV422(Y1, Yu, Yv);
          printf("[YUV] planar-style decode word0: Y0=%d Y1=%d U=%d V=%d\n", Y0, Y1, Yu, Yv);
          printf("[YUV]   -> RGB565 c0=%04X (R=%d G=%d B=%d)  c1=%04X (R=%d G=%d B=%d)\n",
                 c0, (c0>>11)&0x1F, (c0>>5)&0x3F, c0&0x1F,
                 c1, (c1>>11)&0x1F, (c1>>5)&0x3F, c1&0x1F);
        }

        // Decode the first 2x2 block exactly the way the twiddled path does,
        // so we can compare against the planar interpretation above.
        {
          u32 tw = twop(0, 0, w, h) / 4;
          u8 *p  = &yuv_src[tw * 8];
          u32 tw0 = *(u32*)&p[0];
          u32 tw1 = *(u32*)&p[4];
          // Decode through the same helper the real twiddled decoders use, so
          // the log shows what the ACTIVE yuv_twiddle_fix setting produces.
          yuv_tw_block b;
          yuv_tw_fetch(p, YUV_TW_FIX() ? 1 : 0, b);
          printf("[YUV] twiddled-style decode block0: tw_idx=%u raw=%08X %08X tw_fix=%d\n",
                 tw, tw0, tw1, YUV_TW_FIX() ? 1 : 0);
          printf("[YUV]   Y00=%d Y10=%d Y01=%d Y11=%d  U0=%d V0=%d U1=%d V1=%d\n",
                 b.Y00, b.Y10, b.Y01, b.Y11, b.Yu0, b.Yv0, b.Yu1, b.Yv1);
        }

        // ASCII brightness thumbnail of the FULL raw source (every cell is a
        // real sampled pixel, not averaged) — dense enough to actually show
        // structure like dashed lines, instead of the previous 64px-step scan
        // which could easily land entirely between thin dashes every time.
        // This directly answers whether tiling/repetition is already present
        // in the raw VRAM bytes BEFORE any CMPR/RGBA8 encode or GX upload.
        {
          u32 *raw32t = (u32*)yuv_src;
          const int COLS = 100, ROWS = 32;
          printf("[YUV] thumbnail %dx%d (luma 0-9, low..high):\n", COLS, ROWS);
          for (int r = 0; r < ROWS; r++)
          {
            char line[COLS + 1];
            u32 sy = (u32)((u64)r * h / ROWS);
            for (int c = 0; c < COLS; c++)
            {
              u32 sx = (u32)((u64)c * w / COLS) & ~1u; // even: UYVY pixel pairs
              u8 *p = &((u8*)raw32t)[(sy * w + sx) * 2];
              s32 yy = p[1]; // luma of the first pixel in the pair (byte1 = Y0)
              int level = (yy * 10) / 256; if (level > 9) level = 9; if (level < 0) level = 0;
              line[c] = (char)('0' + level);
            }
            line[COLS] = 0;
            printf("[YUV]  %s\n", line);
          }
        }
      }

      if (FMV_FORMAT_RGBA8())
      {
        // ---- RGBA8 path ----
        if (mod->tcw.NO_PAL.ScanOrder)
        {
          // Planar (linear scan-order) source.
          YUV422_to_RGBA8_Planar(yuv_src, w, h, VramWork);
        }
        else
        {
          // Twiddled source.
          YUV422_to_RGBA8_Twiddled(yuv_src, w, h, VramWork);
        }
        FMT = GX_TF_RGBA8;
      }
      else if (FMV_FORMAT_RGB565())
      {
        // ---- RGB565 path — cheapest per-pixel cost (direct store, no
        // block encoding, no 8-bit channel expansion) ----
        if (mod->tcw.NO_PAL.ScanOrder)
        {
          // Planar (linear scan-order) source.
          YUV422_to_RGB565_Planar(yuv_src, w, h, VramWork);
        }
        else
        {
          // Twiddled source.
          YUV422_to_RGB565_Twiddled(yuv_src, w, h, VramWork);
        }
        FMT = GX_TF_RGB565;
      }
      else
      {
        // ---- CMPR (DXT1) path — default (fmv_format == 0) ----
        if (mod->tcw.NO_PAL.ScanOrder)
        {
          // Planar (linear scan-order) source.
          YUV422_to_CMPR_Planar(yuv_src, w, h, VramWork);
        }
        else
        {
          // Twiddled source.
          YUV422_to_CMPR_Twiddled(yuv_src, w, h, VramWork);
        }
        FMT = GX_TF_CMPR;
      }

      if (DEBUG_MESSAGE())
      {
        // Dump the first few bytes of the ENCODED output too, so we can see
        // if the CMPR/RGBA8/RGB565 packing itself produced something sane.
        u8 *out = (u8*)VramWork;
        if (FMT == GX_TF_CMPR)
        {
          printf("[YUV] CMPR block0 bytes: %02X%02X %02X%02X  sel=%02X%02X%02X%02X\n",
                 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
        }
        else if (FMT == GX_TF_RGBA8)
        {
          printf("[YUV] RGBA8 tile0 AR bytes: %02X%02X %02X%02X %02X%02X %02X%02X\n",
                 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
          printf("[YUV] RGBA8 tile0 GB bytes: %02X%02X %02X%02X %02X%02X %02X%02X\n",
                 out[32], out[33], out[34], out[35], out[36], out[37], out[38], out[39]);
        }
        else // GX_TF_RGB565
        {
          printf("[YUV] RGB565 tile0 bytes: %02X%02X %02X%02X %02X%02X %02X%02X\n",
                 out[0], out[1], out[2], out[3], out[4], out[5], out[6], out[7]);
        }
      }
      break;
    }
      // 4	Bump Map	16 bits/pixel; S value: 8 bits; R value: 8 bits
    case 5: 
    {
      if(TEXTURE_4BPP_I4_STUB()){ // I4 Stub (initial code)
        // 5	4 BPP Palette	Palette texture with 4 bits/pixel
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        SetupPaletteForTexture(mod->tcw.PAL.PalSelect << 4, 16);

        FMT = GX_TF_I4; // Intensity 4 bit (Grayscale)
        }
      else if(TEXTURE_4BPP_OPTIMIZED()){ // BEST: LUT-baked palette decode -> GX RGB565/RGB5A3
        // Same accuracy as a full palette decode (correct ARGB8888/4444/1555
        // palettes, correct PalSelect bank — fixes counters/HUDs that CI4+TLUT
        // can't render right), but the palette is converted to GX format ONCE
        // per texture into a 16-entry u16 LUT. The inner loop is then a pure
        // table fetch: no per-pixel format switch, no conversion macros.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bit 5, so any game using a 4BPP palette bank >= 32 wrongly
        // took the old linear branch and got scrambled output. Real DC
        // hardware stores palette textures twiddled, always — so this path
        // now decodes twiddled unconditionally, no branch needed.
        //
        // Other speedups vs the previous OPTIMIZED code:
        //   - Twiddle tables used via direct cached pointers (the old code
        //     memcpy'd 2x 4 KB of tables on EVERY texture decode).
        //   - Morton bit0 is Y's LSB (twop() interleaves y first), so rows
        //     y and y+1 share each source byte:
        //       nib(x, y even) = ty | tx    -> LOW  nibble
        //       nib(x, y+1)    = nib + 1    -> HIGH nibble, SAME byte
        //     Decoding two rows per pass halves the reads + table lookups.
        //   - GX tile addressing: the per-row tile/sub-tile math (y>>2, y&3,
        //     w/4) is hoisted out of the x loop.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        u32  pal_fmt  = PAL_RAM_CTRL & 3;
        u32  pal_base = mod->tcw.PAL.PalSelect << 4; // PalSelect selects a 16-entry bank
        u32 *pal      = PALETTE_RAM + pal_base;

        FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

        // Bake the palette: 16 conversions total instead of w*h.
        u16 gx_pal[16];
        switch (pal_fmt)
        {
          case 1: // RGB565 -> RGB565 (direct)
            for (u32 i = 0; i < 16; i++) gx_pal[i] = (u16)(pal[i] & 0xFFFF);
            break;
          case 2: // ARGB4444 -> RGB5A3
            for (u32 i = 0; i < 16; i++) gx_pal[i] = ABGR4444((u16)(pal[i] & 0xFFFF));
            break;
          case 3: // ARGB8888 -> RGB5A3
            for (u32 i = 0; i < 16; i++)
            {
              u32 pe = pal[i];
              u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
              gx_pal[i] = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
            }
            break;
          default: // ARGB1555 -> RGB5A3
            for (u32 i = 0; i < 16; i++) gx_pal[i] = ABGR1555((u16)(pal[i] & 0xFFFF));
            break;
        }

        u8  *src     = (u8 *)&params.vram[tex_addr];
        u16 *dst16   = (u16 *)VramWork;
        u32  tiles_x = w / 4; // GX 16bpp tile = 4x4 pixels

        // Twiddled decode (unconditional — see note above).
        const u32 *table_x, *table_y;
        get_twiddle_axis_tables(w, h, &table_x, &table_y);

        for (u32 y = 0; y < h; y += 2) // h is pow2 >= 8, always even
        {
          u32 row_base0 = ((y    ) >> 2) * tiles_x * 16 + ((y    ) & 3) * 4;
          u32 row_base1 = ((y + 1) >> 2) * tiles_x * 16 + ((y + 1) & 3) * 4;
          u32 ty        = table_y[y]; // even y -> Morton bit0 clear
          for (u32 x = 0; x < w; x++)
          {
            u32 nib = ty | table_x[x];     // nibble index for (x, y)
            u8  raw = src[(nib >> 1) ^ 3]; // ^3: 32-bit BE VRAM correction
            u32 col = (x >> 2) * 16 + (x & 3);
            dst16[row_base0 + col] = gx_pal[raw & 0xF]; // row y   (low nibble)
            dst16[row_base1 + col] = gx_pal[raw >> 4];  // row y+1 (high nibble)
          }
        }

        pbuff->has_pal = false; // palette baked in, GPU doesn't need a TLUT
      }
      else if (TEXTURE_4BPP_CI4_FAST()) { // 4BPP palette -> GX_TF_CI4 (Fast)
        // TLUT already loaded above — only the index nibbles are reordered here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bit 5, so any game using a 4BPP palette bank >= 32 wrongly
        // took the old linear branch and got scrambled output. Real DC
        // hardware stores palette textures twiddled, always — so this path
        // now decodes twiddled unconditionally.
        //
        // Paired-nibble loop: 2 pixels per iteration, both nibbles of each
        // GX CI4 output byte filled in a single store (no ci4_prel calls).
        // Per-axis Morton tables come from the shared cache (O(w+h) once per
        // size, zero per-decode rebuild/copy).
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x += 2)
            {
              u32 tw0  = ty | table_x[x];                         // nibble index for x
              u32 tw1  = ty | table_x[x + 1];                     // nibble index for x+1
              u8  raw0 = src[(tw0 >> 1) ^ 3];                     // ^3: 32-bit BE correction
              u8  raw1 = src[(tw1 >> 1) ^ 3];
              u8  idx0 = (tw0 & 1) ? (raw0 >> 4) : (raw0 & 0xF); // palette index for x
              u8  idx1 = (tw1 & 1) ? (raw1 >> 4) : (raw1 & 0xF); // palette index for x+1

              // Pack both indices into one GX CI4 tile byte
              // (even -> high nibble, odd -> low nibble).
              u32 tile     = (y / 8) * (w / 8) + (x / 8);
              u32 nibble   = tile * 64 + (y % 8) * 8 + (x % 8); // points at even pixel
              idst[nibble >> 1] = (u8)((idx0 << 4) | (idx1 & 0x0F));
            }
          }
        }

        FMT = GX_TF_CI4;
        pbuff->has_pal = true;
      }

      else if (TEXTURE_4BPP_CI4()) { // 4BPP palette -> GX_TF_CI4 (Accurate)
        // Untwiddle index nibbles into GX CI4 block layout via ci4_prel.
        // TLUT already loaded above — only index data written here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bit 5, so any game using a 4BPP palette bank >= 32 wrongly
        // took the old linear branch and got scrambled output. Real DC
        // hardware stores palette textures twiddled, always — so this path
        // now decodes twiddled unconditionally.
        //
        // DC nibble convention: even pixel -> LOW nibble, odd pixel -> HIGH nibble.
        // VRAM byte addressing: DC stores data in 32-bit little-endian words,
        // but the Wii's big-endian SH4 emulator stores them with full 32-bit
        // reversal (DC byte B lives at vram[B ^ 3]).
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;
          memset(idst, 0, w * h / 2); // clear required: nibbles are OR'd in

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u32 tw_nibble = ty | table_x[x];                          // nibble index
              u8  raw       = src[(tw_nibble >> 1) ^ 3];                // ^3: BE correction
              u8  idx       = (tw_nibble & 1) ? (raw >> 4) : (raw & 0xF);
              ci4_prel(idst, x, y, w, idx);
            }
          }
        }

        FMT = GX_TF_CI4;
        pbuff->has_pal = true;
      }
      else if (TEXTURE_4BPP_RGB565()) {
          verify(mod->tcw.PAL.VQ_Comp == 0);
          if (mod->tcw.NO_PAL.MipMapped)
              tex_addr += OtherMipPoint[mod->tsp.TexU + 3] / 2; // 4bpp: texels/2 bytes

          u32  pal_fmt  = PAL_RAM_CTRL & 3;
          u32  pal_base = mod->tcw.PAL.PalSelect & ~15u;
          u32 *pal      = PALETTE_RAM + pal_base;

          FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

          u8  *src = (u8 *)&params.vram[tex_addr];
          u16 *dst = (u16 *)VramWork;

          // Precompute per-axis Morton contributions once (O(w+h)) instead of
          // calling twop(x,y,w,h) per pixel (O(w*h) — same fix applied to the
          // other 4BPP paths above; this loop was still doing it per pixel).
          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          // We pull the switch OUTSIDE the loops.
          // The compiler can now optimize each loop specifically for the format.
          switch (pal_fmt) {
              case 1: // RGB565
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          dst[GX_TexOffs(x, y, w)] = (u16)(pal[idx] & 0xFFFF);
                      }
                  }
                  break;

              case 2: // ARGB4444
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          dst[GX_TexOffs(x, y, w)] = ABGR4444((u16)(pal[idx] & 0xFFFF));
                      }
                  }
                  break;

              case 3: // ARGB8888 -> RGB5A3
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          u32 pe  = pal[idx];
                          u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
                          dst[GX_TexOffs(x, y, w)] = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
                      }
                  }
                  break;

              default: // ARGB1555
                  for (u32 y = 0; y < h; y++) {
                      u32 ty = table_y[y];
                      for (u32 x = 0; x < w; x++) {
                          u32 tw_nibble = ty | table_x[x];
                          u8  raw = src[(tw_nibble >> 1) ^ 1];
                          u8  idx = (tw_nibble & 1) ? (raw & 0xF) : (raw >> 4);
                          dst[GX_TexOffs(x, y, w)] = ABGR1555((u16)(pal[idx] & 0xFFFF));
                      }
                  }
                  break;
          }

          pbuff->has_pal = false; // We baked the palette in, so the GPU doesn't need it.
      }
      break;
    }
    case 6: // 8BPP Palette: 8 bits per pixel, 256-entry palette.
    {
      if(TEXTURE_8BPP_I8_STUB()) {// I8 Stub (initial code)
        // 8 BPP Palette	Palette texture with 8 bits/pixel
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        SetupPaletteForTexture(mod->tcw.PAL.PalSelect << 4, 256);

        FMT = GX_TF_I8; // Intensity 8 bit (Grayscale)

      }
      else if(TEXTURE_8BPP_OPTIMIZED()) {
        // BEST: LUT-baked palette decode -> GX RGB565/RGB5A3.
        // Same output as TEXTURE_8BPP_RGB565 (the path that renders the
        // Crazy Taxi / Bomberman counters correctly) but faster:
        //   - Palette converted ONCE into a 256-entry u16 LUT: the inner loop
        //     is gx_pal[idx], no per-pixel format switch / ARGB8888 unpack.
        //   - Twiddle tables via direct cached pointers (old code memcpy'd
        //     8 KB of tables per decode), GX tile row addressing hoisted.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bits 4-5, so any game using an 8BPP palette bank 2 or 3
        // wrongly took the old linear branch and got scrambled output. Real
        // DC hardware stores palette textures twiddled, always — so this
        // path now decodes twiddled unconditionally.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        u32  pal_fmt  = PAL_RAM_CTRL & 3;
        u32  pal_base = (mod->tcw.PAL.PalSelect >> 4) << 8; // bits[5:4] pick a 256-entry bank
        u32 *pal      = PALETTE_RAM + pal_base;

        FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

        // Bake the palette: 256 conversions total instead of w*h.
        static u16 gx_pal[256]; // static: keep it off the stack
        switch (pal_fmt)
        {
          case 1: // RGB565 -> RGB565 (direct)
            for (u32 i = 0; i < 256; i++) gx_pal[i] = (u16)(pal[i] & 0xFFFF);
            break;
          case 2: // ARGB4444 -> RGB5A3
            for (u32 i = 0; i < 256; i++) gx_pal[i] = ABGR4444((u16)(pal[i] & 0xFFFF));
            break;
          case 3: // ARGB8888 -> RGB5A3
            for (u32 i = 0; i < 256; i++)
            {
              u32 pe = pal[i];
              u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
              gx_pal[i] = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
            }
            break;
          default: // ARGB1555 -> RGB5A3
            for (u32 i = 0; i < 256; i++) gx_pal[i] = ABGR1555((u16)(pal[i] & 0xFFFF));
            break;
        }

        u8  *src     = (u8 *)&params.vram[tex_addr];
        u16 *dst16   = (u16 *)VramWork;
        u32  tiles_x = w / 4; // GX 16bpp tile = 4x4 pixels

        // Twiddled decode (unconditional — see note above).
        // 1 byte = 1 pixel in Morton order.
        const u32 *table_x, *table_y;
        get_twiddle_axis_tables(w, h, &table_x, &table_y);

        for (u32 y = 0; y < h; y++)
        {
          u32 row_base = (y >> 2) * tiles_x * 16 + (y & 3) * 4;
          u32 ty       = table_y[y];
          for (u32 x = 0; x < w; x++)
          {
            u8 idx = src[(ty | table_x[x]) ^ 3]; // ^3: 32-bit BE VRAM correction
            dst16[row_base + (x >> 2) * 16 + (x & 3)] = gx_pal[idx];
          }
        }

        pbuff->has_pal = false; // palette baked in, GPU doesn't need a TLUT
      }
      else if(TEXTURE_8BPP_CI8_FAST()) {
        // 8BPP palette -> GX_TF_CI8  [FAST path]
        // TLUT already loaded above — only index data written here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bits 4-5, so any game using an 8BPP palette bank 2 or 3
        // wrongly took the old linear branch and got scrambled output. Real
        // DC hardware stores palette textures twiddled, always — so this
        // path now decodes twiddled unconditionally.
        //
        // Inlined tile write (no ci8_prel call); y_tile_base hoisted out of
        // the x loop; per-axis Morton tables from the shared cache.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          u32 tiles_x = w / 8;
          for (u32 y = 0; y < h; y++)
          {
            u32 y_tile_base = (y / 4) * tiles_x * 32 + (y % 4) * 8;
            u32 ty          = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u8  idx      = src[(ty | table_x[x]) ^ 3]; // ^3: 32-bit BE correction
              u32 byte_off = y_tile_base + (x / 8) * 32 + (x % 8);
              idst[byte_off] = idx;
            }
          }
        }

        FMT = GX_TF_CI8;
        pbuff->has_pal = true;
      }
      else if(TEXTURE_8BPP_CI8()) {
        // 8BPP palette -> GX_TF_CI8  [ACCURATE path]
        // Untwiddle index bytes into GX CI8 block layout via ci8_prel.
        // TLUT already loaded above — only index data written here.
        //
        // Always twiddled: palette textures have NO ScanOrder bit. TCW bits
        // 21-26 are PAL.PalSelect for paletted formats (see ta_structs.h),
        // NOT Reserved/StrideSel/ScanOrder — those only exist in the NO_PAL
        // union member. Reading NO_PAL.ScanOrder here was actually reading
        // PalSelect bits 4-5, so any game using an 8BPP palette bank 2 or 3
        // wrongly took the old linear branch and got scrambled output. Real
        // DC hardware stores palette textures twiddled, always — so this
        // path now decodes twiddled unconditionally.
        //
        // ^3 = full 32-bit BE word reversal (DC byte B lives at vram[B ^ 3]).
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        {
          u8 *src  = (u8 *)&params.vram[tex_addr];
          u8 *idst = (u8 *)VramWork;

          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u8 idx = src[(ty | table_x[x]) ^ 3]; // ^3: 32-bit BE correction
              ci8_prel(idst, x, y, w, idx);
            }
          }
        }

        FMT = GX_TF_CI8;
        pbuff->has_pal = true;
      }
      else if (TEXTURE_8BPP_RGB565()){
        // Fully decode each indexed pixel into a GX 16bpp pixel.
        verify(mod->tcw.PAL.VQ_Comp == 0);
        if (mod->tcw.NO_PAL.MipMapped)
          tex_addr += OtherMipPoint[mod->tsp.TexU + 3]; // 8bpp: texels == bytes

        {
          u32  pal_fmt  = PAL_RAM_CTRL & 3;
          // PalSelect[5:4] selects the 256-entry bank (each bank = 256 u32 entries)
          u32  pal_base = (mod->tcw.PAL.PalSelect >> 4) << 8;
          u32 *pal      = PALETTE_RAM + pal_base;

          FMT = (pal_fmt == 1) ? GX_TF_RGB565 : GX_TF_RGB5A3;

          u8  *src = (u8 *)&params.vram[tex_addr];
          u16 *dst = (u16 *)VramWork;

          // 8BPP twiddled: 1 byte = 1 pixel in Morton order. Precompute
          // per-axis Morton contributions once (O(w+h)) instead of calling
          // twop(x,y,w,h) per pixel (O(w*h) — same fix as the 4BPP paths above).
          const u32 *table_x, *table_y;
          get_twiddle_axis_tables(w, h, &table_x, &table_y);

          for (u32 y = 0; y < h; y++)
          {
            u32 ty = table_y[y];
            for (u32 x = 0; x < w; x++)
            {
              u32 tw  = ty | table_x[x];
              u8  idx = src[tw ^ 3];  // ^3: full 32-bit BE word correction (matches CI8/I8 paths)

              u32 pe = pal[idx];
              u16 px;
              switch (pal_fmt)
              {
                case 1:  px = (u16)(pe & 0xFFFF); break;                    // RGB565  → RGB565
                case 2:  px = ABGR4444((u16)(pe & 0xFFFF)); break;          // ARGB4444→ RGB5A3
                case 3:                                                      // ARGB8888→ RGB5A3
                {
                  u8 a=(pe>>24)&0xFF, r=(pe>>16)&0xFF, g=(pe>>8)&0xFF, b=pe&0xFF;
                  px = (u16)(((a>>5)<<12)|((r>>4)<<8)|((g>>4)<<4)|(b>>4));
                  break;
                }
                default: px = ABGR1555((u16)(pe & 0xFFFF)); break;          // ARGB1555→ RGB5A3
              }
              dst[GX_TexOffs(x, y, w)] = px;
            }
          }
        }
      }
      break;
    }
    default:
      printf("Unhandled texture\n");
      // memset(temp_tex_buffer,0xFFEFCFAF,w*h*4);
    }

    if (texVQ)
    {
      // texture_VQ now fully decompresses into standard 16bpp RGB pixels —
      // same format as texture_TW. No CI8/TLUT. w,h stay full size.
      pbuff->has_pal = false;
    }

    // Generate a GX mip chain from the decoded base level (see the helpers
    // above). Only for mipmapped textures whose decoded form is plain tiled
    // 16bpp — that covers 1555/565/4444, twiddled + VQ, and the palette-baked
    // CI presets. Skipped for:
    //   * CACHE_VERY_FAST — skimp slots have no size accounting, and the
    //     chain's extra ~1/3 would raise the preset's known overlap risk;
    //   * index-only CI4/CI8 and I4/I8 outputs — palette indices can't be
    //     averaged (and intensity mips aren't worth a third code path);
    //   * YUV422 (PixelFmt 3) — FMV surfaces, allocated without chain room;
    //   * MIPMAP_OFF (default) — legacy base-level-only path, zero overhead.
    u32 mip_levels = 0;
    u32 mip_bytes  = 0;
    if (!MIPMAP_OFF()
        && mod->tcw.NO_PAL.MipMapped && !CACHE_VERY_FAST() && !pbuff->has_pal
        && (FMT == GX_TF_RGB565 || FMT == GX_TF_RGB5A3)
        && mod->tcw.NO_PAL.PixelFmt != 3
        && w == h && w >= 8)
    {
      mip_levels = GenerateMipChain16((u16*)dst, w, FMT == GX_TF_RGB565);
      mip_bytes  = MipChainExtraBytes16(w);
    }

    //// 4. Hardware Handover ////

    void *gx_pixels = (void*)dst;

    // Flush pixel data from CPU cache to RAM for GX.
    {
      u32 flush_sz;
      if      (FMT == GX_TF_CI4 || FMT == GX_TF_I4) flush_sz = w * h / 2;
      else if (FMT == GX_TF_CI8 || FMT == GX_TF_I8) flush_sz = w * h;
      else if (FMT == GX_TF_RGBA8)                   flush_sz = w * h * 4; // 32bpp
      else if (FMT == GX_TF_CMPR)                    flush_sz = w * h / 2; // DXT1 4bpp
      else                                            flush_sz = w * h * 2; // 16bpp default
      flush_sz += mip_bytes; // generated GX mip chain lives right after the base level
      DCFlushRange(gx_pixels, (flush_sz + 31) & ~31u);
    }

    if (pbuff->has_pal)
    {
      GX_InitTexObjCI(&pbuff->tex, gx_pixels, (u16)w, (u16)h, (u8)FMT,
                      (u8)TexUV(mod->tsp.FlipU, mod->tsp.ClampU),
                      (u8)TexUV(mod->tsp.FlipV, mod->tsp.ClampV),
                      GX_FALSE, GX_TLUT0);
    }
    else
    {
      // GX_TRUE requires the mip levels packed sequentially in GX block
      // format right after the base level — exactly what GenerateMipChain16
      // produced when mip_levels > 0. Without a generated chain (indexed
      // formats, VERY_FAST, non-mipmapped), keep GX_FALSE: the DC mip offset
      // already selected the largest level and GX samples it directly.
      GX_InitTexObj(&pbuff->tex, gx_pixels, w, h, FMT,
                    TexUV(mod->tsp.FlipU, mod->tsp.ClampU),
                    TexUV(mod->tsp.FlipV, mod->tsp.ClampV),
                    mip_levels ? GX_TRUE : GX_FALSE);
    }

    if (mip_levels)
    {
      // MIPMAP_FAST uses GX_LIN_MIP_NEAR: bilinear from the nearest mip level,
      // a single texture cycle on Hollywood — near-free. MIPMAP_TRILINEAR uses
      // GX_LIN_MIP_LIN, which takes two cycles per texel and halves texture
      // fill rate. maxlod clamps to the smallest level we produced (4x4).
      // Anisotropy (opt-in, GX_ANISO_1 by default) is only iterated by the TX
      // unit when minfilt is GX_LIN_MIP_LIN, so asking for 2X/4X promotes the
      // min filter to trilinear here rather than silently doing nothing.
      u8 mip_min_filt;
      if (min_filt == GX_NEAR)
        mip_min_filt = GX_NEAR_MIP_NEAR;
      else
        mip_min_filt = (MIPMAP_TRILINEAR() || aniso != GX_ANISO_1) ? GX_LIN_MIP_LIN
                                                                  : GX_LIN_MIP_NEAR;
      GX_InitTexObjLOD(&pbuff->tex, mip_min_filt, mag_filt,
                    0.0f, (f32)mip_levels, lod_bias,
                    bias_clamp, edge_lod, aniso);
    }
    else
    {
      GX_InitTexObjLOD(&pbuff->tex, min_filt, mag_filt,
                    0.0f, 10.0f, lod_bias,
                    bias_clamp, edge_lod, aniso);
    }

    // Write sentinel.
    if (CACHE_VERY_FAST())
    {
      *ptex_skmp = 0xDEADBEEF;
    }
    else if (CACHE_VERY_FAST_PLUS())
    {
      // Same rule as FAST/NORMAL below for VQ (fingerprint only, no VRAM
      // write), but non-VQ goes through ptex_skmp — the pointer captured from
      // the ORIGINAL tex_addr before the mip-level decoders advanced it. That
      // is the exact word the validity check reads, so a mipmapped texture
      // finally registers a cache hit instead of re-decoding on every draw.
      // It also lands on the SMALLEST mip level (DC mip chains run small ->
      // large) rather than on the full-size level the decoder actually
      // samples, so the stomp is invisible as well as effective.
      if (!is_vq && !stride_dyn)
        *ptex_skmp = 0xDEADBEEF;
    }
    else if (CACHE_FAST() || CACHE_NORMAL())
    {
      // VQ validity is tracked purely via the codebook+index fingerprint
      // (stored above) — no VRAM write, so the codebook/index data the
      // decoder just read stays intact. Only stomp the sentinel for
      // non-VQ formats. NOTE: for a MIPMAPPED non-VQ texture tex_addr has
      // already been advanced past the small mip levels by the decoder, so
      // this lands somewhere the validity check never looks and the texture
      // re-decodes on every draw — CACHE_VERY_FAST_PLUS above is the gated
      // fix for that; this path is left as-is so `fast`/`normal` keep the
      // exact behaviour every tuned game preset was tested against.
      if (!is_vq)
      {
        u32 *ptex_fast = (u32*)&params.vram[tex_addr];
        *ptex_fast = 0xDEADBEEF;
      }
    }
    else if (!is_vq)
    {
      u32 *ptex = (u32*)&params.vram[orig_tex_addr];
      *ptex = 0xDEADBEEF;
    }

    if(DEBUG_MESSAGE()){
      printf("Texture:%d %d %dx%d %08X --> %08X\n",
        mod->tcw.NO_PAL.PixelFmt, mod->tcw.NO_PAL.ScanOrder,
        8 << mod->tsp.TexU, 8 << mod->tsp.TexV,
        orig_tex_addr, (u32)gx_pixels);
    }

    // TMEM_CACHE(): DoRender no longer wipes the texture cache each frame, so
    // TMEM may hold stale lines for the buffer just re-decoded (same address,
    // old pixels). Invalidate here — the command lands in the FIFO before the
    // draw that samples this texture, and frames with no decodes pay nothing.
    if (TMEM_CACHE())
      GX_InvalidateTexAll();
  }

  GX_LoadTexObj(&pbuff->tex, GX_TEXMAP0);
  if (SEAM_FIX())
    SeamFixStashTexSize(&pbuff->tex);
}

static bool s_did_3d_render = false;

// Set by SPG.cpp's CalculateSync() from SPG_CONTROL/SCALER_CTL whenever the
// video timing changes. 240p (non-interlaced NTSC/PAL) modes halve the active
// vertical resolution (scale 0.5), and with the x_scaler preset on,
// SCALER_CTL's hscale bit means the TA renders at DOUBLE width — 1280 — that
// the video scaler halves 2:1 on framebuffer write (scale_x 2.0; Omicron,
// Wacky Races). DoRender() sizes the projected canvas from these.
static float g_fb_scale_x = 1.0f;
static float g_fb_scale_y = 1.0f;

void SetFbScale(float x, float y)
{
  g_fb_scale_x = x;
  g_fb_scale_y = y;
}

// ============================
// Framebuffer-present tracking (devcast-style, magic-word variant)
// ============================
//
// Some titles (notably MIL-CD / CDI selfboot 2D content) write their image
// directly into VRAM and present it via the SPG scanout, without ever issuing
// a PVR RENDER_START.  In that case StartRender() never runs and the screen
// would stay stale.  devcast handles this by presenting the VRAM framebuffer
// at vblank whenever no 3D render happened that frame (render_called / fb_dirty
// gate in Renderer_if.cpp / RenderFramebuffer()).
//
// Here we use an in-VRAM magic word instead of a host-side flag so the signal
// is tied to the actual framebuffer contents:
//
//   * When the PVR produces a frame (DoRender), we stamp FB_MAGIC into VRAM at
//     the framebuffer *write* start address (FB_W_SOF1).  This marks "the image
//     currently in VRAM was produced by the PVR and is already on screen".
//   * At vblank (VBlank) we read the magic from the framebuffer *read* start
//     address (FB_R_SOF1).  If it is still there, the on-screen image is the
//     PVR's render and there is nothing new to show.  If it is gone, the CPU
//     (or any non-PVR writer) has overwritten the framebuffer, so we present
//     the VRAM framebuffer contents.
//
// The magic is written at the very first pixel of the framebuffer; a real frame
// will normally overwrite pixel 0, which is exactly the "needs present" signal.
#define FB_PRESENT_MAGIC 0xDC4F0FB0u   // "DC4 0FB" – framebuffer-presented marker

// Linear 32-bit VRAM accessor (DC byte address -> interleaved 64-bit bank).
// Mirrors vri()/vrf() but lets us write as well as read the magic word.
static INLINE u32 fb_vram_read32(u32 addr)
{
  return *(u32 *)&params.vram[fast_ConvOffset32toOffset64(addr)];
}

static INLINE void fb_vram_write32(u32 addr, u32 val)
{
  *(u32 *)&params.vram[fast_ConvOffset32toOffset64(addr)] = val;
}

// Stamp the "already presented" marker into the framebuffer.  Called:
//   * after a PVR 3D render, at the write origin (FB_W_SOF1) — the address the
//     PVR just produced into and that the game will scan out from;
//   * after a VRAM framebuffer present, at the read origin (FB_R_SOF1) — the
//     address that was just shown.
// In both cases the marker means "the current framebuffer contents are on
// screen".  Any subsequent CPU/2D write to the framebuffer overwrites pixel 0
// (and thus the marker), which is exactly the "needs present" signal.
static INLINE void fb_stamp_present_magic_at(u32 addr)
{
  fb_vram_write32(addr & 0x00FFFFFF, FB_PRESENT_MAGIC);
}

static INLINE void fb_stamp_present_magic()
{
  fb_stamp_present_magic_at(FB_W_SOF1);
}

// True when the framebuffer read origin no longer holds our marker, i.e. the
// framebuffer was written by something other than the present path and the new
// contents still need to be shown.
static INLINE bool fb_needs_present()
{
  return fb_vram_read32(FB_R_SOF1 & 0x00FFFFFF) != FB_PRESENT_MAGIC;
}

void PresentFramebuffer();

// ============================
// Frame-skip state
// ============================

// FRAMESKIP_AUTO() / FRAMESKIP_AUTO_MAX(): drop renders while the emulator is
// behind real time, so EMULATED time keeps advancing at 100% speed even when
// the display rate collapses. The game's clocks, music and physics stay on
// schedule; only the number of frames you see goes down.
//
// Two earlier attempts and why they failed, so neither gets reinvented:
//
//   1. Timing DoRender() alone against a fixed 1/60 s budget. Never fired:
//      with ASYNC_RENDER() (default ON) DoRender() returns right after queuing
//      the display copy — both blocking GX_DrawDone() calls are skipped, and
//      the gx_sync_pending() wait at its top is a no-op because the previous
//      frame's GPU work drained while the SH4 was emulating — so the measured
//      span was CPU submit time only. It also cannot see a game that is slow
//      because of SH4/AICA emulation rather than rendering.
//
//   2. Comparing the wall-clock time between two skip decisions against the
//      emulated time between them (vblanks x frame period) and skipping when
//      the RATIO exceeded ~1.1. This fired, but it under-skipped by
//      construction, which is the "auto mode lowers the framerate but doesn't
//      restore speed" symptom. A skipped frame is cheap, so the very next
//      interval measures near 1.0 and the controller immediately stops
//      skipping. Take a 25 ms frame with a 16.7 ms target and 9 ms of render
//      in it: it settles into render/skip/render/skip, average 20.5 ms — a
//      stable 81% speed that it is perfectly happy with, because no single
//      interval ever looks bad for two frames running.
//
// The fix is to stop measuring one interval at a time. spg_PaceFrame() (SPG.cpp)
// keeps an ABSOLUTE real-time schedule — one deadline per emulated vblank,
// advanced by exactly one frame period, never re-anchored to the wake time —
// and publishes the CUMULATIVE deviation in spg_PaceErrorSec. Debt built up by
// heavy frames survives the cheap skipped frames that follow, so the skipper
// keeps dropping frames until the debt is actually repaid rather than until
// one frame happens to look cheap. In the example above it converges on the
// 2-in-3 skip rate that genuinely holds 100%, instead of stalling at 81%.
//
// The decision itself is Yabause-style bang-bang: skip while the debt exceeds
// a deadband, with a hard cap on consecutive skips so the screen can never
// freeze. The deadband is what stops it alternating skip/draw every frame on
// timer jitter; the cap sets the worst-case display rate.
//
// Sampling lives at the vblank, not here: spg_PaceFrame() runs once per
// EMULATED frame whether or not anything was drawn, so dropping renders cannot
// starve the controller of samples. This function only consumes the verdict.
//
// If a game still can't hold 100% with AUTO MAX, rendering is not the
// bottleneck — the SH4/AICA emulation alone is over budget, and frame skipping
// has nothing left to give. The lever for that case is the sh4_clock preset
// (guest underclock), not this one.

#define AUTO_DEADBAND_FRAC     0.50 // AUTO: tolerate half a frame of lateness
#define AUTO_MAX_DEADBAND_FRAC 0.25 // AUTO MAX: tighter, reacts a frame sooner
#define AUTO_CONSEC_CAP        3    // AUTO: >= 1 render in 4  -> 15 FPS floor @60Hz
#define AUTO_MAX_CONSEC_CAP    9    // AUTO MAX: >= 1 render in 10 -> 6 FPS floor

static int s_auto_consec_skips = 0;  // renders dropped since the last one that ran

static bool ShouldSkipFrame() // Returns true when the current frame should be dropped
{
    if (NO_FRAMESKIP())
        return false;

    if (FRAMESKIP_1())
    {
        frame_counter = (frame_counter + 1) & 1;
        return frame_counter != 0;
    }
    if (FRAMESKIP_2())
    {
        frame_counter = (frame_counter + 1) % 3;
        return frame_counter != 0;
    }
    if (FRAMESKIP_AUTO() || FRAMESKIP_AUTO_MAX())
    {
        // No schedule yet: SPG not programmed, or spg_PaceFrame() just
        // re-anchored on a video-mode change / reset. Render, and let the
        // pacer build a fresh baseline.
        if (spg_PaceFrameSec <= 0.0)
        {
            s_auto_consec_skips = 0;
            return false;
        }

        const bool   maxmode  = FRAMESKIP_AUTO_MAX();
        const double deadband = spg_PaceFrameSec *
            (maxmode ? AUTO_MAX_DEADBAND_FRAC : AUTO_DEADBAND_FRAC);
        const int    cap      = maxmode ? AUTO_MAX_CONSEC_CAP : AUTO_CONSEC_CAP;

        if (spg_PaceErrorSec > deadband && s_auto_consec_skips < cap)
        {
            s_auto_consec_skips++;
            return true;
        }

        // Either we're on schedule, or the cap forces a render. Note that
        // hitting the cap while still behind is the intended floor behaviour:
        // the counter resets here, the rendered frame pushes the debt back up,
        // and the next cap-length run of skips starts — a bounded worst case
        // rather than a frozen screen.
        s_auto_consec_skips = 0;
        return false;
    }

    return false;
}

// ── TRANS_SORT(): per-strip back-to-front sort of the translucent list ──────
// One record per TR strip, built by walking the lists/vertices/listModes
// parallel streams once (the same walk the draw loop does), then qsort'ed.
// Vertex::z holds W (view depth, larger = farther — see vert_base()), so
// back-to-front means descending far_w. Ties keep TA submission order via the
// vtx pointer (monotonically increasing in submission order) so 2D UI layers
// stacked at the same depth are not reshuffled. Sized like lists[] (worst
// case: every strip is translucent) — 8K * 16B = 128KB of BSS.
struct TransStripRec
{
  float far_w;     // max Vertex::z of the strip (its farthest point)
  Vertex *vtx;     // first vertex of the strip
  PolyParam *mod;  // render state in effect for this strip
  u16 count;       // vertex count (sign bit already consumed)
  u16 tr_class;    // HOKUTO_HACK() layer tier at equal depth (0 otherwise):
                   // 3 = full-screen plate, 2 = VQ stage art,
                   // 1 = 8bpp bank 32-47 (stage sprites), 0 = rest
};

static TransStripRec trans_sort_recs[8 * 1024];

// hokuto_hack

// Stage 1
#define HOKUTO_DEBRIS_TEX_ADDR_0 0x000E6000u // top-left corner ~x=242 y=107
#define HOKUTO_DEBRIS_TEX_ADDR_1 0x000E7800u // top-left corner ~x=370 y=107
#define HOKUTO_DEBRIS_TEX_ADDR_2 0x000E9000u // top-left corner ~x=498 y=107
#define HOKUTO_DEBRIS_TEX_ADDR_3 0x000EA800u // top-left corner ~x=242 y=235
#define HOKUTO_DEBRIS_TEX_ADDR_4 0x000EC000u // top-left corner ~x=370 y=235
#define HOKUTO_DEBRIS_TEX_ADDR_5 0x000ED800u // top-left corner ~x=498 y=235

// Stage 2
#define HOKUTO_DEBRIS_TEX_ADDR_7 0x000EF000u // top-left corner ~x=192 y=213
#define HOKUTO_DEBRIS_TEX_ADDR_8 0x000F0800u // top-left corner ~x=320 y=213
#define HOKUTO_DEBRIS_TEX_ADDR_9 0x000F2000u // top-left corner ~x=448 y=213
#define HOKUTO_DEBRIS_TEX_ADDR_10 0x000F3800u // top-left corner ~x=576 y=213
#define HOKUTO_DEBRIS_TEX_ADDR_11 0x000F5000u // top-left corner ~x=704 y=213

static inline bool hokuto_is_debris_tex(u32 addr)
{
  return addr == HOKUTO_DEBRIS_TEX_ADDR_0
      || addr == HOKUTO_DEBRIS_TEX_ADDR_1
      || addr == HOKUTO_DEBRIS_TEX_ADDR_2
      || addr == HOKUTO_DEBRIS_TEX_ADDR_3
      || addr == HOKUTO_DEBRIS_TEX_ADDR_4
      || addr == HOKUTO_DEBRIS_TEX_ADDR_5 // also stage 2's first debris tile (0xED800)
      || addr == HOKUTO_DEBRIS_TEX_ADDR_7
      || addr == HOKUTO_DEBRIS_TEX_ADDR_8
      || addr == HOKUTO_DEBRIS_TEX_ADDR_9
      || addr == HOKUTO_DEBRIS_TEX_ADDR_10
      || addr == HOKUTO_DEBRIS_TEX_ADDR_11;
}

static int trans_strip_cmp(const void *a, const void *b)
{
  const TransStripRec *ra = (const TransStripRec *)a;
  const TransStripRec *rb = (const TransStripRec *)b;
  if (ra->far_w > rb->far_w) return -1; // farther strips draw first
  if (ra->far_w < rb->far_w) return 1;
  // hokuto_hack layer tier: at equal depth, background plates (3) draw before
  // stage art (2) before stage sprites (1) before everything else (0).
  // Always 0 with the preset off.
  if (ra->tr_class > rb->tr_class) return -1;
  if (ra->tr_class < rb->tr_class) return 1;
  // Within tier 2 (VQ stage art) only: some of the grid is opaque
  if (ra->tr_class == 2 && rb->tr_class == 2)
  {
    // HOKUTO_DEBRIS_TEX_ADDR_0 should not be seen
    bool ra_hidden_tile = ((ra->mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK) == HOKUTO_DEBRIS_TEX_ADDR_0;
    bool rb_hidden_tile = ((rb->mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK) == HOKUTO_DEBRIS_TEX_ADDR_0;
    if (ra_hidden_tile && !rb_hidden_tile) return -1;
    if (!ra_hidden_tile && rb_hidden_tile) return 1;

    bool ra_replace = ra->mod->tsp.SrcInstr == 1 && ra->mod->tsp.DstInstr == 0;
    bool rb_replace = rb->mod->tsp.SrcInstr == 1 && rb->mod->tsp.DstInstr == 0;
    if (ra_replace && !rb_replace) return -1;
    if (!ra_replace && rb_replace) return 1;
    // Within the non-REPLACE (blend) bucket: HnK's debris pile (see
    // hokuto_is_debris_tex() above) draws before — behind — other
    // translucent VQ scenery sharing this tier.
    if (!ra_replace && !rb_replace)
    {
      bool ra_debris = hokuto_is_debris_tex((ra->mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK);
      bool rb_debris = hokuto_is_debris_tex((rb->mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK);
      if (ra_debris && !rb_debris) return -1;
      if (!ra_debris && rb_debris) return 1;
    }
  }
  // stable tie-break: original submission order
  if (ra->vtx < rb->vtx) return -1;
  if (ra->vtx > rb->vtx) return 1;
  return 0;
}

// Fixed GX scratch texture, always 640x480 RGB565 (4x4 tiled), 32-byte aligned
// as EFB-copy destinations must be. Shared (file scope) between
// PresentFramebuffer() below — which decodes the DC framebuffer into it — and
// the RENDER_TO_TEXTURE() write-back — which uses it as the GX_CopyTex
// destination. Both users finish with the buffer within their own call and
// both run on the SH4 thread, so sharing is safe and saves 600 KB of BSS.
#define FB2D_W 640
#define FB2D_H 480
static u16 fb2d_tex[FB2D_W * FB2D_H] ATTRIBUTE_ALIGN(32);

// ── RENDER_TO_TEXTURE() state ────────────────────────────────────────────────
// Set by StartRender() around the DoRender() call for an RTT frame; DoRender()
// swaps its canvas/viewport to the RTT target size and hands its tail to
// rtt_copy_efb_to_vram() instead of copying to the display.
static bool s_rtt_pass = false;
static u32  s_rtt_w = 0;   // RTT target size, from FB_X_CLIP / FB_Y_CLIP
static u32  s_rtt_h = 0;
#if RTT_DEBUG_LOG
static u32 s_rtt_drawn = 0, s_rtt_skipped = 0, s_rtt_probe_n = 0;
static float s_rtt_bx0, s_rtt_bx1, s_rtt_by0, s_rtt_by1;
#endif
static u32  s_rtt_x = 0;   // ... and its ORIGIN: the clip is a window into the
static u32  s_rtt_y = 0;   // normal screen space, not a canvas of its own

// ── Per-strip user tile clip (listTileClip[]) ────────────────────────────────
// DC-pixel → EFB-pixel mapping of the current DoRender() pass, stored by the
// viewport block below and used by apply_tile_clip() to turn a strip's user
// tile clip into a GX scissor.
static float s_clip_sx = 1.f, s_clip_sy = 1.f; // DC px → EFB px scale
static float s_clip_ox = 0.f, s_clip_oy = 0.f; // EFB px offset (viewport origin)
static u32 s_tileclip_applied = 0; // packed tileclip currently in the GX scissor

// Apply a strip's user tile clip as a GX scissor. Mode 2 (inside): scissor =
// rect, clamped to the EFB. Mode 0/1 (disabled/reserved): full EFB. Mode 3
// (outside — draw only OUTSIDE the rect) cannot be expressed as one GX
// scissor rect and is drawn unclipped; splitscreen games only use inside mode.
static void apply_tile_clip(u32 tc)
{
  if ((tc >> 30) != 2)
    tc = 0;
  if (tc == s_tileclip_applied)
    return;
  s_tileclip_applied = tc;
  if (tc == 0)
  {
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    return;
  }
  // Tile units are 32 DC pixels; max is inclusive.
  float x0 = (float)((tc >> 24) & 63) * 32.f;
  float y0 = (float)((tc >> 16) & 31) * 32.f;
  float x1 = (float)(((tc >> 8) & 63) + 1) * 32.f;
  float y1 = (float)((tc & 31) + 1) * 32.f;
  s32 ex0 = (s32)(s_clip_ox + x0 * s_clip_sx);
  s32 ey0 = (s32)(s_clip_oy + y0 * s_clip_sy);
  s32 ex1 = (s32)(s_clip_ox + x1 * s_clip_sx + 0.5f);
  s32 ey1 = (s32)(s_clip_oy + y1 * s_clip_sy + 0.5f);
  if (ex0 < 0) ex0 = 0;
  if (ey0 < 0) ey0 = 0;
  if (ex1 > (s32)rmode->fbWidth)   ex1 = (s32)rmode->fbWidth;
  if (ey1 > (s32)rmode->efbHeight) ey1 = (s32)rmode->efbHeight;
  if (ex1 <= ex0 || ey1 <= ey0)
  {
    // Fully clipped away: park the scissor on a 1px corner so nothing shows.
    ex0 = 0; ey0 = 0; ex1 = 1; ey1 = 1;
  }
  GX_SetScissor((u32)ex0, (u32)ey0, (u32)(ex1 - ex0), (u32)(ey1 - ey0));
}

// ── AUTOSORT(): per-pixel translucent depth peeling ─────────────────────────
// See the preset doc at the top of the file for the algorithm. Everything
// here is the fixed-function plumbing: the Z snapshot buffers the select
// pass compares against, the two fragment-depth ramp textures, and the TEV
// compare chain.

#define AS_MAX_PEELS 4

// Z snapshot of the compare reference: opaque depth for peel 0, previous
// peel's selection for peels 1+. Two 8-bit planes (EFB Z bits 23:16 and
// 15:8) instead of one Z16 copy: 8-bit copies sample as I8 (R=G=B=byte),
// which has no high/low byte-order ambiguity to get wrong on hardware.
static u8 *s_as_zref_hi;   // GX_TF_Z8    copy: Z bits 23:16
static u8 *s_as_zref_mid;  // GX_CTF_Z8M  copy: Z bits 15:8
// Fragment-depth ramps, 1024x1 RGBA8, value = 16-bit screen depth in (G,R)
// as GX_TEV_COMP_GR16_GT reads it. Texel i covers screen depth [i,i+1)/1024
// = Z16 [64i, 64i+64):
//   ramp_gt = 64i-1 : guaranteed <= any true Z16 in the span minus 1, so a
//             strict GT against the previous peel can never re-select the
//             very layer it just drew (re-selection would double-blend it
//             every remaining peel; dropping sub-texel neighbours is the
//             safe failure mode).
//   ramp_ge = 64i+64: guaranteed >= any true Z16 in the span plus 1, so
//             peel 0's compare against the opaque snapshot behaves like >=
//             and coplanar decals on opaque surfaces still pass.
static u8 *s_as_ramp_gt;
static u8 *s_as_ramp_ge;
static GXTexObj s_as_tex_zhi, s_as_tex_zmid, s_as_tex_rgt, s_as_tex_rge;
static bool s_as_ready = false;

// RGBA8 tile write, row 0 only (ramps are 1024x1). 4x4 RGBA8 tiles hold a
// 32-byte AR plane then a 32-byte GB plane.
static void as_ramp_store(u8 *tex, u32 x, u32 v16)
{
  u8 *tile = tex + (x >> 2) * 64;
  u32 c = (x & 3) * 2;
  tile[c + 0]      = 0xFF;             // A (unused)
  tile[c + 1]      = (u8)(v16 & 0xFF); // R = low byte
  tile[32 + c + 0] = (u8)(v16 >> 8);   // G = high byte
  tile[32 + c + 1] = 0;                // B (unused)
}

// MEM2 arena bump allocation. This libogc doesn't export
// SYS_AllocArena2MemLo, so do exactly what it would: advance Arena2Lo by the
// aligned size. Runs once at InitRenderer time, before anything else in this
// process claims Arena2.
static void *as_arena2_alloc(u32 size, u32 align)
{
  u32 lo = (u32)SYS_GetArena2Lo();
  u32 hi = (u32)SYS_GetArena2Hi();
  lo = (lo + align - 1) & ~(align - 1);
  if (lo + size > hi)
    return NULL;
  SYS_SetArena2Lo((void *)(lo + size));
  return (void *)lo;
}

// One-time setup, called from InitRenderer (after rmode/pixel format exist).
// Buffers live in MEM2 (~700 KB): MEM1 is the scarce pool (see the budget
// audit), and GX samples/copies to MEM2 fine on Wii.
static void as_init()
{
  if (rmode->aa)
  {
    printf("[AUTOSORT] disabled: AA pixel format has no Z24 buffer\n");
    return;
  }
  u32 zw = rmode->fbWidth, zh = rmode->efbHeight;
  u32 zbytes = ((zw + 7) & ~7u) * ((zh + 3) & ~3u); // Z8/I8 tiles are 8x4
  s_as_zref_hi  = (u8 *)as_arena2_alloc(zbytes, 32);
  s_as_zref_mid = (u8 *)as_arena2_alloc(zbytes, 32);
  s_as_ramp_gt  = (u8 *)as_arena2_alloc(256 * 64, 32); // 1024x1 RGBA8
  s_as_ramp_ge  = (u8 *)as_arena2_alloc(256 * 64, 32);
  if (!s_as_zref_hi || !s_as_zref_mid || !s_as_ramp_gt || !s_as_ramp_ge)
  {
    printf("[AUTOSORT] disabled: MEM2 alloc failed\n");
    return;
  }
  memset(s_as_zref_hi, 0, zbytes);
  memset(s_as_zref_mid, 0, zbytes);
  for (u32 i = 0; i < 1024; i++)
  {
    u32 zlo = i * 64;
    u32 zhi = i * 64 + 64;
    as_ramp_store(s_as_ramp_gt, i, zlo ? zlo - 1 : 0);
    as_ramp_store(s_as_ramp_ge, i, zhi > 65535 ? 65535 : zhi);
  }
  DCFlushRange(s_as_zref_hi, zbytes);
  DCFlushRange(s_as_zref_mid, zbytes);
  DCFlushRange(s_as_ramp_gt, 256 * 64);
  DCFlushRange(s_as_ramp_ge, 256 * 64);
  GX_InitTexObj(&s_as_tex_zhi, s_as_zref_hi, zw, zh, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjLOD(&s_as_tex_zhi, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GX_InitTexObj(&s_as_tex_zmid, s_as_zref_mid, zw, zh, GX_TF_I8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjLOD(&s_as_tex_zmid, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GX_InitTexObj(&s_as_tex_rgt, s_as_ramp_gt, 1024, 1, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjLOD(&s_as_tex_rgt, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GX_InitTexObj(&s_as_tex_rge, s_as_ramp_ge, 1024, 1, GX_TF_RGBA8, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjLOD(&s_as_tex_rge, GX_NEAR, GX_NEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  s_as_ready = true;
  printf("[AUTOSORT] ready: %ux%u Z snapshots, %.0f KB MEM2\n",
         zw, zh, (2.0f * zbytes + 2 * 256 * 64) / 1024.0f);
}

// Per-frame setup when peeling is active this frame: the two projective
// texgens the select pass samples with, plus the GR16 channel-mask konsts.
// Vertices are in DC screen space pre-multiplied by W (x = sx*W, z = -W),
// so both texgens ride q = W (row 2) for the perspective divide:
//   TEXCOORD1  s = (s_clip_ox + sx*s_clip_sx)/fbW, t likewise with y: the
//              fragment's own EFB pixel, exactly texel-aligned with the Z
//              snapshot copies (both cover the full EFB rect).
//   TEXCOORD2  s = 1 - p5 + p6/W: the very screen depth the rasterizer
//              writes (viewport z, 0=far 1=near), indexing the ramps.
static void as_setup_frame(float p5, float p6)
{
  Mtx m;
  m[0][0] = s_clip_sx / (float)rmode->fbWidth;
  m[0][1] = 0.0f;
  m[0][2] = -s_clip_ox / (float)rmode->fbWidth;
  m[0][3] = 0.0f;
  m[1][0] = 0.0f;
  m[1][1] = s_clip_sy / (float)rmode->efbHeight;
  m[1][2] = -s_clip_oy / (float)rmode->efbHeight;
  m[1][3] = 0.0f;
  m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = -1.0f; m[2][3] = 0.0f;
  GX_LoadTexMtxImm(m, GX_TEXMTX0, GX_MTX3x4);

  m[0][0] = 0.0f; m[0][1] = 0.0f; m[0][2] = p5 - 1.0f; m[0][3] = p6;
  m[1][0] = 0.0f; m[1][1] = 0.0f; m[1][2] = 0.0f;      m[1][3] = 0.5f;
  m[2][0] = 0.0f; m[2][1] = 0.0f; m[2][2] = -1.0f;     m[2][3] = 0.0f;
  GX_LoadTexMtxImm(m, GX_TEXMTX1, GX_MTX3x4);

  GX_SetTexCoordGen(GX_TEXCOORD1, GX_TG_MTX3x4, GX_TG_POS, GX_TEXMTX0);
  GX_SetTexCoordGen(GX_TEXCOORD2, GX_TG_MTX3x4, GX_TG_POS, GX_TEXMTX1);

  GXColor k0 = {0, 255, 0, 255};   // keep G (high byte)
  GXColor k1 = {255, 0, 0, 255};   // keep R (low byte)
  GX_SetTevKColor(GX_KCOLOR0, k0);
  GX_SetTevKColor(GX_KCOLOR1, k1);
}

// Select-pass TEV chain. Stage 0 is left to the per-strip code (the
// polygon's own texture, MODULATE, so APREV carries texA*vtxA for the
// transparent-texel kill). Stages 1-3:
//   s1  REG0.G  = Z snapshot high byte   (TEXC * K0)
//   s2  REG0.R += Z snapshot mid  byte   (TEXC * K1 + C0)
//   s3  GR16 compare: ramp(TEXC) > REG0 ? keep : kill. The alpha combiner
//       in R8/GR16/BGR24 compare modes compares the COLOR a/b inputs of
//       the same stage and selects between its own c/d alphas (hardware
//       behavior, mirrored by Dolphin), so the color and alpha ops here
//       run one and the same comparison: alpha out = pass ? c : 0, with
//       c = KONST(1.0) normally or APREV when the strip's transparent
//       texels may be killed too. GX_GREATER 0 alpha test does the kill.
static void as_setup_select_tev(bool first_peel)
{
  GX_LoadTexObj(&s_as_tex_zhi, GX_TEXMAP4);
  GX_LoadTexObj(&s_as_tex_zmid, GX_TEXMAP5);
  GX_LoadTexObj(first_peel ? &s_as_tex_rge : &s_as_tex_rgt, GX_TEXMAP6);

  GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORD1, GX_TEXMAP4, GX_COLORNULL);
  GX_SetTevKColorSel(GX_TEVSTAGE1, GX_TEV_KCSEL_K0);
  GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_ZERO);
  GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
  GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

  GX_SetTevOrder(GX_TEVSTAGE2, GX_TEXCOORD1, GX_TEXMAP5, GX_COLORNULL);
  GX_SetTevKColorSel(GX_TEVSTAGE2, GX_TEV_KCSEL_K1);
  GX_SetTevColorIn(GX_TEVSTAGE2, GX_CC_ZERO, GX_CC_TEXC, GX_CC_KONST, GX_CC_C0);
  GX_SetTevColorOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVREG0);
  GX_SetTevAlphaIn(GX_TEVSTAGE2, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  GX_SetTevAlphaOp(GX_TEVSTAGE2, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);

  GX_SetTevOrder(GX_TEVSTAGE3, GX_TEXCOORD2, GX_TEXMAP6, GX_COLORNULL);
  GX_SetTevKAlphaSel(GX_TEVSTAGE3, GX_TEV_KASEL_1);
  GX_SetTevColorIn(GX_TEVSTAGE3, GX_CC_TEXC, GX_CC_C0, GX_CC_ONE, GX_CC_ZERO);
  GX_SetTevColorOp(GX_TEVSTAGE3, GX_TEV_COMP_GR16_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GX_SetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO, GX_CA_KONST, GX_CA_ZERO);
  GX_SetTevAlphaOp(GX_TEVSTAGE3, GX_TEV_COMP_GR16_GT, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

// Z-only full-canvas quad (color/alpha updates are expected OFF, Z func
// ALWAYS): parks the whole Z buffer at depth W. Same position convention as
// every other vertex this renderer submits (x = sx*W, z = -W), and the full
// VCD is fed so the vertex stream never changes shape mid-frame.
// wide_vtx: the frame's VCD carries the CLR1 attribute (OFFSET_COLOR_FIX() or
// FOG() is on), so every vertex submitted this frame — this quad included —
// must supply it or the CP packet stream goes out of alignment.
static void as_submit_zquad(float dcw, float dch, float W, bool wide_vtx)
{
  const float xs[4] = {0.0f, dcw, 0.0f, dcw};
  const float ys[4] = {0.0f, 0.0f, dch, dch};
  GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
  for (int i = 0; i < 4; i++)
  {
    GX_Position3f32(xs[i] * W, ys[i] * W, -W);
    GX_Color1u32(0);
    if (wide_vtx)
      GX_Color1u32(0);
    GX_TexCoord2f32(0.0f, 0.0f);
  }
  GX_End();
}

// ── SUBPASS_ZCLEAR(): depth-only re-clear between geometry groups ──────────
// GX has no partial/depth-only EFB clear short of a real copy-out (which
// would also cost the color buffer), so re-parking the Z buffer at a chosen
// depth is done the same way AUTOSORT() already does it above: rasterize a
// full-canvas quad with color/alpha writes off and Z func ALWAYS, Z write
// on. Caller is responsible for restoring whatever GX_SetZMode /
// GX_SetColorUpdate / GX_SetAlphaUpdate state it needs afterward — this is a
// low-level primitive, not a state save/restore wrapper (same contract as
// as_submit_zquad, which this reuses).
//   atW      : the W (Dreamcast depth convention, see vtx_min_Z/vtx_max_Z)
//              every pixel is re-initialised to. Pass vtx_min_Z to mark the
//              buffer "everything is as close as the near plane" (nothing
//              already-drawn survives GEQUAL against later geometry), or
//              vtx_max_Z for the opposite ("everything is at the far
//              plane" — the same state a fresh frame's clear leaves it in).
static void ClearDepthOnlyPass(float dcw, float dch, float atW, bool wide_vtx)
{
  GX_SetColorUpdate(GX_FALSE);
  GX_SetAlphaUpdate(GX_FALSE);
  GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
  GX_SetZCompLoc(GX_TRUE);
  GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
  GX_SetNumTevStages(1);
  GX_SetNumTexGens(1);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
  GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
  as_submit_zquad(dcw, dch, atW, wide_vtx);
  GX_SetColorUpdate(GX_TRUE);
  GX_SetAlphaUpdate(GX_TRUE);
}

// ── FOG(): the TEV stage that applies one polygon's fog mode ───────────────
// stage is the (fixed for the whole frame) TEV stage index the fog blend runs
// in — 1 normally, 2 when OFFSET_COLOR_FIX() owns stage 1. The coefficient
// arrives as the CLR1 alpha of each vertex (GX_CC_RASA / GX_CA_RASA); the fog
// colour is a konst, K2 = FOG_COL_RAM and K3 = FOG_COL_VERT, both loaded once
// per frame in DoRender(). K0/K1 are left alone: AUTOSORT()'s select pass owns
// those.
//
// Not called for mode 2 (No Fog) — that polygon simply drops the stage from
// the stage count instead, so it keeps the legacy pipeline exactly.
static void FogSetStage(u8 stage, int mode)
{
  // Fog colour: per-vertex mode blends toward FOG_COL_VERT, both LUT modes
  // toward FOG_COL_RAM.
  GX_SetTevKColorSel(stage, (mode == 1) ? GX_TEV_KCSEL_K3 : GX_TEV_KCSEL_K2);
  GX_SetTevOrder(stage, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);

  if (mode == 3)
  {
    // LUT Mode 2: the polygon's own colour is discarded — the pixel becomes
    // the fog colour with the fog coefficient as its alpha, and the game's
    // own blend equation composites it.
    GX_SetTevColorIn(stage, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_KONST);
    GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_RASA);
  }
  else
  {
    // Modes 0 and 1: colour = lerp(previous stage, fog colour, coefficient).
    // TEV computes D + (1-C)*A + C*B, so A=CPREV, B=KONST, C=RASA, D=ZERO is
    // the lerp verbatim. Alpha passes straight through — PVR fog never
    // touches the pixel's alpha in these modes.
    GX_SetTevColorIn(stage, GX_CC_CPREV, GX_CC_KONST, GX_CC_RASA, GX_CC_ZERO);
    GX_SetTevAlphaIn(stage, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
  }
  GX_SetTevColorOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  GX_SetTevAlphaOp(stage, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
}

// ── POLY_OFFSET(): depth bias for co-planar PT-list decals ─────────────────
// GX has no glPolygonOffset-style register to reach from the fixed-function
// pixel pipeline. GX_SetZTexture(GX_ZT_ADD,...) can inject a constant into
// the Z test, but it samples whatever texture the LAST ACTIVE TEV stage has
// bound (there is no independent "bias map" slot) — coupling the bias to
// each strip's own decal texture is fragile and unverifiable without real
// hardware to test against, so this renderer instead biases depth the same
// way it already does everywhere else that needs a Z nudge (see the HUD_PASS
// rescue and the AUTOSORT near-plane park above): scale W (and, to keep the
// screen-space x/y bit-identical, x/y by the same ratio — x/y here are
// already W-premultiplied). Software, not a hardware register, but it is
// the technique this codebase already relies on and it is exactly correct
// per-vertex, unlike the texture-coupled hardware trick.
//
// Smaller W = nearer camera (this file's convention, see vtx_min_Z/vtx_max_Z
// and as_setup_frame()'s "0=far 1=near" note), so shrinking W nudges the
// PT-list decal strictly closer than the opaque geometry it sits on,
// guaranteeing it wins the co-planar GX_GEQUAL tie instead of flickering.
// Tiers are fractions of W, not fixed Z units, so the bias scales correctly
// whether FIXED_DEPTH_TIGHT() or the legacy dynamic range is in effect.
static const float s_poly_offset_frac[4] = { 0.0f, 0.0005f, 0.0015f, 0.004f };

static inline void ApplyPolyOffset(Vertex *v, int count, int tier)
{
  if (tier <= 0) return;
  if (tier > 3) tier = 3;
  const float frac = s_poly_offset_frac[tier];
  for (int i = 0; i < count; i++)
  {
    const float w = v[i].z;
    if (w <= 0.0f) continue; // already clamped upstream, but stay defensive
    const float r = (1.0f - frac); // biasedW / w
    v[i].x *= r;
    v[i].y *= r;
    v[i].z  = w * r;
  }
}

// Copy the finished EFB scene back into emulated VRAM at FB_W_SOF1, the way
// the real PVR writes an RTT frame, then leave the EFB cleared for the next
// frame (the display path gets that clear from GX_CopyDisp's clear flag).
//
// Addressing: bit 24 of FB_W_SOF1 marks the 64-bit texture area, which is
// exactly the linear params.vram[] layout the texture decoders read — so the
// pixels are written linearly (no 32->64 offset conversion), and the first
// u32 written at the target address stomps the tex-cache 0xDEADBEEF sentinel,
// which is precisely the "re-decode this texture" signal.
//
// Pixel layout: Plannar() reads each native u32 word as low16 = left pixel,
// high16 = right pixel, so pairs are written as (p1 << 16) | p0.
static void rtt_copy_efb_to_vram()
{
  // Pad the copy rect to the 4x4 tile grid of GX_TF_RGB565 so the tile pitch
  // used below is exact (EFB copies also require even sizes).
  u32 copy_w = (s_rtt_w + 3) & ~3u;
  u32 copy_h = (s_rtt_h + 3) & ~3u;

  // Source rect: the clip WINDOW inside the EFB, mapped through the same
  // DC-px -> EFB-px transform the viewport was set up with. The pass rendered
  // in full screen space, so the texture's pixels live at the clip origin, not
  // at the EFB corner (they coincide only when the game clips from 0,0).
  u32 src_x = (u32)(s_rtt_x * s_clip_sx + s_clip_ox) & ~1u;
  u32 src_y = (u32)(s_rtt_y * s_clip_sy + s_clip_oy) & ~1u;
  if (src_x + copy_w > (u32)rmode->fbWidth)   src_x = 0; // rect must stay inside
  if (src_y + copy_h > (u32)rmode->efbHeight) src_y = 0; // the EFB or GX faults

  // Grab the RTT pixels without clearing yet — the EFB is cleared as a whole
  // below so color AND depth start fresh everywhere, not just in this rect.
#if RTT_DEBUG_LOG
  // Poison the destination first. After the copy: still 0xA5A5 => GX_CopyTex
  // never wrote anything; all zeros => the copy ran and the EFB really is black.
  const bool rtt_probe = ((s_rtt_probe_n++ % 120) == 0);
  if (rtt_probe)
  {
    for (u32 i = 0; i < copy_w * copy_h; i++) fb2d_tex[i] = 0xA5A5;
    DCFlushRange(fb2d_tex, copy_w * copy_h * 2);
  }
#endif
  // EFB-copy hygiene, the same discipline the AUTOSORT() depth snapshot uses:
  //   * the deflicker/AA copy filter blends neighbouring EFB rows, which is
  //     right for a picture and wrong for a texture the game will sample —
  //     turn it off for this copy and restore it for the display copy;
  //   * GX_PixModeSync() before reading the EFB back, so the pixel engine has
  //     retired the writes this pass just made instead of copying stale tiles.
  GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);
  GX_PixModeSync();
  GX_SetTexCopySrc((u16)src_x, (u16)src_y, (u16)copy_w, (u16)copy_h);
  GX_SetTexCopyDst((u16)copy_w, (u16)copy_h, GX_TF_RGB565, GX_FALSE);
  GX_CopyTex(fb2d_tex, GX_FALSE);
  GX_PixModeSync();
  GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
  GX_DrawDone(); // wait for the copy to land in main memory
  DCInvalidateRange(fb2d_tex, copy_w * copy_h * 2);

#if RTT_DEBUG_LOG
  if (rtt_probe)
  {
    u32 nz = 0, firsti = 0, poison = 0; u16 firstv = 0;
    for (u32 i = 0; i < copy_w * copy_h; i++)
    {
      const u16 v = fb2d_tex[i];
      if (v == 0xA5A5) poison++;
      else if (v) { if (!nz) { firstv = v; firsti = i; } nz++; }
    }
    printf("[RTT] drawn=%u skipped=%u drawn_bbox=(%.0f,%.0f)-(%.0f,%.0f) | "
           "src=(%u,%u) %ux%u scale=(%.3f,%.3f) efb=%dx%d | "
           "nonzero=%u poison=%u of %u first=%04X@%u\n",
           s_rtt_drawn, s_rtt_skipped, s_rtt_bx0, s_rtt_by0, s_rtt_bx1, s_rtt_by1,
           src_x, src_y, copy_w, copy_h,
           s_clip_sx, s_clip_sy, (int)rmode->fbWidth, (int)rmode->efbHeight,
           nz, poison, copy_w * copy_h, firstv, firsti);
    fflush(stdout);
  }
#endif

  u32 packmode = FB_W_CTRL & 7;
  if (packmode <= 3)
  {
    // fb_kval bit 7 (FB_W_CTRL bit 15) becomes pixel bit 15 for 0555/1555.
    u16 kbit    = (u16)(FB_W_CTRL & 0x8000);
    u32 stride  = (FB_W_LINESTRIDE & 0x1FF) * 8; // register unit = 8 bytes/line
    if (stride == 0)
      stride = s_rtt_w * 2;
    u32 base = FB_W_SOF1 & VRAM_MASK & ~3u;
    u32 tiles_per_row = copy_w >> 2;

    for (u32 y = 0; y < s_rtt_h; y++)
    {
      const u16 *trow = &fb2d_tex[((y >> 2) * tiles_per_row) * 16 + (y & 3) * 4];
      u32 line = base + y * stride;
      for (u32 x = 0; x < s_rtt_w; x += 2)
      {
        // x is even, so x and x+1 share a 4x4 tile row (x&3 is 0 or 2).
        const u16 *tile = &trow[(x >> 2) * 16];
        u16 t0 = tile[(x & 3) + 0];
        u16 t1 = tile[(x & 3) + 1];
        u16 p0, p1;
        switch (packmode)
        {
        case 1: // RGB565: EFB copy format, pass through
          p0 = t0;
          p1 = t1;
          break;
        case 2: // ARGB4444 (alpha forced opaque: RGB565 copies carry no alpha)
          p0 = (u16)(0xF000 | ((t0 >> 4) & 0x0F00) | ((t0 >> 3) & 0x00F0) | ((t0 >> 1) & 0x000F));
          p1 = (u16)(0xF000 | ((t1 >> 4) & 0x0F00) | ((t1 >> 3) & 0x00F0) | ((t1 >> 1) & 0x000F));
          break;
        default: // 0 = KRGB0555, 3 = ARGB1555: drop G LSB, K/A from fb_kval
          p0 = (u16)(kbit | ((t0 >> 1) & 0x7FE0) | (t0 & 0x001F));
          p1 = (u16)(kbit | ((t1 >> 1) & 0x7FE0) | (t1 & 0x001F));
          break;
        }
        *(u32 *)&params.vram[(line + x * 2) & VRAM_MASK] = ((u32)p1 << 16) | p0;
      }
    }
#if RTT_DEBUG_LOG
    if (rtt_probe)
    {
      printf("[RTT] wrote VRAM base=%06X stride=%u pack=%u W_SOF1=%08X first=%08X\n",
             base, stride, packmode, (u32)FB_W_SOF1, *(u32 *)&params.vram[base]);
      fflush(stdout);
    }
#endif
  }
  else if (DEBUG_MESSAGE())
  {
    // 24/32-bit packmodes (4/5/6) are not written back: no DC texture format
    // reads them, so a game doing this is using the buffer some other way.
    printf("[RTT] unsupported FB_W_CTRL packmode %d, write-back skipped\n", (int)packmode);
  }

  // Clear the whole EFB (color + Z) for the next frame, chunked so each
  // discard-copy destination fits the scratch buffer. With fbWidth=640 the
  // chunk is 480 lines, i.e. a single copy on every common video mode.
  u32 chunk = ((u32)(FB2D_W * FB2D_H) / rmode->fbWidth) & ~3u;
  for (u32 y = 0; y < rmode->efbHeight; y += chunk)
  {
    u32 ch = rmode->efbHeight - y;
    if (ch > chunk)
      ch = chunk;
    GX_SetTexCopySrc(0, (u16)y, rmode->fbWidth, (u16)ch);
    GX_SetTexCopyDst(rmode->fbWidth, (u16)ch, GX_TF_RGB565, GX_FALSE);
    GX_CopyTex(fb2d_tex, GX_TRUE); // data discarded, only the clear matters
  }
  // Wait for the discard-copies: fb2d_tex must not be pending as a GP copy
  // destination when PresentFramebuffer() next fills it from the CPU side.
  GX_DrawDone();
}

// ============================
// The main rendering loop. Executes GX commands to draw the stored vertex lists.
// ============================

void DoRender()
{
  // ASYNC_RENDER(): wait for the previous queued frame and apply its deferred
  // VIDEO flip before anything else — texture decode below re-writes bump
  // slots the GPU may still be sampling. No-op when nothing is pending.
  gx_sync_pending();

  /* commented to help prevent FIFO */
  /*
  if(1) {
    printf("MEM1 free: %.2f MB\n", ((unat)SYS_GetArena1Hi() - (unat)SYS_GetArena1Lo()) / 1024.f / 1024);
    printf("MEM2 free: %.2f MB\n", ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / 1024.f / 1024.f);
  }
    */

  // 240p (non-interlaced NTSC/PAL) modes only use a fraction of the nominal
  // 640x480 canvas (shrink it so the scene still fills the output), while
  // SCALER_CTL.hscale games render 1280 wide for 2:1 horizontal SSAA (grow it
  // so the whole scene shows instead of just the left half) — see SetFbScale.
  float dc_width = 640.f * g_fb_scale_x;
  float dc_height = 480.f * g_fb_scale_y;
  // Forced canvas width (see CANVAS_WIDTH() above), only for 240p modes —
  // g_fb_scale_y == 0.5 identifies non-interlaced NTSC/PAL (set by
  // CalculateSync in SPG.cpp). Interlaced/VGA screens keep their canvas.
  bool forced_canvas_w = false;
  {
    int forced_w = CANVAS_WIDTH();
    if (forced_w >= 320 && forced_w <= 1280 && g_fb_scale_y == 0.5f)
    {
      dc_width = (float)forced_w;
      forced_canvas_w = true;
    }
  }
  // Y SCALER (see Y_SCALER() above): the vertical twin of the hscale handling
  // that CalculateSync() folds into g_fb_scale_x. Read straight from the
  // register here instead of routing it through SetFbScale, so the 240p
  // sentinel just above (g_fb_scale_y == 0.5) keeps meaning "video mode" and
  // not "video mode times whatever the game scaled by". The factor is masked
  // out of .full rather than read through the bitfield, so the big-endian
  // layout can never bite again (see the SCALER_CTL_type note in regs.h).
  if (Y_SCALER())
  {
    const float vsf = (float)(SCALER_CTL.full & 0xFFFF) / 1024.f;
    // 0x400 (= 1.0) is the idle value every game programs, and anything
    // outside a sane 1/4x..4x window is uninitialised garbage rather than a
    // scale request - in both cases leave the canvas alone.
    if (vsf >= 0.25f && vsf <= 4.0f && (vsf < 0.999f || vsf > 1.001f))
      dc_height *= vsf;
  }
  // H SCALER (see H_SCALER() above): with VO_CONTROL.pixel_double set the
  // framebuffer is half width and the DAC doubles every pixel on the way out,
  // so the scene lives in a 320-wide screen space. An explicit CANVAS_WIDTH()
  // wins - that preset is the manual override for the games that render narrow
  // without ever setting this bit.
  if (H_SCALER() && !forced_canvas_w && ((VO_CONTROL >> 8) & 1))
    dc_width *= 0.5f;
  // NOTE (RTT canvas): this used to force dc_width/dc_height to the RTT target
  // size, on the assumption that an RTT pass submits geometry in its target's
  // own coordinate space. It does not. The PVR renders every pass in the SAME
  // screen space and FB_X_CLIP / FB_Y_CLIP merely restrict which pixels get
  // written out — the clip is a WINDOW, not a canvas. Silent Scope settles it:
  // its 256x256 scope pass (XCLIP=0..255 YCLIP=0..255) submits geometry
  // spanning x=-390..24188 in ordinary 640-wide screen space. Squeezing that
  // into a 256-wide canvas magnified the scene 2.5x and wrote the wrong pixels
  // into the texture, which is why render_to_texture=on produced nothing
  // usable. The canvas below is now the display canvas for RTT passes too.

  VIDEO_SetBlack(FALSE);
  // Set viewport to a centred 4:3 sub-region of the 16:9 framebuffer.
  // NDC [-1..+1] maps to this viewport, so all DC geometry (which is
  // already in 4:3 screen-space) displays with correct proportions.
  // In fullscreen mode use the whole width (stretched 16:9).
  if (s_rtt_pass)
  {
    // RTT: render the pass across the WHOLE EFB in ordinary DC screen space
    // (see the canvas note above), full width regardless of the display aspect
    // preset — the result is a texture, not a picture, so it must not inherit
    // the 4:3 pillarbox. rtt_copy_efb_to_vram() then lifts the clip window out
    // of the EFB, using exactly the s_clip_* mapping set here.
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    // GX scissor state persists across frames and across passes, and nothing on
    // the RTT path ever set it — so a sub-rect left by a user tile clip (or by
    // the carry overlay) would silently reject every pixel of this render.
    // Open it explicitly; the tile clip re-applies per strip if it is on.
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
    s_tileclip_applied = 0;
    s_clip_sx = (float)rmode->fbWidth / dc_width;
    s_clip_sy = (float)rmode->efbHeight / dc_height;
    s_clip_ox = 0.f; s_clip_oy = 0.f;
  }
  else if (RATIO_FULL_WIDTH())
  {
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    s_clip_sx = (float)rmode->fbWidth / dc_width;
    s_clip_sy = (float)rmode->efbHeight / dc_height;
    s_clip_ox = 0.f; s_clip_oy = 0.f;
  }
  else
  {
    const float ratio  = (4.f / 3.f) / (16.f / 9.f); // 0.75
    const float vp_w   = rmode->fbWidth * ratio;
    const float vp_x   = (rmode->fbWidth - vp_w) * 0.5f;
    GX_SetViewport(vp_x, 0, vp_w, rmode->efbHeight, 0, 1);
    s_clip_sx = vp_w / dc_width;
    s_clip_sy = (float)rmode->efbHeight / dc_height;
    s_clip_ox = vp_x; s_clip_oy = 0.f;
  }
  // Per-strip user tile clip (splitscreen games draw both players' viewports
  // in one pass, confining each with an inside clip). Read once per frame.
  // GX scissor state persists across frames — with the preset on, re-open it
  // in case the last strip of the previous frame left a sub-rect applied;
  // with it off the scissor is never touched, so skip the GX call entirely.
  const bool tileclip_on = SPLIT_SCREEN();
  if (tileclip_on)
  {
    s_tileclip_applied = 0;
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  }
  GX_InvVtxCache();
  // TMEM_CACHE(): keep the GPU texture cache warm across frames; stale entries
  // are invalidated at decode time instead (see SetTextureParams).
  if (!TMEM_CACHE())
    GX_InvalidateTexAll();

  // GX ACCURATE
  // Single vertex format, always 24 bytes/vertex (POS+CLR0+TEX0).
  // VCD never changes mid-stream to avoid CP packet FIFO misalignment.
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS,  GX_POS_XYZ,  GX_F32,   0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST,   GX_F32,   0);

  // OLD GX ACCURATE (better in some cases ?)
  // Define the vertex format for the GX pipeline.
  // GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
  // GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
  // GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);

  // Note : making an if/else statement here also cause FIFO (CPU desynchronisation)

  

  // Offset (specular) color support. Read once per frame like the other
  // presets; the setting cannot change while a game is running, so the VCD
  // stays identical from frame to frame (see the FIFO warnings above).
  const bool offset_fix = OFFSET_COLOR_FIX();
  // 2D sprite seam fix — read once per frame, same rationale as offset_fix.
  const bool seam_fix = SEAM_FIX();
  // FOG(): per-polygon PVR fog (see the macro doc at the top of the file).
  // Read once per frame like the presets above — the CLR1 vertex attribute it
  // needs is a VCD decision, and the VCD must not change mid-stream.
  const bool fog_on = FOG();
  if (fog_on)
    FogFrameSetup();
  // CLR1 in the vertex format: the offset colour needs its RGB, fog needs its
  // alpha. Either preset alone is enough to widen the vertex.
  const bool emit_clr1 = offset_fix || fog_on;
  // TEV stages that are live for EVERY polygon this frame, before the
  // per-polygon fog stage is appended on top. With fog on, the offset stage is
  // pinned on rather than toggled per polygon, so the fog stage keeps one
  // fixed index all frame instead of sliding between 1 and 2 (polygons without
  // an offset colour carry spc.rgb == 0, so the pinned stage adds nothing).
  const int tev_base  = (offset_fix && fog_on) ? 2 : 1;
  const u8  fog_stage = (u8)tev_base; // GX_TEVSTAGE0..15 are 0..15

  GX_SetNumChans(emit_clr1 ? 2 : 1);
  GX_SetNumTexGens(1);

  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);

  if (emit_clr1)
  {
    // 28 bytes/vertex: POS+CLR0+CLR1+TEX0. Every vertex this frame submits
    // CLR1 (spc, 0 when unused) so the VCD never changes mid-stream.
    GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR1, GX_CLR_RGBA, GX_RGBA8, 0);
    GX_SetVtxDesc(GX_VA_CLR1, GX_DIRECT);
    // Channel 1: plain vertex color, no lighting.
    GX_SetChanCtrl(GX_COLOR1A1, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, 0, GX_DF_NONE, GX_AF_NONE);
  }

  if (fog_on)
  {
    // Fog colours as TEV konsts, picked per polygon by FogSetStage(): K2 for
    // the LUT modes, K3 for per-vertex fog. Alpha is unused (the fog stage
    // only ever selects KONST as a colour input).
    GXColor kfog_ram  = { (u8)(s_fog_col_ram  >> 16), (u8)(s_fog_col_ram  >> 8),
                          (u8)s_fog_col_ram,  0xFF };
    GXColor kfog_vert = { (u8)(s_fog_col_vert >> 16), (u8)(s_fog_col_vert >> 8),
                          (u8)s_fog_col_vert, 0xFF };
    GX_SetTevKColor(GX_KCOLOR2, kfog_ram);
    GX_SetTevKColor(GX_KCOLOR3, kfog_vert);
  }

  if (offset_fix)
  {
    // TEV stage 1 implements the PVR offset term: PIX = stage0 + OffsetCol.
    //   color = CPREV + RASC(COLOR1A1)   (a=RASC, b=0, c=0, d=CPREV)
    //   alpha = APREV                    (offset alpha is unused on real PVR)
    // Configured once here; the per-polygon loop below only toggles the
    // stage count between 1 and 2, so polys without offset color keep the
    // exact single-stage output (and fill rate) they had before.
    GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
    GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_RASC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
    GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
    GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
    GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
  }
  GX_SetNumTevStages(tev_base);

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
  // Get vertex ptr: the background polygon is always a 3-vertex triangle
  // (v0, v1, v2) stored back-to-back starting at index tag_offset within the
  // strip. The header word at strip_base is the ISP/TSP instruction for all
  // 3 (background has no PCW; Texture/Offset/UV_16b come straight from it).
  u32 vertex_ptr = strip_vert_num * strip_vs + strip_base + 3 * 4;

  ISP_TSP bg_isp;
  bg_isp.full = vri(strip_base);
  TSP bg_tsp;
  bg_tsp.full = vri(strip_base + 4);
  TCW bg_tcw;
  bg_tcw.full = vri(strip_base + 8);

  // decode_pvr_vertex gives the raw hardware fields: x,y in screen space and
  // z = the PVR's raw "1/W" register. Keep them raw here — the barycentric
  // weights below must be computed in this same screen-space (x,y), matching
  // bgCorner (also raw screen space). The W pre-scale vert_base() applies to
  // regular geometry (x*W, y*W, z=W) is applied per corner further down,
  // after interpolating raw 1/W — not here, or the weights and the final
  // screen space end up in mismatched scales.
  // v0 alone is always decoded: it feeds the fast EFB-clear color below
  // regardless of BG_POLY_FIX().
  Vertex bgV[3] = {};
  Vertex bgQuad[4] = {};
  decode_pvr_vertex(strip_base, vertex_ptr, &bgV[0]);

  const bool bg_poly_fix = BG_POLY_FIX();
  if (bg_poly_fix)
  {
    for (int bgi = 1; bgi < 3; bgi++)
      decode_pvr_vertex(strip_base, vertex_ptr + bgi * strip_vs, &bgV[bgi]);

    if (DEBUG_MESSAGE())
    {
      printf("[BG] tag_addr=%06X isp=%08X(Tex=%d Off=%d Gour=%d) tsp=%08X(UseAlpha=%d) tcw=%08X(addr=%06X fmt=%d)\n",
             real_tag_address, bg_isp.full, bg_isp.Texture, bg_isp.Offset, bg_isp.Gouraud,
             bg_tsp.full, bg_tsp.UseAlpha, bg_tcw.full,
             (bg_tcw.NO_PAL.TexAddr << 3) & VRAM_MASK, bg_tcw.NO_PAL.PixelFmt);
      for (int vi = 0; vi < 3; vi++)
        printf("[BG] v%d x=%.2f y=%.2f raw_z(1/W)=%.6f uv=(%.3f,%.3f) col=%08X\n",
               vi, bgV[vi].x, bgV[vi].y, bgV[vi].z, bgV[vi].u, bgV[vi].v, bgV[vi].col);
    }

    // Extrapolate every dynamic attribute (Z, base color, offset color, UV)
    // across the whole screen using the plane defined by the 3 background
    // vertices — this is what the PVR2 rasterizer itself does: it treats the
    // "triangle" as an infinite plane rather than clipping to its edges.
    // (Real background polys carry no Area1/two-volume UV or color: that pair
    // only exists on regular ISP/TSP polygons with PCW.Volume set, and
    // ISP_BACKGND_T's fixed vertex layout never includes it.)
    struct { float x, y; } bgCorner[4] =
    {
      { 0.f,               0.f                }, // Top-Left
      { 0.f,               dc_height          }, // Bottom-Left
      { dc_width,          0.f                }, // Top-Right
      { dc_width,          dc_height          }, // Bottom-Right
    };

    float bg_denom = (bgV[1].y - bgV[2].y) * (bgV[0].x - bgV[2].x)
                   + (bgV[2].x - bgV[1].x) * (bgV[0].y - bgV[2].y);
    if (bg_denom == 0.f)
      bg_denom = 1e-6f; // degenerate background triangle guard

    const bool bg_force_opaque = !bg_tsp.UseAlpha; // TSP.UseAlpha==0 → force alpha=255

    for (int i = 0; i < 4; i++)
    {
      float px = bgCorner[i].x, py = bgCorner[i].y;
      float w0 = ((bgV[1].y - bgV[2].y) * (px - bgV[2].x) + (bgV[2].x - bgV[1].x) * (py - bgV[2].y)) / bg_denom;
      float w1 = ((bgV[2].y - bgV[0].y) * (px - bgV[2].x) + (bgV[0].x - bgV[2].x) * (py - bgV[2].y)) / bg_denom;
      float w2 = 1.f - w0 - w1;

      // Interpolate the raw 1/W register affinely (matches the hardware's
      // plane-equation approach), then apply vert_base()'s W pre-scale.
      float raw_z  = bg_lerp_f32(bgV[0].z, bgV[1].z, bgV[2].z, w0, w1, w2);
      float safe_z = (raw_z >= 0.0001f) ? raw_z : 0.0001f;
      float W      = 1.0f / safe_z;

      if (!g_fixed_depth_cached)
      {
        if (W > 0.0f && W < vtx_min_Z) vtx_min_Z = W;
        if (W > 0.0f && W > vtx_max_Z) vtx_max_Z = W;
      }

      Vertex &qv = bgQuad[i];
      qv.x   = px * W;
      qv.y   = py * W;
      qv.z   = W;
      qv.u   = bg_lerp_f32(bgV[0].u, bgV[1].u, bgV[2].u, w0, w1, w2);
      qv.v   = bg_lerp_f32(bgV[0].v, bgV[1].v, bgV[2].v, w0, w1, w2);
      qv.col = bg_lerp_color(bgV[0].col, bgV[1].col, bgV[2].col, w0, w1, w2, bg_force_opaque);
      qv.spc = bg_lerp_color(bgV[0].spc, bgV[1].spc, bgV[2].spc, w0, w1, w2, false);
    }
  }

  // Fast EFB clear using v0's color: cheap fallback that covers the whole
  // screen instantly, before the precisely-interpolated quad below is drawn
  // (with correct depth) on top of it.
  GXColor bgColor = {
    (u8)(bgV[0].col & 0xFFu),         // R
    (u8)((bgV[0].col >> 8) & 0xFFu),  // G
    (u8)((bgV[0].col >> 16) & 0xFFu), // B
    (u8)((bgV[0].col >> 24) & 0xFFu)  // A
  };
  GX_SetCopyClear(bgColor, 0x00000000);

#if LOGO_DEBUG_LOG
  {
    static u32 s_logo_last_bg = 0x12345678u; // never a plausible first value
    if ((u32)bgV[0].col != s_logo_last_bg)
    {
      printf("[LOGO] BGCOL change frame=%u %08X -> %08X (BACKGND_T=%08X D=%08X)\n",
             (u32)FrameCount, s_logo_last_bg, (u32)bgV[0].col,
             (u32)ISP_BACKGND_T, (u32)ISP_BACKGND_D);
      fflush(stdout);
      s_logo_last_bg = (u32)bgV[0].col;
    }
  }
#endif

#if LOGO_DEBUG_LOG
  // The single most important line for a "whole screen is one flat colour"
  // bug: bgColor is what GX_SetCopyClear paints everywhere the scene does not
  // cover, and it comes straight from the background polygon's first vertex.
  // If this reads FFFFFFFF the screen is white before a single triangle is
  // drawn, and nothing downstream can be blamed for it.
  if (LOGO_ARMED())
  {
    printf("[LOGO] bg CLEAR rgba=%02X%02X%02X%02X (from v0.col=%08X) bg_poly_fix=%d "
           "tag=%06X skip=%u isp=%08X tsp=%08X tcw=%08X tex=%d useA=%d shad=%u "
           "src=%u dst=%u ignA=%u\n",
           (unsigned)bgColor.r, (unsigned)bgColor.g, (unsigned)bgColor.b,
           (unsigned)bgColor.a, (u32)bgV[0].col, (int)bg_poly_fix,
           real_tag_address, real_skip,
           (u32)bg_isp.full, (u32)bg_tsp.full, (u32)bg_tcw.full,
           (int)bg_isp.Texture, (int)bg_tsp.UseAlpha, (unsigned)bg_tsp.ShadInstr,
           (unsigned)bg_tsp.SrcInstr, (unsigned)bg_tsp.DstInstr,
           (unsigned)bg_tsp.IgnoreTexA);
    // With bg_poly_fix off the renderer decodes v0 alone and floods the
    // screen with its colour. That is only correct for a FLAT background: if
    // ISP.Gouraud is set the three corners carry different colours and the
    // real background is a gradient, of which v0 is just one corner. Decode
    // all three here whatever the preset says, so the log can tell the two
    // apart -- decode_pvr_vertex() only reads VRAM, no side effects, so this
    // does not change a single pixel of what is drawn.
    Vertex logo_bg[3] = {};
    logo_bg[0] = bgV[0];
    for (int lvi = 1; lvi < 3; lvi++)
    {
      if (bg_poly_fix)
        logo_bg[lvi] = bgV[lvi];
      else
        decode_pvr_vertex(strip_base, vertex_ptr + lvi * strip_vs, &logo_bg[lvi]);
    }
    for (int lvi = 0; lvi < 3; lvi++)
      printf("[LOGO] bg v%d x=%.2f y=%.2f 1/W=%.6f uv=(%.3f,%.3f) col=%08X spc=%08X%s\n",
             lvi, logo_bg[lvi].x, logo_bg[lvi].y, logo_bg[lvi].z,
             logo_bg[lvi].u, logo_bg[lvi].v,
             (u32)logo_bg[lvi].col, (u32)logo_bg[lvi].spc,
             (lvi && !bg_poly_fix) ? "  (read for this log only - not drawn)" : "");
    if (!bg_poly_fix)
      printf("[LOGO] bg gouraud=%d: only v0 feeds the flat clear. If v1/v2 "
             "differ from v0 the background is a GRADIENT and the flat clear "
             "is wrong - bg_poly=on is the test.\n", (int)bg_isp.Gouraud);
    // Provenance: PARAM_BASE + tag_address*4 is where the background's
    // ISP/TSP/TCW triple was read from, and vertex_ptr where v0 came from.
    // If those words do not look like a plausible parameter block, the white
    // is a misread rather than the game's intent.
    printf("[LOGO] bg src PARAM_BASE=%08X param_base=%06X strip_base=%06X "
           "vs=%u vertex_ptr=%06X words:",
           (u32)PARAM_BASE, param_base, strip_base, strip_vs, vertex_ptr);
    for (u32 lwi = 0; lwi < 8; lwi++)
      printf(" %08X", vri(strip_base + lwi * 4));
    printf("\n");
    printf("[LOGO] bg vtx words:");
    for (u32 lwi = 0; lwi < 8; lwi++)
      printf(" %08X", vri(vertex_ptr + lwi * 4));
    printf("\n");
    fflush(stdout);
  }
#endif

  GX_SetZMode(GX_TRUE, GX_GEQUAL, GX_TRUE);
  GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
  GX_SetAlphaUpdate(GX_TRUE);
  GX_SetColorUpdate(GX_TRUE);


  /*
  // This algorithm performs 4x4 matrix vertex transformation followed by W-buffer depth normalization.
  // keep in mind that the Wii's Paired Singles (floating point unit) is very fast at these $4 \times 4$ multiplications!
  // Still needed ? we have "Mtx44 mtx" code, it that the same stuff ?
  
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

  // FIXED_DEPTH: vert_base skipped the per-vertex tracking this frame
  // (vtx_min_Z / vtx_max_Z still hold reset_vtx_state()'s sentinels), so
  // load the fixed planes here. WIDE reuses the proven sanitize-fallback
  // range (what a degenerate frame already renders with, e.g. the Bomberman
  // intro FMV); TIGHT trades total range for far better Z-buffer precision
  // on typical scenes — geometry closer than W=0.1 or farther than W=25000
  // gets clipped, so it is strictly a per-game setting.
  // LEGACY_DEPTH:
  float p5, p6;
  if (LEGACY_DEPTH())
  {
    vtx_min_Z = 0.001f;             // NEAR
    vtx_max_Z = 10000.0f * 1.001f;  // FAR, extended to not clip vtx_max verts

    p6 = -1.0f / (1.0f / vtx_max_Z - 1.0f / vtx_min_Z);
    p5 = p6 / vtx_min_Z;
  }
  else
  {
    if (FIXED_DEPTH_WIDE())
    {
      vtx_min_Z = 0.0001f;
      vtx_max_Z = 100000.0f;
    }
    else if (FIXED_DEPTH_TIGHT())
    {
      vtx_min_Z = 0.1f;
      vtx_max_Z = 25000.0f;
    }

    // Allow W to be much smaller to push the far plane out for massive environments (like racing games)
    // Important : Keep 0.0001f ! 0.001f is not enough
    if (vtx_min_Z <= 0.0001f)
      vtx_min_Z = 0.0001f;

    if (vtx_max_Z < 0 || vtx_max_Z > 128 * 1024)
      vtx_max_Z = 100000.0f;

    // Extra guard: if EFB garbage or NaN slipped through...
    // Z range so that min >= max, the projection math below produces NaN/inf
    // which desyncs the GX FIFO ("GFX Fifo Opcode unknown 0x64").
    // Reset to a safe default range so the frame renders without a crash.
    // Don't remove this part or Bomberman intro FMV will not display on real hardware
    if (vtx_min_Z >= vtx_max_Z || vtx_max_Z != vtx_max_Z || vtx_min_Z != vtx_min_Z)
    {
      vtx_min_Z = 0.0001f;
      vtx_max_Z = 100000.0f;  
    }

    // extend range
    vtx_max_Z *= 1.001; // to not clip vtx_max verts
    // Near-side mirror of the 1.001 above. In dynamic mode the nearest layer
    // (typically the 2D UI/menu, which DEFINES vtx_min_Z) maps exactly onto the
    // near clip plane; XF float rounding on real hardware then pushes the whole
    // quad just outside and it is clipped away — Dolphin doesn't Z-clip, so it
    // still shows there. The margin keeps that layer strictly inside the range.
    if (DEPTH_CLIP_MARGIN())
      vtx_min_Z *= 0.999f;

    // convert to [-1 .. 0]
    p6 = -1 / (1 / vtx_max_Z - 1 / vtx_min_Z);
    p5 = p6 / vtx_min_Z;
  }

#if LOGO_DEBUG_LOG
  // Frame-level state the per-strip lines are interpreted against: the canvas
  // the projection maps onto, the depth range every W gets squeezed into, and
  // the handful of presets that can turn a correct submission into a wrong
  // picture (depth mode, punch-through/sort routing, alpha, texture cache).
  if (LOGO_ARMED())
    printf("[LOGO] frame canvas=%.0fx%.0f nearW=%.5f farW=%.2f p5=%.6f p6=%.6f "
           "| presets: tex_cache=%d fixed_depth=%d legacy_depth=%d depth_clip=%d "
           "hud_pass=%d punch_through=%d trans_sort=%d autosort=%d bg_poly_fix=%d "
           "adv_alpha=%d blend_mode=%d decal_alpha=%d offset_col=%d "
           "graphics=%d accuracy=%d frameskip=%d trans_zwrite=%d(no_zw=%d) "
           "ppz_write=%d isp_depth_func=%d isp_cull=%d sprite_color=%d "
           "vtx_alpha=%d"
           " | game_presets section matched: '%s'\n",
           dc_width, dc_height, vtx_min_Z, vtx_max_Z, p5, p6,
           get_texture_cache_preset(), get_fixed_depth_preset(),
           (int)LEGACY_DEPTH(), get_depth_clip_preset(),
           HUD_PASS(), (int)PUNCH_THROUGH_FIX(), (int)TRANS_SORT(), AUTOSORT(),
           (int)BG_POLY_FIX(), (int)ADVANCED_ALPHA(), (int)BLEND_MODE(),
           (int)DECAL_ALPHA_FIX(), (int)OFFSET_COLOR_FIX(),
           get_graphism_preset(), get_accuracy_preset(), get_frameskip_preset(),
           get_trans_zwrite_preset(), (int)TRANS_NO_ZWRITE(),
           (int)PER_POLYGON_Z_WRITE(),
           ISP_DEPTH_FUNC(), ISP_CULL(), (int)SPRITE_COLOR(),
           (int)VTX_ALPHA_HONOR(), get_matched_preset_name());
#endif
  // The projection matrix maps DC screen-space coords to GX clip space.
  // X aspect ratio is NOT corrected here — DC vertices are already in screen
  // space (x=[0..640]) and z is 1/W (depth), so a perspective matrix would
  // incorrectly couple x and z. X is remapped at vertex submission instead.
  Mtx44 mtx =
      {
          {(2.f / dc_width), 0, +1, 0},
          {0, -(2.f / dc_height), -1, 0},
          {0, 0, p5, p6},
          {0, 0, -1, 0}
        };

  // NOCLIP: disable XF clipping so real hardware matches Dolphin — polys with
  // depth outside the near/far range get their Z clamped instead of vanishing.
  // Safe here because W is always > 0 (vert_base clamps 1/W to >= 0.0001) so
  // no poly ever crosses w=0, and X/Y stay in DC screen space so the
  // rasterizer guard band + scissor still handle off-screen geometry.
  //
  // GX_SetClipMode() is NOT a cheap register poke: it makes the XF unit flush
  // and resync its vertex pipeline, so issuing it unconditionally every frame
  // (even to reapply the same mode) stalled the GPU and cost ~40% FPS across
  // the board, regardless of the preset value. Only call it on the frame the
  // mode actually changes (menu toggle), same caching approach as the other
  // *_cached preset flags in this file.
  {
    static u8 s_last_clip_mode = 0xFF; // sentinel: forces the very first frame to set it
    u8 want_clip_mode = DEPTH_CLIP_NOCLIP() ? GX_CLIP_DISABLE : GX_CLIP_ENABLE;
    if (want_clip_mode != s_last_clip_mode)
    {
      GX_SetClipMode(want_clip_mode);
      s_last_clip_mode = want_clip_mode;
    }
  }

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

  // FMV Strip Debug
  // commented to prevent unecessary if/else in main render loop
  /*
  if (fmv_debug)
  {
    // Full, unfiltered dump of every strip this frame: catches anything a
    // narrower instrumentation point (e.g. only logging inside EndPolyStrip)
    // could miss, and ties each strip to its actual texture/format state.
    Vertex *dvtx = vertices;
    PolyParam *dmod = listModes;
    u32 strip_idx = 0;
    for (const VertexList *dlst = lists; dlst != crLST; dlst++, strip_idx++)
    {
      s32 raw_cnt   = dlst->count;
      bool new_param = raw_cnt < 0;
      s32 vcnt      = raw_cnt & 0x7FFF;
      bool textured = dmod->pcw.Texture != 0;
      u32 fmt       = textured ? (u32)dmod->tcw.NO_PAL.PixelFmt : 0xFF;
      u32 addr      = textured ? ((dmod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK) : 0;

      float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
      for (s32 i = 0; i < vcnt; i++)
      {
        if (dvtx[i].x < minx) minx = dvtx[i].x;
        if (dvtx[i].x > maxx) maxx = dvtx[i].x;
        if (dvtx[i].y < miny) miny = dvtx[i].y;
        if (dvtx[i].y > maxy) maxy = dvtx[i].y;
      }

      printf("[STRIP] #%u %s vtx=%d new_param=%d textured=%d fmt=%u addr=%06X x=[%.1f..%.1f] y=[%.1f..%.1f]\n",
             strip_idx, (dlst == TransLST) ? "TRANS" : "OPAQUE", vcnt, (int)new_param,
             (int)textured, fmt, addr, minx, maxx, miny, maxy);

      if (new_param) dmod++;
      dvtx += vcnt;
    }
    printf("[FRAME] total strips=%u total verts=%u\n", strip_idx, (u32)(curVTX - vertices));
  }
  */

  GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

  GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
  GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);

  int last_textured = -1;  // track texture state to skip redundant GX calls
  int last_tev_stages = tev_base; // offset_fix only: 2 while drawing offset-color
                                  // polys. With fog on, tev_base is already the
                                  // pinned base and the fog stage adds one more.
  // FOG(): mode currently programmed into the fog TEV stage (-1 = unknown, so
  // the next fogged strip reprograms it) and the mode the strip being submitted
  // is drawn with. Both are only read when fog_on; mode 2 = No Fog, which is
  // also the right neutral value for a frame that never sets one.
  int last_fog_mode  = -1;
  int strip_fog_mode = 2;
  int last_alpha_fmt = -1; // -1 = unset
  int last_shad_instr = -1; // -1 = unset; tracks the GX op currently set on TEVSTAGE0 for textured polys
  const bool decal_alpha_fix = DECAL_ALPHA_FIX(); // read once per frame, not per polygon
  bool force_vtx_alpha_opaque = false; // true for 1555/4444: vertex alpha must not kill tex alpha
  bool last_z_write = true; // Per Polygon Z Write algorythm (Beta, untested)
  int  last_z_func  = GX_GEQUAL; // matches the GEQUAL established at frame start (GX_SetZMode above)
  int  last_as_kill = 0; // AUTOSORT() select pass: stage-3 kill input currently set (0=konst, 1=texture alpha)

  // hud_pass: rescue near-clipped 2D HUD strips (see HUD_PASS() note above).
  // Read the toggle and the projection's near plane (final vtx_min_Z, built by
  // the GX_LoadProjectionMtx block above) once per frame — both loop-invariant.
  // scene_near_W is the smallest W the projection includes, so any strip with a
  // vertex nearer than it is exactly what the near plane would clip. hud_pass:
  // 0=off, 1=overlay (no Z-write), 2=protect (Z-write at the near plane).
  const int   hud_pass     = HUD_PASS();
  const float scene_near_W = vtx_min_Z;

  // RTT_CARRY_OVERLAY(): the depth every carried vertex is re-parked to.
  const float clamp_carry_W = scene_near_W * 1.002f;

  // RTT_KEEP_LIST(): read once per frame; last_clip_mod tracks the param in
  // effect for the current strip (strips without a header reuse the previous).
  const bool rtt_keep = RTT_KEEP_LIST();
#if RTT_DEBUG_LOG
  if (s_rtt_pass) { s_rtt_drawn = 0; s_rtt_skipped = 0;
                    s_rtt_bx0 = s_rtt_by0 =  1e30f;
                    s_rtt_bx1 = s_rtt_by1 = -1e30f; }
#endif
  const PolyParam *last_clip_mod = 0;

  // POLY_OFFSET() / SUBPASS_ZCLEAR(): read once per frame, same rationale as
  // every other preset above.
  const int  poly_offset_tier = POLY_OFFSET();
  const bool subpass_zclear   = SUBPASS_ZCLEAR();
  bool hud_zcleared_this_frame = false;

  // Per-polygon ISP state presets, read once per frame (macros at top of file).
  const bool ppz_write      = PER_POLYGON_Z_WRITE();
  const bool trans_no_zwrite = TRANS_NO_ZWRITE();
  const bool vtx_alpha_honor = VTX_ALPHA_HONOR();
  const int  isp_depth_func = ISP_DEPTH_FUNC(); // 0=off 1=OP/PT lists 2=all lists
  const int  isp_cull       = ISP_CULL();       // 0=off 1=on 2=on, swapped winding

  // DC ISP CullMode -> GX cull mode. Mode 0 = no culling; mode 1 "cull if
  // small" (|det| < FPU_CULL_VAL) has no GX equivalent, treated as no culling;
  // modes 2/3 ("cull if negative/positive" determinant) each cull one screen
  // winding. GX's FRONT/BACK naming doesn't follow GL's convention and the
  // effective winding also depends on this renderer's projection, so the 2/3
  // pairing is selectable (ISP_CULL()==2 swaps it) instead of hardcoded.
  u8 dc_cull_to_gx[4] = { GX_CULL_NONE, GX_CULL_NONE, GX_CULL_FRONT, GX_CULL_BACK };
  if (isp_cull == 2) { dc_cull_to_gx[2] = GX_CULL_BACK; dc_cull_to_gx[3] = GX_CULL_FRONT; }
  int last_cull = GX_CULL_NONE; // GX_SetCullMode(GX_CULL_NONE) is the frame default

  // DC TSP blend factor → GX blend factor lookup.
  // SrcInstr: "OtherColor" for a source factor means the framebuffer (dst) color.
  // DstInstr: "OtherColor" for a dest factor means the incoming (src) color.
  static const u8 dc_src_to_gx[8] = {
    GX_BL_ZERO,        // 0 = Zero
    GX_BL_ONE,         // 1 = One
    GX_BL_DSTCLR,      // 2 = OtherColor  (dst color used as src factor)
    GX_BL_INVDSTCLR,   // 3 = InvOtherColor
    GX_BL_SRCALPHA,    // 4 = SrcAlpha
    GX_BL_INVSRCALPHA, // 5 = InvSrcAlpha
    GX_BL_DSTALPHA,    // 6 = DstAlpha
    GX_BL_INVDSTALPHA, // 7 = InvDstAlpha
  };
  static const u8 dc_dst_to_gx[8] = {
    GX_BL_ZERO,        // 0 = Zero
    GX_BL_ONE,         // 1 = One
    GX_BL_SRCCLR,      // 2 = OtherColor  (src color used as dst factor)
    GX_BL_INVSRCCLR,   // 3 = InvOtherColor
    GX_BL_SRCALPHA,    // 4 = SrcAlpha
    GX_BL_INVSRCALPHA, // 5 = InvSrcAlpha
    GX_BL_DSTALPHA,    // 6 = DstAlpha
    GX_BL_INVDSTALPHA, // 7 = InvDstAlpha
  };
  bool in_trans_list = false;
  int last_src_blend = -1;
  int last_dst_blend = -1;

  // TRANS_SORT() state. While ts_active, strips are fetched from
  // trans_sort_recs[] (sorted back-to-front) instead of the sequential
  // lists/vertices/listModes walk; ts_end_* are the walk cursors at the end
  // of the TR range, restored when the sorted range is done so any strips
  // after it (legacy path: a PT list submitted after TR) still line up.
  // AUTOSORT() frame gate: per-pixel depth peeling for the TR range (see the
  // preset doc at the top). RTT passes keep the legacy path (the game reads
  // the copy back immediately; peeling would multiply that cost), and
  // hokuto_hack keeps its curated layer-tier sort (those scenes sit at ONE
  // depth, peeling cannot separate them). Peeling replaces TRANS_SORT() when
  // both are on -- it is the strictly stronger ordering.
  int as_frame_peels = 0;
  if (!s_rtt_pass && TransLST && s_as_ready && !HOKUTO_HACK())
  {
    as_frame_peels = AUTOSORT();
    if (as_frame_peels < 0) as_frame_peels = 0;
    if (as_frame_peels > AS_MAX_PEELS) as_frame_peels = AS_MAX_PEELS;
  }
  if (as_frame_peels)
    as_setup_frame(p5, p6); // texgen matrices + GR16 mask konsts

  const bool trans_sort = (TRANS_SORT() || HOKUTO_HACK()) && !as_frame_peels; // read once per frame (hokuto_hack implies the sort)
  bool ts_active = false;
  int ts_idx = 0;
  int ts_count = 0;
  Vertex *ts_end_vtx = 0;
  PolyParam *ts_end_mod = 0;
  const PolyParam *ts_last_mod = 0; // last state applied while sorted-drawing

  // ── Draw the extrapolated background quad ──────────────────────────────────
  // Must run after the projection/modelview matrices were loaded above: GX is
  // a command stream, so vertex positions submitted here are transformed by
  // whatever matrix load is earliest ahead of them in the FIFO, not whatever
  // is "current" when this C code executes.
  // Gated on BG_POLY_FIX(): off by default, since the extra texture bind +
  // TEV state pinning + polygon draw costs FPS in games that don't need it.
  if (bg_poly_fix)
  {
    // The background draws before any of this frame's real polygons, so it
    // would otherwise inherit whatever GX_SetAlphaCompare/GX_SetZCompLoc
    // state the PREVIOUS frame's last textured polygon left behind
    // (ADVANCED_ALPHA() below sets GX_GREATER,0 + ZCompLoc(FALSE) for many
    // textured polys, and that GX hardware state persists across frame
    // boundaries). Pin down a deterministic state instead.
    GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GX_SetZCompLoc(GX_TRUE);

    if (bg_isp.Texture)
    {
      GX_SetNumTexGens(1);
      GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
      GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);

      PolyParam bgParam = {};
      bgParam.isp = bg_isp;
      bgParam.tsp = bg_tsp;
      bgParam.tcw = bg_tcw;
      SetTextureParams(&bgParam, decal_alpha_fix);
    }
    else
    {
      GX_SetNumTexGens(0);
      GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
      GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    }

    // Match the main loop's rule: TEV stage 1 (offset color add) is only
    // wired up for textured polys that carry an offset color. Keep
    // last_tev_stages in sync so the main loop below (which only reissues
    // GX_SetNumTevStages on a change) doesn't skip re-applying it for the
    // first real polygon.
    // FOG(): the background polygon carries its own TSP, so it picks its own
    // fog mode like any other strip — and it is the one that matters most,
    // being the sky/horizon the scene fades into. Owns the stage count here
    // for the same reason the main loop's fog branch does.
    if (fog_on)
    {
      last_fog_mode = (int)bg_tsp.FogCtrl;
      if (last_fog_mode != 2)
        FogSetStage(fog_stage, last_fog_mode);
      last_tev_stages = tev_base + ((last_fog_mode == 2) ? 0 : 1);
      GX_SetNumTevStages(last_tev_stages);
    }
    else if (offset_fix)
    {
      last_tev_stages = (bg_isp.Texture && bg_isp.Offset) ? 2 : 1;
      GX_SetNumTevStages(last_tev_stages);
    }

    const bool bg_fog_lut = fog_on && (bg_tsp.FogCtrl == 0 || bg_tsp.FogCtrl == 3);

    GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, 4);
    for (int i = 0; i < 4; i++)
    {
      GX_Position3f32(bgQuad[i].x, bgQuad[i].y, -bgQuad[i].z);
      GX_Color1u32(HOST_TO_LE32(bgQuad[i].col));
      if (emit_clr1)
      {
        u32 spc = bgQuad[i].spc;
        if (bg_fog_lut)
          spc = (spc & 0x00FFFFFFu) | (FogTableLookup(bgQuad[i].z) << 24);
        GX_Color1u32(HOST_TO_LE32(spc));
      }
      GX_TexCoord2f32(bgQuad[i].u, bgQuad[i].v);
    }
    GX_End();
  }

  // ── Draw segments ──────────────────────────────────────────────────────────
  // Legacy: one pass over every list in TA submission order (OP, TR, PT), so
  // punch-through polys get drawn last, in the translucent blend state.
  // PUNCH_THROUGH_FIX() with a PT list present splits the walk into three
  // ranges drawn OP → PT → TR, like real PVR: PT polys render as opaque ones
  // gated by a GX alpha test against PT_ALPHA_REF, and translucent polys can
  // then blend over them. lists/vertices/listModes are parallel streams, so
  // each range restarts from the cursors captured in StartList().
  struct DrawSeg
  {
    VertexList *begin;
    const VertexList *end;
    Vertex *vtx;
    PolyParam *mod;
    bool punch_through;
    u8 as_pass; // AUTOSORT(): 0 = normal walk, 1 = select pass, 2 = draw pass
    u8 as_peel; // peel index for as_pass != 0
    u8 as_last; // set on the final draw pass: restore the painter baseline after it
    u8 as_tail; // strips submitted after an autosorted TR range (legacy PT tail)
    u8 overlay; // RTT_CARRY_OVERLAY(): carried pass, drawn flat on top of the frame
    // Strip that switches this segment into the translucent blend state. Normally
    // the frame's TransLST; the carried overlay segment replays its own boundary
    // instead, so its TR strips blend like they did in the pass they came from.
    const VertexList *trans_begin;
  };
  DrawSeg segs[5 + 2 * AS_MAX_PEELS];
  int seg_count = 0;
  memset(segs, 0, sizeof(segs));

  // AUTOSORT(): the TR range is walked 2x per peel (select + draw), so it
  // becomes 2N segments over the same cursors instead of one.
  const VertexList *as_tr_end = 0;
  if (as_frame_peels)
    as_tr_end = (PTLST && PTLST > TransLST) ? PTLST : crLST;

  if (PUNCH_THROUGH_FIX() && PTLST)
  {
    // Opaque: everything before the first of the TR/PT boundaries.
    const VertexList *op_end = crLST;
    if (TransLST && TransLST < op_end) op_end = TransLST;
    if (PTLST < op_end) op_end = PTLST;
    segs[seg_count].begin = carry_frame_lst; segs[seg_count].end = op_end;
    segs[seg_count].vtx = carry_frame_vtx;   segs[seg_count].mod = carry_frame_mod;
    segs[seg_count].punch_through = false; seg_count++;

    segs[seg_count].begin = PTLST;
    segs[seg_count].end = (TransLST && TransLST > PTLST) ? TransLST : crLST;
    segs[seg_count].vtx = PT_VTX;     segs[seg_count].mod = PT_Mod;
    segs[seg_count].punch_through = true; seg_count++;

    if (TransLST && as_frame_peels)
    {
      for (int k = 0; k < as_frame_peels; k++)
      {
        segs[seg_count].begin = TransLST; segs[seg_count].end = as_tr_end;
        segs[seg_count].vtx = TransVTX;   segs[seg_count].mod = TransMod;
        segs[seg_count].as_pass = 1; segs[seg_count].as_peel = (u8)k; seg_count++;
        segs[seg_count].begin = TransLST; segs[seg_count].end = as_tr_end;
        segs[seg_count].vtx = TransVTX;   segs[seg_count].mod = TransMod;
        segs[seg_count].as_pass = 2; segs[seg_count].as_peel = (u8)k;
        segs[seg_count].as_last = (k == as_frame_peels - 1); seg_count++;
      }
    }
    else if (TransLST)
    {
      segs[seg_count].begin = TransLST;
      segs[seg_count].end = (PTLST > TransLST) ? PTLST : crLST;
      segs[seg_count].vtx = TransVTX; segs[seg_count].mod = TransMod;
      segs[seg_count].punch_through = false; seg_count++;
    }
  }
  else if (as_frame_peels)
  {
    // Legacy list order with peeling: everything before TR, the 2N peel
    // walks, then (rare: PT list submitted after TR without the PT fix)
    // whatever follows the TR range, with cursors pre-walked past it.
    segs[seg_count].begin = carry_frame_lst; segs[seg_count].end = TransLST;
    segs[seg_count].vtx = carry_frame_vtx;   segs[seg_count].mod = carry_frame_mod;
    seg_count++;
    for (int k = 0; k < as_frame_peels; k++)
    {
      segs[seg_count].begin = TransLST; segs[seg_count].end = as_tr_end;
      segs[seg_count].vtx = TransVTX;   segs[seg_count].mod = TransMod;
      segs[seg_count].as_pass = 1; segs[seg_count].as_peel = (u8)k; seg_count++;
      segs[seg_count].begin = TransLST; segs[seg_count].end = as_tr_end;
      segs[seg_count].vtx = TransVTX;   segs[seg_count].mod = TransMod;
      segs[seg_count].as_pass = 2; segs[seg_count].as_peel = (u8)k;
      segs[seg_count].as_last = (k == as_frame_peels - 1); seg_count++;
    }
    if (as_tr_end != crLST)
    {
      Vertex *tvtx = TransVTX;
      PolyParam *tmod = TransMod;
      for (const VertexList *l = TransLST; l != as_tr_end; l++)
      {
        s32 c = l->count;
        if (c < 0) tmod++;
        tvtx += (c & 0x7FFF);
      }
      segs[seg_count].begin = (VertexList *)as_tr_end; segs[seg_count].end = crLST;
      segs[seg_count].vtx = tvtx; segs[seg_count].mod = tmod;
      segs[seg_count].as_tail = 1; seg_count++;
    }
  }
  else
  {
    segs[seg_count].begin = carry_frame_lst; segs[seg_count].end = crLST;
    segs[seg_count].vtx = carry_frame_vtx;   segs[seg_count].mod = carry_frame_mod;
    segs[seg_count].punch_through = false; seg_count++;
  }

  // Every segment built above walks the display frame's own lists, so they all
  // flip into the translucent blend state at the frame's TR boundary.
  for (int i = 0; i < seg_count; i++)
    segs[i].trans_begin = TransLST;

  // RTT_CARRY_OVERLAY(): the pass dropped by StartRender goes last, so it
  // composites over the finished frame instead of being overdrawn by it. It
  // replays its own TR boundary (carry_trans_lst) and is marked `overlay`, which
  // re-parks its vertices onto the near plane and draws them GX_ALWAYS with
  // Z-write off — see the per-strip block further down.
  if (carry_lst_end && !s_rtt_pass)
  {
    segs[seg_count].begin = lists;    segs[seg_count].end = carry_lst_end;
    segs[seg_count].vtx = vertices;   segs[seg_count].mod = listModes;
    segs[seg_count].punch_through = false;
    segs[seg_count].overlay = 1;
    segs[seg_count].trans_begin = carry_trans_lst;
    seg_count++;
  }

  for (int seg = 0; seg < seg_count; seg++)
  {
    const bool seg_is_pt = segs[seg].punch_through;
    const bool seg_overlay = segs[seg].overlay != 0;      // RTT_CARRY_OVERLAY()
    const VertexList *const seg_trans_begin = segs[seg].trans_begin;
    const VertexList *const seg_end = segs[seg].end;
    drawLST = segs[seg].begin;
    drawVTX = segs[seg].vtx;
    drawMod = segs[seg].mod;
    ts_active = false; // each segment restarts from its own captured cursors

    if (seg_is_pt)
    {
      // Punch-through: opaque-style draw (no blending, Z write on) with the
      // hardware alpha test real PVR applies to the PT list — a pixel passes
      // only if its final alpha >= PT_ALPHA_REF. ZCompLoc must be FALSE so
      // discarded pixels don't stamp the Z buffer.
      GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
      GX_SetAlphaCompare(GX_GEQUAL, (u8)(PT_ALPHA_REF & 0xFF), GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_FALSE);
    }

    if (seg_overlay)
    {
      // RTT_CARRY_OVERLAY() runs after the display frame's own segments, so it
      // inherits whatever blend state the last of them left behind (TR blending,
      // or a PT alpha test). Restart from the opaque baseline the frame itself
      // starts from — the carried pass gets to switch into blending on its own
      // TR boundary, exactly like it would have in its own render.
      in_trans_list = false;
      last_src_blend = -1;
      last_dst_blend = -1;
      GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

      // Confine the carried pass to the window its own render was clipped to
      // (FB_X_CLIP / FB_Y_CLIP at the drop), mapped DC px -> EFB px with the
      // same transform apply_tile_clip uses. Without this a zoomed scope pass
      // paints over the entire screen instead of sitting inside its circle.
      if (carry_clip_w)
      {
        s32 sx = (s32)(carry_clip_x * s_clip_sx + s_clip_ox);
        s32 sy = (s32)(carry_clip_y * s_clip_sy + s_clip_oy);
        s32 sw = (s32)(carry_clip_w * s_clip_sx);
        s32 sh = (s32)(carry_clip_h * s_clip_sy);
        if (sx < 0) { sw += sx; sx = 0; }
        if (sy < 0) { sh += sy; sy = 0; }
        if (sx + sw > rmode->fbWidth)   sw = rmode->fbWidth   - sx;
        if (sy + sh > rmode->efbHeight) sh = rmode->efbHeight - sy;
        if (sw > 0 && sh > 0)
          GX_SetScissor((u32)sx, (u32)sy, (u32)sw, (u32)sh);
      }
    }

    const int seg_as = segs[seg].as_pass;
    // FOG(): forget which mode the fog stage holds. Segment starts reprogram
    // TEV stages wholesale (AUTOSORT()'s select pass in particular reuses the
    // very stage indices the fog blend lives in), so the first fogged strip of
    // every segment must rebuild it from scratch rather than trust the cache.
    last_fog_mode = -1;
    if (seg_as == 1)
    {
      // ── AUTOSORT select pass k: leave the farthest not-yet-peeled TR
      // fragment per pixel in the Z buffer. ──────────────────────────────
      const bool first_peel = segs[seg].as_peel == 0;

      // The snapshot copy and the Z-init quad below cover the full EFB;
      // per-strip user tile clips re-apply inside the walk.
      if (tileclip_on)
      {
        s_tileclip_applied = 0;
        GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
      }

      // 1. Snapshot the compare reference: opaque(+PT) depth before peel 0,
      // the previous peel's selection before peels 1+ (the draw pass between
      // them writes no Z, so the selection is still intact). The copy filter
      // must not blend rows into a depth snapshot; restore it right after
      // for the end-of-frame display copy.
      GX_SetCopyFilter(GX_FALSE, NULL, GX_FALSE, NULL);
      GX_SetTexCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
      GX_SetTexCopyDst(rmode->fbWidth, rmode->efbHeight, GX_TF_Z8, GX_FALSE);
      GX_CopyTex(s_as_zref_hi, GX_FALSE);
      GX_SetTexCopyDst(rmode->fbWidth, rmode->efbHeight, GX_CTF_Z8M, GX_FALSE);
      GX_CopyTex(s_as_zref_mid, GX_FALSE);
      GX_PixModeSync();
      GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
      GX_InvalidateTexAll(); // fresh snapshot must not hit stale TMEM lines

      // 2. Park the Z buffer just inside the near plane so the GX_LESS walk
      // can hunt for the minimum (= farthest fragment). 1.0005 keeps the
      // quad strictly off the near clip plane (same failure mode
      // DEPTH_CLIP_MARGIN() exists for).
      GX_SetColorUpdate(GX_FALSE);
      GX_SetAlphaUpdate(GX_FALSE);
      GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_TRUE);
      GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
      GX_SetBlendMode(GX_BM_NONE, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
      GX_SetNumTevStages(1);
      GX_SetNumTexGens(1);
      GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
      GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
      as_submit_zquad(dc_width, dc_height, vtx_min_Z * 1.0005f, emit_clr1);

      // 3. Select pipeline: TEV depth-compare kill + minimum-Z walk.
      as_setup_select_tev(first_peel);
      GX_SetNumTexGens(3);
      GX_SetNumTevStages(4);
      GX_SetZMode(GX_TRUE, GX_LESS, GX_TRUE);
      GX_SetZCompLoc(GX_FALSE); // killed fragments must not stamp Z
      GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);

      // Stage-0 order/op are select-managed per strip: force reissue.
      last_textured = -1;
      last_shad_instr = -1;
      last_alpha_fmt = -1;
      last_as_kill = 0; // as_setup_select_tev leaves the no-kill input
      in_trans_list = false; // keeps the per-poly blend block quiet
    }
    else if (seg_as == 2)
    {
      // ── AUTOSORT draw pass k: blend exactly the selected layer. Z EQUAL
      // re-rasterizes the identical geometry to identical depths, so only
      // the fragments the select pass kept (plus true co-planar ties, which
      // blend in submission order like the painter) touch the pixel. ─────
      GX_SetColorUpdate(GX_TRUE);
      GX_SetAlphaUpdate(GX_TRUE);
      GX_SetZMode(GX_TRUE, GX_EQUAL, GX_FALSE);
      GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_TRUE);
      GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
      GX_SetNumTexGens(1);
      GX_SetNumTevStages(1);
      GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
      GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
      if (offset_fix)
      {
        // Reinstate TEV stage 1 (offset color add, see frame setup): the
        // select pass reprograms stages 1-3 for its depth compare.
        GX_SetTevOrder(GX_TEVSTAGE1, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR1A1);
        GX_SetTevColorIn(GX_TEVSTAGE1, GX_CC_RASC, GX_CC_ZERO, GX_CC_ZERO, GX_CC_CPREV);
        GX_SetTevColorOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
        GX_SetTevAlphaIn(GX_TEVSTAGE1, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_APREV);
        GX_SetTevAlphaOp(GX_TEVSTAGE1, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
      }
      last_tev_stages = 1;
      last_textured = -1;
      last_shad_instr = -1;
      last_alpha_fmt = 0; // matches the ALWAYS/ZCompLoc(TRUE) state above
      last_src_blend = -1;
      last_dst_blend = -1;
      in_trans_list = true; // per-poly TSP blend factors apply here
    }
    else if (segs[seg].as_tail)
    {
      // Strips submitted after an autosorted TR range (legacy path: PT list
      // after TR, no PT fix). The peel walk left the Z buffer parked near
      // the near plane; reset it to the far plane so these strips' GEQUAL
      // compare can pass (legacy drew them over TR-written depth).
      GX_SetColorUpdate(GX_FALSE);
      GX_SetAlphaUpdate(GX_FALSE);
      GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_TRUE);
      GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);
      GX_SetNumTevStages(1);
      GX_SetNumTexGens(1);
      GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
      GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
      as_submit_zquad(dc_width, dc_height, vtx_max_Z * 0.9995f, emit_clr1);
      GX_SetColorUpdate(GX_TRUE);
      GX_SetAlphaUpdate(GX_TRUE);
      GX_SetZMode(GX_TRUE, GX_GEQUAL, GX_TRUE);
      last_z_write = true;
      last_z_func = GX_GEQUAL;
      last_textured = -1;
      last_shad_instr = -1;
      last_alpha_fmt = -1;
      // in_trans_list stays true (set by the draw passes): legacy drew
      // these strips in the translucent blend state.
    }

    for (; drawLST != seg_end; drawLST++)
    {
      if (ts_active && ts_idx == ts_count)
      {
        // Sorted TR range fully drawn — resume the sequential walk right
        // after it (only reached when strips follow the TR range in this
        // segment, i.e. legacy path with a PT list submitted after TR).
        ts_active = false;
        drawVTX = ts_end_vtx;
        drawMod = ts_end_mod;
      }

      // TRANS_NO_ZWRITE(): the PT list is effectively opaque and must keep
      // stamping depth. With PUNCH_THROUGH_FIX the segments run OP -> PT -> TR
      // so PT is finished before the TR boundary below ever fires; on the
      // legacy walk PT follows the TR range inside this same segment, so give
      // the write back here or those strips inherit the TR state.
      if (trans_no_zwrite && PTLST && drawLST == PTLST && !last_z_write)
      {
        GX_SetZMode(GX_TRUE, (u8)last_z_func, GX_TRUE);
        last_z_write = true;
      }

      if (drawLST == seg_trans_begin && !seg_as)
      {
        // Enable blending for the translucent list. Blend factors are set
        // per-polygon below based on TSP.SrcInstr / DstInstr.
        // (AUTOSORT() segments start at TransLST too, but manage their own
        // blend/Z state at segment start — hence the !seg_as guard.)
        // seg_trans_begin is TransLST for every normal segment; the
        // RTT_CARRY_OVERLAY() segment replays the boundary its own pass had.
        in_trans_list = true;
        last_src_blend = -1; // force first per-polygon update
        last_dst_blend = -1;
        GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);

        // TRANS_NO_ZWRITE(): stop translucent strips occluding each other (see
        // the macro at the top of the file). Kept in sync with last_z_write so
        // the per-polygon Z block below only reissues GX_SetZMode on a real
        // change, and so a later opaque/PT segment restores writing normally.
        if (trans_no_zwrite && last_z_write)
        {
          GX_SetZMode(GX_TRUE, (u8)last_z_func, GX_FALSE);
          last_z_write = false;
        }

        // Per-polygon ISP state (isp.ZWriteDis, isp.DepthMode, isp.CullMode)
        // is applied in the strip-header block further below, each behind its
        // own preset: PER_POLYGON_Z_WRITE() / ISP_DEPTH_FUNC() / ISP_CULL().
        // ISP_DEPTH_FUNC()==1 deliberately leaves this translucent list on the
        // GEQUAL painter compare (real PVR per-pixel autosort overrides
        // DepthMode for TR anyway); ==2 honors DepthMode here too. All three
        // default OFF — earlier testing (branch feature/per-poly-isp-state)
        // saw FPS regressions and glitches with DepthMode/CullMode always on,
        // so they stay opt-in per game. Suspects if a game regresses:
        // per-poly GX state churn, and the DC CullMode->GX FRONT/BACK winding
        // pairing (try ISP_CULL()==2 to swap it for this projection).

        // The sorter's range math is expressed in TransLST/PTLST/crLST, which
        // describe the DISPLAY frame — meaningless for a carried overlay, whose
        // strips are all re-parked to one depth anyway. Painter order there.
        if (trans_sort && !seg_overlay)
        {
          // Build one record per TR strip by pre-walking the range with the
          // same cursor rules as the draw loop (drawVTX/drawMod are exactly
          // TransVTX/TransMod here), then sort back-to-front. The TR range
          // ends where the PT list starts if PT was submitted after TR
          // (legacy path — with PUNCH_THROUGH_FIX() the segment split already
          // ends this segment there), else at the end of the stream.
          const VertexList *tr_end = (PTLST && PTLST > TransLST) ? PTLST : crLST;
          if (tr_end > seg_end)
            tr_end = seg_end;

          Vertex *wvtx = drawVTX;
          PolyParam *wmod = drawMod;
          // Param in effect if the first TR strip carries no header (should
          // not happen — TA lists start with a global param — but a valid
          // fallback beats reading a NULL mod).
          PolyParam *cur_mod = (wmod > listModes) ? wmod - 1 : wmod;
          const bool isp_tier = HOKUTO_HACK(); // layer tiers at equal depth
          int n = 0;
          for (const VertexList *l = drawLST; l != tr_end; l++)
          {
            s32 c = l->count;
            if (c < 0)
              cur_mod = wmod++;
            c &= 0x7FFF;

            float far_w = 0.0f;
            for (s32 i = 0; i < c; i++)
              if (wvtx[i].z > far_w)
                far_w = wvtx[i].z;

            // hokuto_hack: classify one-depth 2D layers by texture state (see
            // the preset note at the top of the file). ALL VQ shares one
            // tier: within a tier submission order is preserved, so
            // VQ-vs-VQ compositing stays legacy-identical (an address-based
            // low/high VRAM split was tried and broke the character-select
            // screen by reordering VQ against VQ).
            u16 tclass = 0;
            if (isp_tier && cur_mod->pcw.Texture)
            {
              if (cur_mod->tcw.NO_PAL.PixelFmt == 2 && cur_mod->tcw.NO_PAL.VQ_Comp)
                tclass = 2; // VQ stage art: bottom layer
              else if (cur_mod->tcw.NO_PAL.PixelFmt == 6
                    && (cur_mod->tcw.PAL.PalSelect >> 4) == 2)
                tclass = 1; // stage sprites (crowd): above art, below the rest
              // A "any other 8bpp bank + real alpha blend" tier was tried
              // here to move the debris pile behind the crowd, but that
              // exact signature (fmt=6, non-crowd bank, blend 4/5) also
              // matches the fighting characters themselves, so it pulled
              // them behind the crowd too ("crowd ahead of fighters").
              // Reverted — debris needs a narrower signature (its actual
              // palette bank) than format+blend alone can provide.
            }
            else if (isp_tier && !cur_mod->pcw.Texture && c >= 3
                  && cur_mod->tsp.SrcInstr == 1 && cur_mod->tsp.DstInstr == 0
                  && (wvtx[0].col >> 24) == 0xFF)
            {
              // Untextured full-screen REPLACE plate (blend ONE/ZERO, opaque
              // vertex alpha) submitted at the shared depth: a background
              // plate the game expects per-pixel autosort to bury (HnK's
              // "truth of retribution" screen submits a full-screen black
              // one LAST — painter order blacked out the whole scene).
              // Vertex::z holds W; screen coords are x/W, y/W.
              float sminx = 1e9f, smaxx = -1e9f, sminy = 1e9f, smaxy = -1e9f;
              for (s32 i = 0; i < c; i++)
              {
                if (wvtx[i].z <= 0.0f) { sminx = 1e9f; break; } // degenerate W: not a plate
                float iw = 1.0f / wvtx[i].z;
                float sx = wvtx[i].x * iw, sy = wvtx[i].y * iw;
                if (sx < sminx) sminx = sx;
                if (sx > smaxx) smaxx = sx;
                if (sy < sminy) sminy = sy;
                if (sy > smaxy) smaxy = sy;
              }
              // DC frame is 640x480 here; smaller-res games just never match.
              if (sminx <= 1.0f && smaxx >= 639.0f && sminy <= 1.0f && smaxy >= 479.0f)
                tclass = 3; // behind even the stage art
            }

            trans_sort_recs[n].far_w = far_w;
            trans_sort_recs[n].vtx = wvtx;
            trans_sort_recs[n].mod = cur_mod;
            trans_sort_recs[n].count = (u16)c;
            trans_sort_recs[n].tr_class = tclass;
            n++;
            wvtx += c;
          }

          // Scene gate for tier 3 (background plate) ONLY. Tiers 1 (crowd)
          // and 2 (VQ) stay unconditional — this is what v8 did (no gate at
          // all) and char select/presentation both worked. v9/v10 gated
          // tier 1 off outside the battle and that's what actually broke
          // those two screens: disabling tier 1 doesn't fall back to a
          // neutral order, it moves pal-32-47 content into tier 0 where it
          // can now draw over characters that used to sit safely above it
          // (confirmed by A/B: v10 un-gated tier 2 only and fixed
          // presentation's missing character but left char select exactly
          // as broken — tier 1 was the untouched, still-gated variable in
          // both). Tier 3 is the newest, least-proven addition (only ever
          // needed for the battle's "truth of retribution" full-screen
          // plate, itself inside a >=80-VQ list), so it alone stays gated.
          int gate_c2 = 0;
          for (int i = 0; i < n; i++)
            if (trans_sort_recs[i].tr_class == 2) gate_c2++;
          const bool tier_gate_open = gate_c2 >= 80; // 52 (menus) << 80 << 119 (battle)
          if (!tier_gate_open)
            for (int i = 0; i < n; i++)
              if (trans_sort_recs[i].tr_class == 3)
                trans_sort_recs[i].tr_class = 0;

#if HOKUTO_DEBUG_LOG
          // hokuto_hack debug (see HOKUTO_DEBUG_LOG above): same approach
          // that found stage 1 and 2's debris clusters — log every tier-2
          // (VQ) strip's address and screen bbox, no address filter (each
          // stage's addresses are different — see the "VRAM address is
          // scene/stage dependent" caveat on hokuto_is_debris_tex() below).
          // Gated to tier_gate_open (the >=80-VQ battle-scene gate) and
          // throttled to 1-in-120 frames so a full playthrough doesn't
          // flood the log with menus/cutscenes or near-identical frames.
          // Look for the same signature that gave the first two stages
          // away: a small, tightly-clustered group of addresses whose
          // screen position doesn't fit the main background grid's
          // sprawling, mostly-off-screen pattern.
          static int s_hokuto_frame_throttle = 0;
          if (tier_gate_open && (s_hokuto_frame_throttle++ % 120) == 0)
          {
            for (int i = 0; i < n; i++)
            {
              TransStripRec &r = trans_sort_recs[i];
              if (r.tr_class != 2)
                continue;
              float sminx = 1e9f, smaxx = -1e9f, sminy = 1e9f, smaxy = -1e9f;
              for (u16 j = 0; j < r.count; j++)
              {
                if (r.vtx[j].z <= 0.0f) continue;
                float iw = 1.0f / r.vtx[j].z;
                float sx = r.vtx[j].x * iw, sy = r.vtx[j].y * iw;
                if (sx < sminx) sminx = sx;
                if (sx > smaxx) smaxx = sx;
                if (sy < sminy) sminy = sy;
                if (sy > smaxy) smaxy = sy;
              }
              u32 dbg_addr = (r.mod->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK;
              printf("[HOKUTO_STAGE] n=%d addr=%06X src=%d dst=%d x=%.0f..%.0f y=%.0f..%.0f\n",
                     i, dbg_addr, r.mod->tsp.SrcInstr, r.mod->tsp.DstInstr,
                     sminx, smaxx, sminy, smaxy);
            }
          }
#endif

          if (n > 1)
            qsort(trans_sort_recs, n, sizeof(TransStripRec), trans_strip_cmp);

          ts_idx = 0;
          ts_count = n;
          ts_end_vtx = wvtx;   // walk-end cursors, restored after the range
          ts_end_mod = wmod;
          ts_last_mod = 0;     // force state application on the first strip
          ts_active = n > 0;
        }
      }

      s32 count;
      PolyParam *stripMod = 0; // non-NULL → apply this polygon's render state below
      if (ts_active)
      {
        // Sorted translucent strip: fetched out of submission order, so the
        // count sign bit can't drive state changes anymore — re-apply render
        // state whenever the strip's param differs from the last one applied.
        const TransStripRec &r = trans_sort_recs[ts_idx++];
        count = r.count;
        drawVTX = r.vtx;
        if (r.mod != ts_last_mod)
        {
          stripMod = r.mod;
          ts_last_mod = r.mod;
        }
      }
      else
      {
        count = drawLST->count;
        if (count < 0)
        {
          stripMod = drawMod++;
          count &= 0x7FFF;
        }
      }

      // ── RTT_KEEP_LIST(): split the shared list by user tile clip ──────────
      // The two renders of one list are not meant to draw the same thing. The
      // game marks the half that belongs in the texture with a mode-2 (INSIDE)
      // user tile clip confining it to the target window; the rest — HUD,
      // reticle, the disc that samples the texture — carries no clip and
      // belongs on screen. Without this split the texture render also bakes in
      // the on-screen overlay, and the display render paints the magnified
      // half into the screen's top-left corner, where it Z-fights the real
      // scene (identical W values, different XY: an unwinnable fight, and
      // exactly what the game encoded the tile clip to prevent).
      //   RTT pass      -> draw ONLY tile-clipped strips.
      //   display pass  -> draw everything EXCEPT the tile-clipped strips the
      //                    RTT pass already consumed (strips submitted after
      //                    it are the new half and always draw).
      if (rtt_keep && stripMod)
        last_clip_mod = stripMod;
      bool strip_skip = false;
      if (rtt_keep && last_clip_mod)
      {
        const bool tile_clipped =
            ((listTileClip[last_clip_mod - listModes] >> 30) & 3) == 2;
        strip_skip = s_rtt_pass ? !tile_clipped
                                : (tile_clipped && drawLST < rtt_lst_end);
      }

      // SUBPASS_ZCLEAR(): the FIRST time this frame a strip actually needs
      // hud_pass rescue, re-park the whole Z buffer at the far plane before
      // any of its state (texture/TEV/blend) is set up below — a clean depth
      // baseline for the HUD/cockpit layer instead of whatever the main 3D
      // scene left behind. Must run BEFORE the stripMod block, since
      // ClearDepthOnlyPass reprograms TEV/texgen state that block also
      // touches; the cache invalidation below forces it to be reissued
      // fresh for this strip afterward. Cheap: one extra vertex scan, only
      // for the single strip (if any) that triggers it per frame. Matches
      // "don't share a depth pass with the main scene" for games that
      // submit their HUD as one trailing run of strips (the common DC
      // pattern: 3D scene, then 2D-in-3D overlay). Caveat: if a game
      // interleaves HUD strips back into the middle of its 3D geometry, this
      // would wipe the main scene's depth early — opt-in and per-game like
      // every other preset here, off by default.
      if (hud_pass && subpass_zclear && !hud_zcleared_this_frame && !seg_as && count)
      {
        bool needs_rescue = false;
        for (int i = 0; i < count; i++)
          if (drawVTX[i].z < scene_near_W) { needs_rescue = true; break; }
        if (needs_rescue)
        {
          ClearDepthOnlyPass(dc_width, dc_height, vtx_max_Z, emit_clr1);
          hud_zcleared_this_frame = true;
          // Force full state reissue for this strip: ClearDepthOnlyPass
          // reprogrammed TEV stages/texgens/alpha-compare out from under it.
          last_textured   = -1;
          last_shad_instr = -1;
          last_alpha_fmt  = -1;
          last_tev_stages = 1;
        }
      }

      if (stripMod)
      {
        // Honor the polygon's user tile clip (GX scissor). Cached inside
        // apply_tile_clip(), so params sharing the same clip cost one compare.
        // Skipped on a carried overlay: that segment already owns the scissor
        // (its pass's clip window), and a mode-0 strip clip would re-open it.
        if (tileclip_on && !seg_overlay)
          apply_tile_clip(listTileClip[stripMod - listModes]);

        if (seg_as == 1)
        {
        // ── AUTOSORT select pass per-strip state ───────────────────────────
        // Only position/Z coverage matters here (colors are masked), plus the
        // polygon's texture on stage 0 so fully transparent texels can be
        // alpha-killed instead of eating a peel layer. Everything else
        // (blend, advanced alpha, per-poly Z state) stays untouched: the
        // compare chain on stages 1-3 owns the pipeline.
        int is_textured = stripMod->pcw.Texture ? 1 : 0;
        if (is_textured != last_textured)
        {
          if (is_textured)
          {
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
          }
          else
          {
            GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
            GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
          }
          last_shad_instr = -1;
          last_textured = is_textured;
        }
        bool as_kill = false;
        if (is_textured)
        {
          SetTextureParams(stripMod, decal_alpha_fix);
          // Under decal_alpha_fix the caller owns stage 0's op — the select
          // pass always modulates (texA*vtxA feeds the kill).
          if (decal_alpha_fix && last_shad_instr != GX_MODULATE)
          {
            GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
            last_shad_instr = GX_MODULATE;
          }
          // Same vertex-alpha forcing as the draw pass, so the kill sees the
          // alpha the fragment will actually blend with.
          u32 fmt = stripMod->tcw.NO_PAL.PixelFmt;
          if (vtx_alpha_honor)
            // Hardware rule only: UseAlpha==0 is the DC's own "ignore vertex
            // alpha". No format-based override, so a game that fades via vertex
            // alpha on an ARGB1555 surface actually fades.
            force_vtx_alpha_opaque = !stripMod->tsp.UseAlpha;
          else if (RGB565_OPAQUE_ALPHA())
            force_vtx_alpha_opaque = (fmt == 0 || fmt == 1) || !stripMod->tsp.UseAlpha;
          else
            force_vtx_alpha_opaque = (fmt == 0) || !stripMod->tsp.UseAlpha;
          // Kill only where "alpha == 0 => invisible" provably holds: plain
          // modulate shading with a straight src-alpha blend. Any other
          // combination (decal alpha, dst-color factors, IgnoreTexA) can
          // show alpha-0 fragments, which then must occupy their layer.
          as_kill = stripMod->tsp.ShadInstr == 1 && !stripMod->tsp.IgnoreTexA
                 && stripMod->tsp.SrcInstr == 4
                 && (stripMod->tsp.DstInstr == 5 || stripMod->tsp.DstInstr == 1);
        }
        else
          force_vtx_alpha_opaque = false;
        if ((int)as_kill != last_as_kill)
        {
          GX_SetTevAlphaIn(GX_TEVSTAGE3, GX_CA_ZERO, GX_CA_ZERO,
                           as_kill ? GX_CA_APREV : GX_CA_KONST, GX_CA_ZERO);
          last_as_kill = (int)as_kill;
        }
        }
        else
        {
        // ── Normal / AUTOSORT-draw per-strip state ─────────────────────────
        int is_textured = stripMod->pcw.Texture ? 1 : 0;
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
            last_shad_instr = -1; // force TEVSTAGE0 op to be reapplied on the next textured poly
          }
          last_textured = is_textured;
        }

        // FOG(): pick up this polygon's TSP.FogCtrl and point the fog TEV
        // stage at it. The stage is appended after the frame's pinned base
        // stages, so a No-Fog polygon (mode 2) simply drops back to the base
        // count and rasterizes with the legacy pipeline.
        //
        // With fog on this also OWNS the stage count, which is why the
        // offset_fix branch below is an else: stage 1 stays pinned for the
        // whole frame there (see tev_base) so the fog stage index never moves.
        if (fog_on)
        {
          strip_fog_mode = (int)stripMod->tsp.FogCtrl;
          if (strip_fog_mode != last_fog_mode)
          {
            if (strip_fog_mode != 2)
              FogSetStage(fog_stage, strip_fog_mode);
            last_fog_mode = strip_fog_mode;
          }
          const int tev_stages = tev_base + ((strip_fog_mode == 2) ? 0 : 1);
          if (tev_stages != last_tev_stages)
          {
            GX_SetNumTevStages(tev_stages);
            last_tev_stages = tev_stages;
          }
        }
        // Enable TEV stage 1 (offset color add, configured once above) only
        // for textured polys that actually carry an offset color, so
        // everything else keeps single-stage fill rate.
        else if (offset_fix)
        {
          int tev_stages = (is_textured && stripMod->pcw.Offset) ? 2 : 1;
          if (tev_stages != last_tev_stages)
          {
            GX_SetNumTevStages(tev_stages);
            last_tev_stages = tev_stages;
          }
        }

        if (is_textured)
        {
          SetTextureParams(stripMod, decal_alpha_fix);

          // TSP.ShadInstr==2 (DecalAlpha) needs GX_DECAL instead of GX_MODULATE on
          // TEVSTAGE0 (see DECAL_ALPHA_FIX() above for why). Gated on decal_alpha_fix
          // so the off path never reads tsp.ShadInstr here — SetTextureParams() above
          // already set GX_MODULATE unconditionally either way.
          if (decal_alpha_fix)
          {
            int tev_op = (stripMod->tsp.ShadInstr == 2) ? GX_DECAL : GX_MODULATE;
            if (tev_op != last_shad_instr)
            {
              GX_SetTevOp(GX_TEVSTAGE0, tev_op);
              last_shad_instr = tev_op;
            }
          }

          // Real PVR hardware decides this per-polygon via TSP.UseAlpha, not pixel format.
          // UseAlpha==0 means the hardware forces vertex alpha to 1.0 before modulation
          // (vertex alpha must not be allowed to multiply texture alpha to zero).
          // UseAlpha==1 means vertex alpha is used as supplied.
          // Regression fix: ARGB1555 (fmt 0) and RGB565 (fmt 1) textures are commonly
          // drawn with vertex alpha left at 0 even when UseAlpha==1 (e.g. Test Drive 6's
          // menu/HUD text, an ARGB1555 cutout font): the game relies on the texture's
          // own alpha, not the vertex alpha, to gate visibility. Always force opaque for
          // these two formats; defer to TSP.UseAlpha for everything else (e.g. ARGB4444).
          u32 fmt = stripMod->tcw.NO_PAL.PixelFmt;
          if (vtx_alpha_honor)
            // Hardware rule only: UseAlpha==0 is the DC's own "ignore vertex
            // alpha". No format-based override, so a game that fades via vertex
            // alpha on an ARGB1555 surface actually fades.
            force_vtx_alpha_opaque = !stripMod->tsp.UseAlpha;
          else if (RGB565_OPAQUE_ALPHA())
            force_vtx_alpha_opaque = (fmt == 0 || fmt == 1) || !stripMod->tsp.UseAlpha;
          else
            force_vtx_alpha_opaque = (fmt == 0) || !stripMod->tsp.UseAlpha;

          // This is more accurate for alpha. May cost CPU cycles.
          // Skipped in the punch-through segment: its alpha test (GEQUAL
          // PT_ALPHA_REF, set once at segment start) must not be overwritten.
          if (ADVANCED_ALPHA() && !seg_is_pt)
          {
            int alpha_fmt = 0;
            if (BLEND_MODE()) {
              // fps_boost gains even 2 FPS in Castlevania but the alpha isn't correct anymore
              bool blend_fps_boost = BLEND_FPS_BOOST();
              bool blend_alpha_independent = (stripMod->tsp.SrcInstr < 4 && stripMod->tsp.DstInstr < 4);
              if (blend_fps_boost && drawLST != TransLST) { // New in alpha 0.39
                  alpha_fmt = 0;
              } else if (decal_alpha_fix && stripMod->tsp.ShadInstr == 2) { // Was there in alpha0.38
                  alpha_fmt = 0;
              } else if (blend_alpha_independent) { // New in alpha 0.39
                  alpha_fmt = 0;
              } else if (stripMod->tsp.IgnoreTexA) { // Was there in alpha0.38
                  alpha_fmt = 0;
              } else {
                  alpha_fmt = 1; // Was 0 before
              }
            } else { // Legacy Behavior (correct for Chuchurocket & max FPS)
              alpha_fmt = (decal_alpha_fix && stripMod->tsp.ShadInstr == 2) ? 0 : (stripMod->tsp.IgnoreTexA ? 0 : 1);
            }

            if (alpha_fmt != last_alpha_fmt)
            {
              if (alpha_fmt)
              {
                GX_SetAlphaCompare(GX_GREATER, 0, GX_AOP_AND, GX_ALWAYS, 0);
                GX_SetZCompLoc(GX_FALSE);
              }
              else
              {
                GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
                GX_SetZCompLoc(GX_TRUE);
              }
              last_alpha_fmt = alpha_fmt;
            }
          }
        }
        else
        {
          // Untextured polygon — vertex alpha is meaningful as-is, never override it.
          force_vtx_alpha_opaque = false;
        }

        // ── Per-polygon Z state (ISP.ZWriteDis + ISP.DepthMode) ──────────────
        // Real DC hardware honors both per polygon, and GX carries the compare
        // func and the write mask in a single GX_SetZMode() call, so the two
        // are cached together and re-issued only when either changes.
        //   ppz_write      (PER_POLYGON_Z_WRITE): honor ZWriteDis, else always
        //                  write (fixes e.g. Test Drive 6 sprites).
        //   isp_depth_func (ISP_DEPTH_FUNC): 0=off (GEQUAL painter, legacy),
        //                  1=honor DepthMode on the OP/PT lists only (TR stays
        //                  GEQUAL, real PVR autosorts there), 2=every list.
        // See the macro comments at the top of the file for the DepthMode->GX
        // func 1:1 mapping and why the compare direction is preserved.
        // AUTOSORT() draw passes pin Z to EQUAL/no-write — per-poly Z state
        // must not disturb that (real PVR ignores DepthMode under autosort
        // for the TR list anyway).
        if ((ppz_write || isp_depth_func) && !seg_as)
        {
          bool z_write = ppz_write ? !stripMod->isp.ZWriteDis : true;
          // TRANS_NO_ZWRITE() has to win over the polygon's own ZWriteDis:
          // the preset's whole statement is "the TR list must not stamp
          // depth", and these polys carry ZWriteDis=0. Without this,
          // ppz_write (default ON) re-enables the write on every strip and
          // silently undoes the boundary setting above.
          if (trans_no_zwrite && in_trans_list) z_write = false;
          int z_func = GX_GEQUAL;
          if (isp_depth_func == 2 || (isp_depth_func == 1 && !in_trans_list))
            z_func = (int)stripMod->isp.DepthMode;
          if (z_write != last_z_write || z_func != last_z_func)
          {
            GX_SetZMode(GX_TRUE, (u8)z_func, z_write ? GX_TRUE : GX_FALSE);
            last_z_write = z_write;
            last_z_func  = z_func;
          }
        }
        }

        // ── Per-polygon backface culling (ISP.CullMode) ──────────────────────
        // Fix inside-out geometry in games that lean on hardware culling
        // instead of submitting both windings. Table + swap option built once
        // per frame above (dc_cull_to_gx / isp_cull). Shared by the AUTOSORT()
        // select AND draw passes: both must rasterize identical coverage or
        // the EQUAL-depth draw pass loses its selected fragments.
        if (isp_cull)
        {
          int cull = dc_cull_to_gx[stripMod->isp.CullMode];
          if (cull != last_cull)
          {
            GX_SetCullMode((u8)cull);
            last_cull = cull;
          }
        }

        // Per-polygon blend mode: match TSP.SrcInstr / DstInstr for the translucent
        // Absolutely necessary for Resident Evil 3
        if (BLEND_MODE() && in_trans_list)
        {
          int src = (int)stripMod->tsp.SrcInstr;
          int dst = (int)stripMod->tsp.DstInstr;
          if (src != last_src_blend || dst != last_dst_blend)
          {
            GX_SetBlendMode(GX_BM_BLEND, dc_src_to_gx[src], dc_dst_to_gx[dst], GX_LO_CLEAR);
            last_src_blend = src;
            last_dst_blend = dst;
          }
        }
      }

      // ── hud_pass: rescue a near-clipped 2D HUD strip ─────────────────────
      // Re-park every vertex sitting nearer than the projection's near plane
      // exactly onto the plane. x,y are W-prescaled (screen x = x/W), so scaling
      // x,y,z by the same ratio keeps the screen position bit-identical while
      // moving the vertex just inside the near plane, where the XF unit no longer
      // clips it. The strip is then drawn GX_ALWAYS + Z-write off so it clears
      // the tight range and composites on top. Gated to non-AUTOSORT segments
      // (those own the Z pipeline) and to strips that actually cross the plane,
      // so it is one min-Z compare per strip when on and untouched when off.
      bool hud_strip = false;
      if (seg_overlay && count)
      {
        // RTT_CARRY_OVERLAY(): this strip was submitted for a pass we never
        // rendered, so its depth means nothing in this frame's projection —
        // it can sit anywhere, including outside the near/far planes, where
        // the XF unit would clip it away entirely (the "we see nothing at
        // all" case). Re-park the whole strip onto the near plane with the
        // hud_pass math (x,y are W-prescaled, so scaling x,y,z by the same
        // ratio keeps the screen position bit-identical) and draw it
        // GX_ALWAYS + no Z-write: it can neither be hidden by the scene
        // already in the EFB nor stamp depth that would hide what follows.
        // Blending still comes from the strip's own TSP, so a translucent
        // scope surround composites as it did in its own pass.
        // Screen position is NOT touched: the carried pass submits ordinary
        // full-screen-space coordinates, and the window it was meant to be
        // confined to is applied as a GX scissor at segment start instead.
        for (int i = 0; i < count; i++)
        {
          const float r = clamp_carry_W / drawVTX[i].z;
          drawVTX[i].x *= r;
          drawVTX[i].y *= r;
          drawVTX[i].z  = clamp_carry_W;
        }
        GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_FALSE);
        hud_strip = true;  // reuse the per-strip Z-state restore below
      }
      else if (hud_pass && !seg_as && count)
      {
        const float clamp_W = scene_near_W * 1.002f; // land just inside the near plane
        for (int i = 0; i < count; i++)
        {
          if (drawVTX[i].z < scene_near_W)
          {
            const float r = clamp_W / drawVTX[i].z;
            drawVTX[i].x *= r;
            drawVTX[i].y *= r;
            drawVTX[i].z  = clamp_W;
            hud_strip = true;
          }
        }
        // mode 2 writes the near-plane Z it just clamped to, so later scene
        // geometry (farther) fails GEQUAL over these pixels and the HUD stays
        // on top; mode 1 leaves Z untouched (pure overlay).
        if (hud_strip)
          GX_SetZMode(GX_TRUE, GX_ALWAYS, hud_pass == 2 ? GX_TRUE : GX_FALSE);
      }

      // POLY_OFFSET(): PT list only — nudge co-planar decals/shadows/road-
      // marks a hair closer than the opaque geometry beneath them (see the
      // macro doc at the top of the file), so they win the depth tie instead
      // of flickering. Runs before the hud_pass rescue above already ran
      // (which only touches strips that crossed the near plane), so the two
      // never fight over the same vertices in the common case.
      if (seg_is_pt && poly_offset_tier > 0 && count)
        ApplyPolyOffset(drawVTX, count, poly_offset_tier);

#if RTT_DEBUG_LOG
      if (s_rtt_pass && count)
      {
        if (strip_skip) s_rtt_skipped++;
        else
        {
          s_rtt_drawn++;
          // Screen bbox of what this pass really rasterises — if it never
          // covers the clip window the grab is black for a boring reason.
          for (int i = 0; i < count; i++)
          {
            const float z = drawVTX[i].z;
            if (z <= 0.0f) continue;
            const float iw = 1.0f / z;
            const float sx = drawVTX[i].x * iw, sy = drawVTX[i].y * iw;
            if (sx < s_rtt_bx0) s_rtt_bx0 = sx;
            if (sx > s_rtt_bx1) s_rtt_bx1 = sx;
            if (sy < s_rtt_by0) s_rtt_by0 = sy;
            if (sy > s_rtt_by1) s_rtt_by1 = sy;
          }
        }
      }
#endif
#if LOGO_DEBUG_LOG
      // What GX was ACTUALLY told for this strip, as opposed to what the DC
      // asked for (the [LOGO] #nnn lines above). The gap between the two is
      // where a correct submission turns into a wrong picture: a blend factor
      // that never got applied because BLEND_MODE() is off, a Z func still on
      // the frame-wide GEQUAL painter compare because isp_depth_func is off,
      // a strip withheld by the RTT list split, or vertex alpha forced opaque.
      // -1 means "never overridden this frame, still the frame-start default".
      if (LOGO_ARMED()
          && (s_logo_draw_n < LOGO_MAX_STRIPS
              || (stripMod && stripMod->pcw.Texture
                  && logo_is_hunted_tex(stripMod))))
      {
        // Vertex::z holds W (view depth, larger = farther). The projection
        // maps it to NDC z = -p5 + p6/W, which GX turns into a depth that
        // INCREASES toward the camera -- so the frame-wide GX_GEQUAL means
        // "nearer or equal wins". Printing it makes the ordering between
        // strips readable instead of something to work out from W by hand.
        float zn0 = 1e30f, zn1 = -1e30f;
        for (int lzi = 0; lzi < count; lzi++)
        {
          const float zw = drawVTX[lzi].z;
          const float zn = (zw != 0.0f) ? (-p5 + p6 / zw) : 0.0f;
          if (zn < zn0) zn0 = zn;
          if (zn > zn1) zn1 = zn;
        }
        printf("[LOGO] draw#%03u n=%d skip=%d tex=%d trans=%d pt=%d as=%d hud=%d "
               "zfunc=%d zwrite=%d blend=%d/%d tev_stages=%d alpha_fmt=%d "
               "vtxA_forced=%d cull=%d clr1=%d W=%.5f..%.5f ndcZ=%.4f..%.4f\n",
               s_logo_draw_n, count, (int)strip_skip, last_textured,
               (int)in_trans_list, (int)seg_is_pt, (int)seg_as, (int)hud_strip,
               last_z_func, (int)last_z_write, last_src_blend, last_dst_blend,
               last_tev_stages, last_alpha_fmt, (int)force_vtx_alpha_opaque,
               last_cull, (int)emit_clr1,
               count ? drawVTX[0].z : 0.0f, count ? drawVTX[count - 1].z : 0.0f,
               zn0, zn1);
        s_logo_draw_n++;
      }
#endif
      if (strip_skip)
      {
        // Not for this render (see the RTT_KEEP_LIST() split above). State was
        // still applied, so the next drawn strip inherits a correct pipeline;
        // only the vertices are withheld, and the cursor advances past them.
        drawVTX += count;
      }
      else if (count)
      {
        // For ARGB1555/4444 textures the DC game may leave vertex alpha = 0,
        // expecting the hardware to ignore it. Force alpha = 0xFF so GX_MODULATE
        // does not multiply the texture alpha away. force_vtx_alpha_opaque is
        // loop-invariant (it only changes with the strip's PolyParam), so fold
        // it into an unconditional OR mask and keep the per-vertex loops
        // branch-free.
        const u32 alpha_or = force_vtx_alpha_opaque ? 0xFF000000u : 0u;

        // SEAM_FIX(): inset this strip's UV rectangle by half a texel on every
        // side, so GX_LINEAR taps stay inside the sprite's own texels (no wrap /
        // atlas-neighbour bleed → no black seam), matching what GX_NEAR does for
        // free. Affine per axis: u' = u*su + ou, mapping [umin,umax] onto
        // [umin+½texel, umax-½texel]. su=1/ou=0 (identity) when the preset is off,
        // when the texture size is unknown, or when the strip is flat in that
        // axis, so the else-path below stays bit-for-bit the legacy output.
        f32 su = 1.0f, ou = 0.0f, sv = 1.0f, ov = 0.0f;
        if (seam_fix)
        {
          f32 umin = drawVTX->u, umax = drawVTX->u;
          f32 vmin = drawVTX->v, vmax = drawVTX->v;
          for (int i = 1; i < count; i++)
          {
            const f32 u = drawVTX[i].u, v = drawVTX[i].v;
            if (u < umin) umin = u; else if (u > umax) umax = u;
            if (v < vmin) vmin = v; else if (v > vmax) vmax = v;
          }
          const f32 uspan = umax - umin, vspan = vmax - vmin;
          if (uspan > 1e-5f && uspan > 2.0f * g_seam_half_u)
          { su = (uspan - 2.0f * g_seam_half_u) / uspan; ou = umin + g_seam_half_u - su * umin; }
          if (vspan > 1e-5f && vspan > 2.0f * g_seam_half_v)
          { sv = (vspan - 2.0f * g_seam_half_v) / vspan; ov = vmin + g_seam_half_v - sv * vmin; }
        }

        // FOG(): only the two LUT modes have to be evaluated here — per-vertex
        // fog (mode 1) already carries its coefficient in the offset colour's
        // alpha, exactly where the fog TEV stage reads it from, and No Fog
        // (mode 2) has no stage to feed. Hoisted out of the loop so the
        // ordinary path keeps its branch-free inner loop.
        const bool fog_lut = (strip_fog_mode == 0 || strip_fog_mode == 3);

        GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, count);
        if (emit_clr1)
        {
          // VCD carries CLR1 this frame: every vertex must submit it, in
          // attribute order POS, CLR0, CLR1, TEX0.
          if (fog_lut)
          {
            // Overwrite CLR1's alpha with the table's coefficient for this
            // vertex's depth. The offset colour's own alpha is unused by the
            // offset TEV stage (it passes APREV through), so nothing is lost.
            while (count--)
            {
              GX_Position3f32(drawVTX->x, drawVTX->y, -drawVTX->z);
              GX_Color1u32(HOST_TO_LE32(drawVTX->col | alpha_or));
              GX_Color1u32(HOST_TO_LE32((drawVTX->spc & 0x00FFFFFFu)
                                        | (FogTableLookup(drawVTX->z) << 24)));
              GX_TexCoord2f32(drawVTX->u * su + ou, drawVTX->v * sv + ov);
              drawVTX++;
            }
          }
          else
          while (count--)
          {
            GX_Position3f32(drawVTX->x, drawVTX->y, -drawVTX->z);
            GX_Color1u32(HOST_TO_LE32(drawVTX->col | alpha_or));
            GX_Color1u32(HOST_TO_LE32(drawVTX->spc));
            GX_TexCoord2f32(drawVTX->u * su + ou, drawVTX->v * sv + ov);
            drawVTX++;
          }
        }
        else
        while (count--)
        {
          GX_Position3f32(drawVTX->x, drawVTX->y, -drawVTX->z);
          GX_Color1u32(HOST_TO_LE32(drawVTX->col | alpha_or));
          GX_TexCoord2f32(drawVTX->u * su + ou, drawVTX->v * sv + ov);
          drawVTX++;
        }
        GX_End();
      }

      // hud_pass: undo the per-strip ALWAYS / no-write override so the next
      // strip resumes the painter baseline. last_z_* is updated too, so if the
      // per-poly Z presets (ppz_write / isp_depth_func) are active the next
      // strip re-applies its own mode from here, exactly like the AUTOSORT tail.
      if (hud_strip)
      {
        // TRANS_NO_ZWRITE(): restore to the baseline this range actually has,
        // not unconditionally to "writing" -- inside the TR range the baseline
        // is no-write, and re-enabling it here would let the rest of the list
        // start occluding again halfway through.
        const bool restore_zw = !(trans_no_zwrite && in_trans_list);
        GX_SetZMode(GX_TRUE, GX_GEQUAL, restore_zw ? GX_TRUE : GX_FALSE);
        last_z_write = restore_zw;
        last_z_func  = GX_GEQUAL;
      }

      // sceGuDrawArray(GU_TRIANGLE_STRIP,GU_TEXTURE_32BITF|GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_3D,count,0,drawVTX);

      //			drawVTX+=count;
    }

    if (seg_is_pt)
    {
      // Undo the PT alpha test before the translucent segment — and before the
      // next frame when there is no translucent list, since GX alpha-compare
      // state persists across frames. This matches the alpha_fmt==0 state of
      // the ADVANCED_ALPHA() block above, so tell its cache about it.
      GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_TRUE);
      last_alpha_fmt = 0;
    }

    if (seg_overlay && carry_clip_w)
    {
      // Re-open the scissor: GX scissor state persists across frames, and this
      // is the last segment, so leaving the scope window applied would clip
      // the NEXT frame down to it.
      GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
      s_tileclip_applied = 0; // apply_tile_clip's cache no longer matches
    }

    if (seg_as == 2 && segs[seg].as_last)
    {
      // Last peel drawn: back to the frame's painter Z baseline for whatever
      // follows (legacy tail segment / next frame's first states).
      GX_SetZMode(GX_TRUE, GX_GEQUAL, GX_TRUE);
      last_z_write = true;
      last_z_func  = GX_GEQUAL;
      GX_SetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
      GX_SetZCompLoc(GX_TRUE);
      last_alpha_fmt = 0;
    }
  }

  // Per-polygon culling may have left GX culling enabled; the 2D present path
  // and next frame's background quad both assume GX_CULL_NONE.
  if (last_cull != GX_CULL_NONE)
    GX_SetCullMode(GX_CULL_NONE);

  // RTT_KEEP_LIST(): a PVR render does NOT consume the TA list — the ISP/TSP
  // just walk the tile arrays, which stay valid until TA_LIST_INIT. So a game
  // may legitimately render the SAME accumulated list twice: once into a
  // texture with one FB_W_SOF1 + clip window, then again to the display with
  // another. Silent Scope does exactly that (see the macro doc at the top of
  // the file). Resetting here after an RTT pass throws away the first half of
  // that list, which is why its scope disc never reached the screen.
  if (s_rtt_pass && RTT_KEEP_LIST())
    rtt_lst_end = curLST;  // everything up to here has been offered to the texture
  else
    reset_vtx_state();

  // ASYNC_RENDER() (display frames only): skip the blocking GX_DrawDone() —
  // the queue + deferred wait happens after GX_CopyDisp below. RTT passes
  // keep the legacy full sync, the game reads the result back right away.
  const bool async_render = ASYNC_RENDER() && !s_rtt_pass;
  if (!async_render)
    GX_DrawDone();

  if (s_rtt_pass)
  {
    // Render-to-texture frame: nothing reaches the display. Copy the EFB back
    // into emulated VRAM at FB_W_SOF1 (and leave the EFB cleared) instead.
    // No present, no fb_stamp_present_magic(): FB_W_SOF1 is a texture here,
    // not the scanout framebuffer.
    rtt_copy_efb_to_vram();
    return;
  }

  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  if (async_render)
  {
    // Queue a draw-done token after the display copy and return without
    // waiting: gx_sync_pending() (next frame / next present path) waits on it
    // and only then flips VI to this buffer. Toggling fb makes the next frame
    // render into the other XFB, so the CPU never copies into the buffer VI
    // is about to scan out — true double buffering.
    s_gx_done_irq = false; // must clear BEFORE queuing the token (irq race)
    GX_SetDrawDone();
    s_gx_pending = true;
    s_gx_pending_fb = fb;
    fb ^= 1;
  }
  else
  {
    // GX_CopyDisp was issued immediately before this block. Wait until GX has
    // completely finished writing the XFB before the CPU draws the FPS text.
    GX_DrawDone();

    DrawXfbFpsText(frameBuffer[fb]);

    VIDEO_SetNextFramebuffer(frameBuffer[fb]);
    VIDEO_Flush();
  }

  s_did_3d_render = true;

  // Mark the VRAM framebuffer as "produced by the PVR / already on screen".
  // VBlank() checks this magic word and only presents the VRAM framebuffer
  // when it has been overwritten by a non-PVR (CPU/2D) writer.
  fb_stamp_present_magic();

  wii_audio_frame();
  // VIDEO_WaitVSync() // vsync speed limit removed — don't block the SH4 thread
}

// ============================
// FRAMEBUFFER PRESENT (VRAM scanout -> GX)
// ============================
//
// Reads the Dreamcast framebuffer out of VRAM (at FB_R_SOF1) as a 640x480
// image and draws it as a fullscreen textured quad, then copies to the Wii
// framebuffer and presents.  Shared by the StartRender 2D-blit path and the
// vblank-driven present in VBlank().
// The DC source framebuffer may be smaller/narrower than the fb2d_tex scratch
// texture (declared at file scope above DoRender, shared with the
// RENDER_TO_TEXTURE() write-back); we decode the real region into the top
// area of it and present only that sub-rect via UVs.

void PresentFramebuffer()
{
  // ASYNC_RENDER(): a queued 3D frame may still be in flight — wait and apply
  // its deferred flip first, or the stale pending flip would override this
  // present on the next DoRender. This path itself stays fully synchronous.
  gx_sync_pending();

  // ── Derive the source framebuffer geometry from FB_R_SIZE / FB_R_CTRL ──────
  // (devcast RenderFramebuffer).  FB_R_SIZE is a raw u32 here:
  //   bits  0-9  fb_x_size, 10-19 fb_y_size, 20-29 fb_modulus.
  u32 fb_x_size  =  FB_R_SIZE        & 0x3FF;
  u32 fb_y_size  = (FB_R_SIZE >> 10) & 0x3FF;
  u32 fb_modulus = (FB_R_SIZE >> 20) & 0x3FF;
  u32 fb_depth   = FB_R_CTRL.fb_depth;

  if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[FB] PresentFramebuffer FB_R_SIZE=%08X (x=%u y=%u mod=%u) depth=%u SOF1=%08X\n",
    (u32)FB_R_SIZE, fb_x_size, fb_y_size, fb_modulus, fb_depth, FB_R_SOF1);

  if (fb_x_size == 0 || fb_y_size == 0)
    return;   // nothing configured to scan out

  // width/modulus expressed in pixels for the 16-bit (0555/565) formats.
  int src_w   = (int)((fb_x_size + 1) << 1);   // pixels per line (16bpp word == 1 pixel)
  int src_h   = (int)(fb_y_size + 1);
  int mod_px  = (int)((fb_modulus > 0 ? (fb_modulus - 1) : 0) << 1); // gap pixels per line
  int bpp     = 2;                              // bytes per pixel (16bpp paths)

  // For 24/32bpp scanout the stride math differs; fall back to treating the
  // pixels as 16bpp so we at least show something rather than a torn line.
  // (DC selfboot 2D content is virtually always 0555/565.)

  // Clamp the decoded region to the scratch texture.
  int cpy_w = src_w > FB2D_W ? FB2D_W : src_w;
  int cpy_h = src_h > FB2D_H ? FB2D_H : src_h;

  u32 addr = (FB_R_SOF1 & 0x00FFFFFF);

  // Decode line-by-line into the 4x4-tiled RGB565 scratch texture, honoring the
  // real source stride (line width + modulus gap).
  for (int y = 0; y < cpy_h; y++)
  {
    u32 line_addr = addr;
    for (int x = 0; x < cpy_w; x++)
    {
      u32 off64 = fast_ConvOffset32toOffset64(line_addr);
      u16 px = *host_ptr_xor((u16*)&params.vram[off64]);

      // Convert 1555 -> 565 when needed so the GX texture format is uniform.
      if (fb_depth != 1) // fbde_0555 (or non-565): RGB555 -> RGB565
      {
        u16 r = (px >> 10) & 0x1F;
        u16 g = (px >>  5) & 0x1F;
        u16 b =  px        & 0x1F;
        px = (u16)((r << 11) | (g << 6) | b);
      }

      // 4x4 tile address inside fb2d_tex.
      u16 *dst = &fb2d_tex[((y / 4) * (FB2D_W / 4) + (x / 4)) * 16
                           + (y % 4) * 4 + (x % 4)];
      *dst = px;

      line_addr += bpp;
    }
    // Advance to the next source line by the full source width + modulus gap,
    // regardless of how much we clamped into the scratch texture.
    addr += (u32)((src_w + mod_px) * bpp);
  }
  DCFlushRange(fb2d_tex, sizeof(fb2d_tex));

  // TMEM_CACHE(): fb2d_tex is re-decoded at the same address every present and
  // the per-frame invalidate in DoRender is off — drop its stale TMEM lines.
  if (TMEM_CACHE())
    GX_InvalidateTexAll();

  // Texture is uniformly RGB565 now.
  GXTexObj texobj;
  GX_InitTexObj(&texobj, fb2d_tex, FB2D_W, FB2D_H, GX_TF_RGB565, GX_CLAMP, GX_CLAMP, GX_FALSE);
  GX_InitTexObjLOD(&texobj, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
  GX_LoadTexObj(&texobj, GX_TEXMAP0);

  // UV sub-rect: only the decoded cpy_w x cpy_h region holds real pixels.
  float u1 = (float)cpy_w / (float)FB2D_W;
  float v1 = (float)cpy_h / (float)FB2D_H;

  // VTXFMT1: XY+UV only, leaves VTXFMT0 (3D path) undisturbed.
  GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_POS,  GX_POS_XY,  GX_F32, 0);
  GX_SetVtxAttrFmt(GX_VTXFMT1, GX_VA_TEX0, GX_TEX_ST,  GX_F32, 0);
  GX_ClearVtxDesc();
  GX_SetVtxDesc(GX_VA_POS,  GX_DIRECT);
  GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
  GX_SetNumChans(0);
  // The 3D path may have left TEV stage 1 active (OFFSET_COLOR_FIX with an
  // offset poly drawn last); with 0 channels its raster input would be junk.
  GX_SetNumTevStages(1);
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

  // A user-tile-clipped strip may have left a sub-rect scissor applied (see
  // apply_tile_clip); this fullscreen quad must not inherit it. Preset-gated
  // so the legacy state flow is untouched.
  if (SPLIT_SCREEN())
  {
    GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
    GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
  }

  // In 4:3 mode, draw the 2D framebuffer into a centred 4:3 sub-rectangle
  // so logo screens and 2D content have the same correct framing as 3D.
  float x0_2d, x1_2d;
  if (RATIO_FULL_WIDTH())
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
    GX_Position2f32(x0_2d,   0); GX_TexCoord2f32(0,  0);
    GX_Position2f32(x1_2d,   0); GX_TexCoord2f32(u1, 0);
    GX_Position2f32(x1_2d, 480); GX_TexCoord2f32(u1, v1);
    GX_Position2f32(x0_2d, 480); GX_TexCoord2f32(0,  v1);
  GX_End();

  GX_DrawDone();
  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  VIDEO_SetNextFramebuffer(frameBuffer[fb]);
  VIDEO_Flush();
  wii_audio_frame();
  // VIDEO_WaitVSync() // Not necessary here (don't block the SH4 thread)

  // Mark these framebuffer contents as shown.  The texture has already been
  // sampled from VRAM above, so stamping the read origin now only affects what
  // the *next* VBlank sees: it will re-present only if the game overwrites the
  // framebuffer (clobbering the marker) before then.
  fb_stamp_present_magic_at(FB_R_SOF1);
}

// ============================
// VBLANK (vblank-driven framebuffer present)
// ============================
//
// Called at the start of every vblank (from SPG.cpp).  devcast presents the
// VRAM framebuffer here whenever no 3D render happened that frame
// (Renderer_if.cpp rend_vblank -> RenderFramebuffer).  We do the same, but
// detect the "needs present" condition via the in-VRAM magic word:
//
//   * If the framebuffer read origin (FB_R_SOF1) still holds FB_PRESENT_MAGIC,
//     the on-screen image is the PVR's last render -> nothing new to show.
//   * If the magic is gone, a non-PVR writer (CPU / 2D blit) has refreshed the
//     framebuffer -> scan it out of VRAM and present it.
void VBlank()
{
  // ASYNC_RENDER(): if the queued frame finished since last vblank, put it on
  // screen now (non-blocking) — without this, a game that stops submitting
  // frames (static menu) would never show its last rendered frame.
  gx_present_if_done();

  if (!FB_R_CTRL.fb_enable)
    return;   // display output disabled

  if (!fb_needs_present())
    return;   // on-screen image is still the PVR render; nothing new

  if (ShouldSkipFrame())
    return;

  if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[PATH] VBlank-FB present: FB_R_SOF1=%08X fb_depth=%d\n",
    FB_R_SOF1, (int)FB_R_CTRL.fb_depth);

  PresentFramebuffer();
  s_did_3d_render = false;
  FrameCount++;
}

// ============================
// SCOPE_DEBUG_LOG: per-render-pass dump ([SS] lines)
// ============================
//
// Games that composite several PVR renders per frame (Silent Scope's sniper
// scope: a bit-24 pass plus the display pass) can't be reasoned about from the
// [PATH] one-liners alone — we need to know which pass owns which geometry.
// This walks ONLY the strips submitted since the previous RENDER_START, so each
// dump describes exactly one pass, and prints where every strip lands on screen.
//
// Triggered by the appearance of a bit-24 pass (plus one baseline burst at boot)
// and rate limited in render passes, so it fires on the scope moment and stays
// quiet the rest of the time. A heartbeat line runs unconditionally.
#if SCOPE_DEBUG_LOG
#define SS_BURST        9   // passes per burst — enough for 2-3 whole frames
#define SS_QUIET_PASSES 250 // render passes of quiet between bursts
#define SS_BOOT_PASSES  12  // always dump this many at startup (baseline)
#define SS_HEARTBEAT    300 // proof-of-life line every N passes, always
#define SS_MAX_STRIPS   48
#define SS_MAX_EXTRA    24  // extra tile-clipped strips shown past the cap

// Everything here is counted in RENDER PASSES, not frames. A frame counter is
// the wrong clock for this: a scene that stalls, frame-skips, or never reaches
// the display path stops advancing FrameCount, and the logger would go silent
// exactly where we need it. Passes always tick.
static u32 s_ss_pass_no     = 0; // every StartRender, ever
static u32 s_ss_burst       = 0; // passes logged in the current burst
static u32 s_ss_quiet_until = 0; // pass number the next burst may start at
static u32 s_ss_seen_all    = 0; // passes since the last heartbeat
static u32 s_ss_seen_b24    = 0; //  ... of which had FB_W_SOF1 bit 24 set
// Last bit-24 render target seen, so a later pass sampling it can be spotted:
// that strip is the one compositing the scope/mirror back onto the screen, and
// it is the single most important line in the whole dump.
static u32 s_ss_rtt_addr    = 0;
static u32 s_ss_rtt_end     = 0;
static const VertexList *s_ss_prev_lst = lists;     // cursors at the PREVIOUS
static const Vertex     *s_ss_prev_vtx = vertices;  // RENDER_START — the start
static const PolyParam  *s_ss_prev_mod = listModes; // of this pass's geometry

static void ss_dump_pass()
{
  // DoRender/reset_vtx_state() rewound the buffers since last time: this pass
  // starts at the origin again.
  if (curLST < s_ss_prev_lst)
  {
    s_ss_prev_lst = lists;
    s_ss_prev_vtx = vertices;
    s_ss_prev_mod = listModes;
  }

  const bool b24 = (FB_W_SOF1 & 0x1000000) != 0;
  s_ss_pass_no++;
  s_ss_seen_all++;
  if (b24)
    s_ss_seen_b24++;

  // Heartbeat: unconditional, cheap, and the single most useful line if the
  // dump itself never appears — it proves StartRender is running and says how
  // many of the passes carry bit 24, i.e. whether there is anything to capture.
  if ((s_ss_pass_no % SS_HEARTBEAT) == 0)
  {
    printf("[SS] alive pass=%u frame=%u passes_since=%u bit24_since=%u rtt=%d\n",
           s_ss_pass_no, (u32)FrameCount, s_ss_seen_all, s_ss_seen_b24,
           get_render_to_texture_preset());
    fflush(stdout);
    s_ss_seen_all = 0;
    s_ss_seen_b24 = 0;
  }

  // Burst trigger: finish one already running, otherwise start one at boot (for
  // a baseline) or the moment a bit-24 pass shows up — which is precisely the
  // scope/mirror moment we care about, and never during ordinary play.
  bool want = (s_ss_burst != 0);
  if (!want && s_ss_pass_no >= s_ss_quiet_until)
    want = b24 || (s_ss_pass_no <= SS_BOOT_PASSES);

  if (!want)
  {
    s_ss_prev_lst = curLST;   // keep the cursors tracking while quiet
    s_ss_prev_vtx = curVTX;
    s_ss_prev_mod = curMod;
    return;
  }

  const u32 xclip = FB_X_CLIP.full;
  const u32 yclip = FB_Y_CLIP.full;

  printf("[SS] === pass=%u (burst %u/%d) frame=%u W_SOF1=%08X bit24=%d R_SOF1=%08X\n",
         s_ss_pass_no, s_ss_burst + 1, SS_BURST, (u32)FrameCount,
         (u32)FB_W_SOF1, b24 ? 1 : 0, (u32)FB_R_SOF1);
  printf("[SS]     W_CTRL=%08X pack=%d stride=%u XCLIP=%u..%u YCLIP=%u..%u tclip=%08X\n",
         (u32)FB_W_CTRL, (int)(FB_W_CTRL & 7),
         (u32)((FB_W_LINESTRIDE & 0x1FF) * 8),
         (u32)(xclip & 0x7FF), (u32)((xclip >> 16) & 0x7FF),
         (u32)(yclip & 0x3FF), (u32)((yclip >> 16) & 0x3FF),
         ta_tileclip);

  const VertexList *l   = s_ss_prev_lst;
  const Vertex     *vtx = s_ss_prev_vtx;
  const PolyParam  *mod = s_ss_prev_mod;
  const PolyParam  *cur = (mod > listModes) ? mod - 1 : mod;

  printf("[SS]     strips=%d verts=%d TRat=%d PTat=%d minW=%.5f maxW=%.2f\n",
         (int)(curLST - l), (int)(curVTX - vtx),
         TransLST ? (int)(TransLST - lists) : -1,
         PTLST    ? (int)(PTLST    - lists) : -1,
         vtx_min_Z, vtx_max_Z);

  // Remember this pass's RTT target so a LATER pass sampling it can be flagged.
  if (b24)
  {
    u32 rw = ((xclip >> 16) & 0x7FF) + 1 - (xclip & 0x7FF);
    u32 rh = ((yclip >> 16) & 0x3FF) + 1 - (yclip & 0x3FF);
    u32 st = (FB_W_LINESTRIDE & 0x1FF) * 8;
    if (st == 0) st = rw * 2;
    s_ss_rtt_addr = FB_W_SOF1 & VRAM_MASK & ~3u;
    s_ss_rtt_end  = s_ss_rtt_addr + st * rh;
  }

  u32 lt_count[8];
  for (int i = 0; i < 8; i++) lt_count[i] = 0;
  u32 tcmode_count[4] = { 0, 0, 0, 0 };
  u32 rtt_sampler_count = 0;
  u32 extra_printed = 0; // tile-clipped strips shown past the head cap

  int idx = 0;
  for (; l != curLST; l++, idx++)
  {
    s32 c = l->count;
    if (c < 0)
      cur = mod++;
    c &= 0x7FFF;

    const u32 tex_addr = (u32)((cur->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK);
    const u32 tc_word  = listTileClip[cur - listModes];
    lt_count[cur->pcw.ListType & 7]++;
    tcmode_count[(tc_word >> 30) & 3]++;

    // Is this strip sampling the last bit-24 render target? That strip is the
    // one compositing the scope/mirror back onto the screen, so it is printed
    // whatever its index — it is the whole point of the capture. Same for any
    // strip carrying a real user tile clip (that is how a game confines a
    // picture-in-picture), which the head cap was hiding.
    const bool samples_rtt = cur->pcw.Texture && s_ss_rtt_end &&
                             tex_addr >= s_ss_rtt_addr && tex_addr < s_ss_rtt_end;
    if (samples_rtt)
      rtt_sampler_count++;

    // Tile-clipped strips can be hundreds per pass; a handful shows where the
    // window is and the summary counts the rest. RTT samplers are never capped.
    bool show_clip = ((tc_word >> 30) & 3) != 0 && extra_printed < SS_MAX_EXTRA;
    if (show_clip)
      extra_printed++;

    if (idx < SS_MAX_STRIPS || samples_rtt || show_clip)
    {
      // Screen bbox: x,y are W-prescaled (screen x = x/W), same math the
      // [HOKUTO_STAGE] dump uses. W is the raw view depth (larger = farther).
      float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
      float minw = 1e30f, maxw = -1e30f;
      for (s32 i = 0; i < c; i++)
      {
        const float z  = vtx[i].z;
        const float iw = (z != 0.0f) ? 1.0f / z : 0.0f;
        const float sx = vtx[i].x * iw, sy = vtx[i].y * iw;
        if (sx < minx) minx = sx;
        if (sx > maxx) maxx = sx;
        if (sy < miny) miny = sy;
        if (sy > maxy) maxy = sy;
        if (z  < minw) minw = z;
        if (z  > maxw) maxw = z;
      }
      const char *cls = (PTLST    && l >= PTLST)    ? "PT"
                      : (TransLST && l >= TransLST) ? "TR" : "OP";
      printf("[SS] %s#%03d %s lt=%d cnt=%2d tex=%d addr=%06X fmt=%d scan=%d st=%d %dx%d "
             "src=%d dst=%d zw=%d dm=%d cull=%d uc=%d x=%.0f..%.0f y=%.0f..%.0f "
             "W=%.5f..%.2f tc=%08X\n",
             samples_rtt ? "**RTT** " : " ",
             idx, cls, (int)cur->pcw.ListType, (int)c, (int)cur->pcw.Texture,
             tex_addr,
             (int)cur->tcw.NO_PAL.PixelFmt, (int)cur->tcw.NO_PAL.ScanOrder,
             (int)cur->tcw.NO_PAL.StrideSel,
             8 << cur->tsp.TexU, 8 << cur->tsp.TexV,
             (int)cur->tsp.SrcInstr, (int)cur->tsp.DstInstr,
             (int)cur->isp.ZWriteDis, (int)cur->isp.DepthMode, (int)cur->isp.CullMode,
             (int)cur->pcw.User_Clip,
             minx, maxx, miny, maxy, minw, maxw,
             tc_word);
    }
    vtx += c;
  }
  if (idx > SS_MAX_STRIPS)
    printf("[SS]  ... %d strips past the head cap (only RTT samplers and "
           "tile-clipped strips among them were listed)\n", idx - SS_MAX_STRIPS);
  printf("[SS]     SUMMARY lt: OP=%u TR=%u PT=%u other=%u | tileclip: off=%u m1=%u "
         "INSIDE=%u OUTSIDE=%u | sampling RTT %06X: %u strips\n",
         lt_count[0], lt_count[2], lt_count[3],
         lt_count[1] + lt_count[4] + lt_count[5] + lt_count[6] + lt_count[7],
         tcmode_count[0], tcmode_count[1], tcmode_count[2], tcmode_count[3],
         s_ss_rtt_addr, rtt_sampler_count);

  s_ss_prev_lst = curLST;
  s_ss_prev_vtx = curVTX;
  s_ss_prev_mod = curMod;

  // stdout is a FILE on the SD card (InitRenderer freopens it to /ndclog.txt),
  // so it is fully buffered — without this, everything logged since the last
  // block boundary is lost if the console is powered off instead of exited
  // cleanly, which is exactly how you leave a stuck scene.
  fflush(stdout);

  if (++s_ss_burst >= SS_BURST)
  {
    s_ss_burst = 0;
    s_ss_quiet_until = s_ss_pass_no + SS_QUIET_PASSES;
  }
}
#endif // SCOPE_DEBUG_LOG

// ============================
// START RENDERING
// ============================

// ─────────────────────────────────────────────────────────────────────────────
// LOGO_DEBUG_LOG: per-render-pass dump ([LOGO] lines)
// See the big comment next to the #define at the top of this file for what
// each line is for. Everything below compiles to nothing when the gate is 0.
// ─────────────────────────────────────────────────────────────────────────────
#if LOGO_DEBUG_LOG

static u32 s_logo_pass_no = 0; // every StartRender, ever
static u32 s_logo_seen    = 0; // passes since the last heartbeat
// Cursors at the PREVIOUS StartRender: everything submitted since then is
// exactly this pass's geometry (the same bookkeeping ss_dump_pass uses).
static const VertexList *s_logo_prev_lst = lists;
static const Vertex     *s_logo_prev_vtx = vertices;
static const PolyParam  *s_logo_prev_mod = listModes;
// Texture addresses already thumbnailed in the current pass, so a scene that
// draws one atlas across twenty strips still costs one dump.
static u32 s_logo_tex_done[LOGO_MAX_TEX];
static u32 s_logo_tex_n = 0;

// Does this parameter reference the texture being hunted? A PVR viewer may
// report either the TCW's raw TexAddr field (8-byte units) or the byte address
// it expands to, and guessing wrong turns the trigger into a silent no-op --
// which is indistinguishable from "the texture was never drawn". Accept both
// readings rather than make the capture depend on that guess.
static INLINE bool logo_is_hunted_tex(const PolyParam *pp)
{
#if LOGO_TRIGGER_TEX
  const u32 byte_addr = (u32)((pp->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK);
  return byte_addr == ((u32)LOGO_TRIGGER_TEX & VRAM_MASK)
      || byte_addr == (((u32)LOGO_TRIGGER_TEX << 3) & VRAM_MASK);
#else
  (void)pp;
  return false;
#endif
}

// One source texel -> 8-bit ARGB. Mirrors the real converters (ABGR1555 /
// ABGR0565 / ABGR4444 near the top of this file) so the thumbnail shows what
// the decoder sees rather than offering a second opinion.
static void logo_texel_argb(u32 fmt, u16 s, int *a, int *r, int *g, int *b)
{
  switch (fmt)
  {
  case 0: // ARGB1555 - one bit of alpha
    *a = (s & 0x8000) ? 255 : 0;
    *r = (((s >> 10) & 31) * 255) / 31;
    *g = (((s >>  5) & 31) * 255) / 31;
    *b = ((  s       & 31) * 255) / 31;
    break;
  case 1: // RGB565 - no alpha channel at all, always opaque
    *a = 255;
    *r = (((s >> 11) & 31) * 255) / 31;
    *g = (((s >>  5) & 63) * 255) / 63;
    *b = ((  s       & 31) * 255) / 31;
    break;
  case 2: // ARGB4444
    *a = (((s >> 12) & 15) * 255) / 15;
    *r = (((s >>  8) & 15) * 255) / 15;
    *g = (((s >>  4) & 15) * 255) / 15;
    *b = ((  s       & 15) * 255) / 15;
    break;
  default: // YUV422 / bump / anything else: the luma byte only. Not colour-
           // exact, but enough to tell "there is a shape in here" from
           // "this block is uniform".
    *a = 255;
    *r = *g = *b = (s >> 8) & 255;
    break;
  }
}

// Print a window of a strip's SOURCE texture as ASCII, read out of emulated
// VRAM. (u0,u1,v0,v1) selects the region: 0,1,0,1 for the whole texture, or the
// strip's own UV rectangle to see exactly the texels that reach the screen --
// which is the only view that means anything for a quad sampling an 8x8 corner
// of a 256x256 atlas.
//   space = transparent texel, '.' = opaque black ... '@' = opaque white.
// A correct SEGA logo is mostly blank with a few bright letters standing out.
// A block that comes back all '@' is a genuinely white texture and explains a
// white screen by itself; all-blank means the polygon has nothing to draw and
// the white must be coming from the background or the blend state instead.
//
// This walks the raw DC data with the same rules texture_TW / texture_VQ /
// Plannar use (Morton twiddle, mip base offset, host_ptr_xor for the DC
// little-endian u16s sitting in big-endian Wii RAM), and deliberately does NOT
// consult the texture cache: it says what the GAME wrote, so a wrong picture
// on screen can be blamed on -- or cleared of -- the decode/cache path.
static void logo_dump_texture(const PolyParam *pp, float u0, float u1,
                              float v0, float v1, const char *what)
{
  const u32  fmt  = pp->tcw.NO_PAL.PixelFmt;
  const bool pal  = (fmt == 5 || fmt == 6);
  const bool isvq = pp->tcw.NO_PAL.VQ_Comp != 0;
  const bool mip  = pp->tcw.NO_PAL.MipMapped != 0;
  // StrideSel / ScanOrder only exist on a non-palettised TCW; those bits are
  // PalSelect otherwise, and reading them there would be nonsense.
  const bool scan = !pal && pp->tcw.NO_PAL.ScanOrder != 0; // 1 = linear/planar
  const bool strd = !pal && pp->tcw.NO_PAL.StrideSel != 0;

  const u32 base = (u32)((pp->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK);
  u32 w = 8u << pp->tsp.TexU;
  u32 h = 8u << pp->tsp.TexV;
  if (mip) h = w;            // mipmapped DC textures are always square
  if (scan && strd) w = 512; // stride-selected surfaces decode 512 px wide

  // FAST/NORMAL/QUALITY stamp 0xDEADBEEF over the first u32 of the source once
  // they have decoded a texture, so seeing it here is not corruption -- it is
  // proof the cache has already been through this address. The first two
  // texels of the thumbnail are that sentinel, not picture data.
  const u32 sentinel = *(u32 *)&params.vram[base & (VRAM_MASK & ~3u)];

  printf("[LOGO]   tex %06X %ux%u fmt=%u(%s) vq=%d scan=%d stride=%d mip=%d "
         "filt=%u clampUV=%u%u flipUV=%u%u mipD=%u palsel=%u first_u32=%08X%s"
         " | window %s: u=%.3f..%.3f v=%.3f..%.3f = texels x %.0f..%.0f y %.0f..%.0f\n",
         base, w, h, (unsigned)fmt,
         fmt == 0 ? "ARGB1555" : fmt == 1 ? "RGB565" : fmt == 2 ? "ARGB4444"
                  : fmt == 3 ? "YUV422" : fmt == 4 ? "BUMP"
                  : fmt == 5 ? "PAL4" : fmt == 6 ? "PAL8" : "?",
         (int)isvq, (int)scan, (int)strd, (int)mip,
         (unsigned)pp->tsp.FilterMode,
         (unsigned)pp->tsp.ClampU, (unsigned)pp->tsp.ClampV,
         (unsigned)pp->tsp.FlipU,  (unsigned)pp->tsp.FlipV,
         (unsigned)pp->tsp.MipMapD,
         pal ? (unsigned)pp->tcw.PAL.PalSelect : 0u,
         sentinel,
         sentinel == 0xDEADBEEF ? " (DEADBEEF = cache sentinel, already decoded)" : "",
         what, u0, u1, v0, v1,
         u0 * w, u1 * w, v0 * h, v1 * h);

  if (pal)
  {
    // A palettised texture is meaningless without its TLUT, and the mip base
    // offset differs per bit depth. Say so instead of printing a lie; the
    // logo polygon this probe was written for is not palettised anyway.
    printf("[LOGO]   (palettised source - thumbnail skipped, needs the TLUT)\n");
    return;
  }

  // Where the full-size level starts. TCW points at the 1x1 mip, levels are
  // packed small->large, and for VQ the codebook stays at the base while only
  // the index map moves -- exactly what twidle_tex() does.
  u32 data_base = base;
  const u32 cb_base = base; // VQ codebook, never moved by the mip offset
  if (isvq)
    data_base = base + (mip ? VQMipPoint[pp->tsp.TexU + 3] : 2048u);
  else if (mip)
    data_base = base + OtherMipPoint[pp->tsp.TexU + 3] * 2u;

  static const char ramp[10] = ".:-=+*#%@";
  char line[LOGO_TEX_COLS + 1];
  u32 n_clear = 0, n_opaque = 0, n_white = 0;
  int lmin = 255, lmax = 0;
  // Distinct source values in the window. A quad that paints one flat colour
  // shows up as a single entry covering every sample, which is the whole
  // question for a full-screen fill.
  u16 hist_val[8];
  u32 hist_cnt[8];
  u32 hist_n = 0, hist_other = 0;

  for (u32 ty = 0; ty < LOGO_TEX_ROWS; ty++)
  {
    for (u32 tx = 0; tx < LOGO_TEX_COLS; tx++)
    {
      // Sample texel centres across the window, clamped inside the texture so
      // a rect ending exactly at 1.0 does not read the row past the last one.
      float fx = (u0 + (u1 - u0) * ((float)tx + 0.5f) / (float)LOGO_TEX_COLS) * (float)w;
      float fy = (v0 + (v1 - v0) * ((float)ty + 0.5f) / (float)LOGO_TEX_ROWS) * (float)h;
      if (fx < 0.0f) fx = 0.0f;
      if (fy < 0.0f) fy = 0.0f;
      u32 x = (u32)fx, y = (u32)fy;
      if (x >= w) x = w - 1;
      if (y >= h) y = h - 1;
      u16 s;
      if (isvq)
      {
        // 2x2 block index, then the block's four codebook words in the same
        // (0,0) (0,1) (1,0) (1,1) order texture_VQ unpacks them.
        const u32 bi   = twop(x & ~1u, y & ~1u, w, h) / 4u;
        const u8  idx  = *host_ptr_xor(&params.vram[(data_base + bi) & VRAM_MASK]);
        const u32 wsel = (x & 1u) * 2u + (y & 1u);
        s = *host_ptr_xor((u16 *)&params.vram[(cb_base + idx * 8u + wsel * 2u) & (VRAM_MASK & ~1u)]);
      }
      else
      {
        const u32 off = scan ? (y * w + x) * 2u : twop(x, y, w, h) * 2u;
        s = *host_ptr_xor((u16 *)&params.vram[(data_base + off) & (VRAM_MASK & ~1u)]);
      }

      u32 hk = 0;
      for (; hk < hist_n; hk++)
        if (hist_val[hk] == s) { hist_cnt[hk]++; break; }
      if (hk == hist_n)
      {
        if (hist_n < 8) { hist_val[hist_n] = s; hist_cnt[hist_n] = 1; hist_n++; }
        else hist_other++;
      }

      int a, r, g, b;
      logo_texel_argb(fmt, s, &a, &r, &g, &b);
      const int lum = (r * 77 + g * 151 + b * 28) >> 8;
      if (lum < lmin) lmin = lum;
      if (lum > lmax) lmax = lum;
      if (a == 0)
      {
        n_clear++;
        line[tx] = ' ';
      }
      else
      {
        n_opaque++;
        if (r > 240 && g > 240 && b > 240) n_white++;
        line[tx] = ramp[(lum * 8) / 255];
      }
    }
    line[LOGO_TEX_COLS] = 0;
    printf("[LOGO]   |%s|\n", line);
  }

  printf("[LOGO]   sampled %u texels: clear=%u opaque=%u pure_white=%u luma=%d..%d\n",
         (unsigned)(LOGO_TEX_COLS * LOGO_TEX_ROWS), n_clear, n_opaque, n_white, lmin, lmax);
  printf("[LOGO]   colours in window (raw -> A R G B):");
  for (u32 hk = 0; hk < hist_n; hk++)
  {
    int ha, hr, hg, hb;
    logo_texel_argb(fmt, hist_val[hk], &ha, &hr, &hg, &hb);
    printf(" %04X=%u(%d,%d,%d,%d)", hist_val[hk], hist_cnt[hk], ha, hr, hg, hb);
  }
  if (hist_other)
    printf(" +%u more", hist_other);
  printf("\n");

  // First eight source u16s verbatim. If the thumbnail looks wrong these say
  // whether the bytes are plausible picture data, all-zero, all-FFFF (white),
  // or byte-swapped.
  printf("[LOGO]   raw u16 @%06X:", data_base);
  for (u32 i = 0; i < 8; i++)
    printf(" %04X", *host_ptr_xor((u16 *)&params.vram[(data_base + i * 2u) & (VRAM_MASK & ~1u)]));
  printf("\n");
}

// One dumped render pass: header, then every strip submitted since the
// previous StartRender, then a thumbnail for the first few distinct textures.
static void logo_dump_pass()
{
  // DoRender/reset_vtx_state() rewound the buffers since last time, so this
  // pass starts from the origin again. Keyed off the rewind COUNTER, not off
  // "curLST moved backwards": a scene that submits the same strip count every
  // frame (this one submits exactly 2) leaves the cursor at the same address
  // pass after pass, and the old test read that as an empty pass.
  static u32 s_logo_seen_seq = 0;
  if (g_vtx_reset_seq != s_logo_seen_seq)
  {
    s_logo_seen_seq = g_vtx_reset_seq;
    s_logo_prev_lst = lists;
    s_logo_prev_vtx = vertices;
    s_logo_prev_mod = listModes;
  }

  s_logo_pass_no++;
  s_logo_seen++;

  // Heartbeat: unconditional and cheap. If the dumps themselves never appear,
  // this line still proves StartRender is running and says how far in we are,
  // which distinguishes "the screen never renders" from "the probe is off".
  if ((s_logo_pass_no % LOGO_HEARTBEAT) == 0)
  {
    printf("[LOGO] alive pass=%u frame=%u since=%u\n",
           s_logo_pass_no, (u32)FrameCount, s_logo_seen);
    fflush(stdout);
    s_logo_seen = 0;
  }

  // Signature trigger: does this pass reference the texture being hunted? One
  // walk over the pass's parameter headers (a few dozen, not the vertices), so
  // it is cheap enough to run on every pass including the quiet ones.
  bool want_sig = false;
#if LOGO_TRIGGER_TEX
  // '<' not '!=': a walk that starts past its end would run through the whole
  // 8K param array.
  for (const PolyParam *m = s_logo_prev_mod; m < curMod; m++)
    if (m->pcw.Texture && logo_is_hunted_tex(m))
    {
      want_sig = true;
      break;
    }
#endif

  // Dump every pass at boot, then sample one in LOGO_EVERY until the window
  // closes -- enough to land several times on any boot logo, and quiet after.
  // The signature trigger overrides all of it, including the closed window.
  s_logo_dump_now = want_sig
                 || (s_logo_pass_no <= LOGO_HEAD_PASSES)
                 || (s_logo_pass_no <= LOGO_LAST_PASS
                     && (s_logo_pass_no % LOGO_EVERY) == 0);
  if (!s_logo_dump_now)
  {
    s_logo_prev_lst = curLST;  // keep the cursors tracking while quiet
    s_logo_prev_vtx = curVTX;
    s_logo_prev_mod = curMod;
    return;
  }

  s_logo_tex_n  = 0;
  s_logo_draw_n = 0;

  const u32 xclip = FB_X_CLIP.full;
  const u32 yclip = FB_Y_CLIP.full;

  printf("[LOGO] === pass=%u frame=%u sig=%d W_SOF1=%08X bit24=%d R_SOF1=%08X "
         "R_CTRL(en=%d depth=%d) W_CTRL=%08X pack=%d stride=%u\n",
         s_logo_pass_no, (u32)FrameCount, (int)want_sig, (u32)FB_W_SOF1,
         (FB_W_SOF1 & 0x1000000) ? 1 : 0, (u32)FB_R_SOF1,
         (int)FB_R_CTRL.fb_enable, (int)FB_R_CTRL.fb_depth,
         (u32)FB_W_CTRL, (int)(FB_W_CTRL & 7),
         (u32)((FB_W_LINESTRIDE & 0x1FF) * 8));
  printf("[LOGO]     XCLIP=%u..%u YCLIP=%u..%u tclip=%08X BORDER_COL=%08X "
         "BACKGND_T=%08X BACKGND_D=%08X ISP_FEED_CFG=%08X (presort bit0=%d)\n",
         (u32)(xclip & 0x7FF), (u32)((xclip >> 16) & 0x7FF),
         (u32)(yclip & 0x3FF), (u32)((yclip >> 16) & 0x3FF),
         ta_tileclip, (u32)VO_BORDER_COL,
         (u32)ISP_BACKGND_T, (u32)ISP_BACKGND_D,
         (u32)ISP_FEED_CFG, (int)((ISP_FEED_CFG & 1) != 0));

  const VertexList *l   = s_logo_prev_lst;
  const Vertex     *vtx = s_logo_prev_vtx;
  const PolyParam  *mod = s_logo_prev_mod;
  const PolyParam  *cur = (mod > listModes) ? mod - 1 : mod;

  printf("[LOGO]     strips=%d verts=%d TRat=%d PTat=%d W=%.5f..%.2f\n",
         (int)(curLST - l), (int)(curVTX - vtx),
         TransLST ? (int)(TransLST - lists) : -1,
         PTLST    ? (int)(PTLST    - lists) : -1,
         vtx_min_Z, vtx_max_Z);

  int idx = 0;
  for (; l != curLST; l++, idx++)
  {
    s32 c = l->count;
    if (c < 0)
      cur = mod++;
    c &= 0x7FFF;

    // The hunted strip is the whole point of the capture: print it whatever
    // its index, and thumbnail its texture even if the per-pass budget is up.
    const u32 strip_tex = cur->pcw.Texture
                        ? (u32)((cur->tcw.NO_PAL.TexAddr << 3) & VRAM_MASK) : 0u;
    const bool is_sig = cur->pcw.Texture && logo_is_hunted_tex(cur);

    if (idx < LOGO_MAX_STRIPS || is_sig)
    {
      // Screen bbox: x,y are W-prescaled, so screen x = x/W (the same maths
      // the [SS] and [HOKUTO_STAGE] dumps use). W is the raw view depth.
      float minx = 1e30f, maxx = -1e30f, miny = 1e30f, maxy = -1e30f;
      float minw = 1e30f, maxw = -1e30f;
      float minu = 1e30f, maxu = -1e30f, minv = 1e30f, maxv = -1e30f;
      for (s32 i = 0; i < c; i++)
      {
        const float z  = vtx[i].z;
        const float iw = (z != 0.0f) ? 1.0f / z : 0.0f;
        const float sx = vtx[i].x * iw, sy = vtx[i].y * iw;
        if (sx < minx) minx = sx;
        if (sx > maxx) maxx = sx;
        if (sy < miny) miny = sy;
        if (sy > maxy) maxy = sy;
        if (z  < minw) minw = z;
        if (z  > maxw) maxw = z;
        if (vtx[i].u < minu) minu = vtx[i].u;
        if (vtx[i].u > maxu) maxu = vtx[i].u;
        if (vtx[i].v < minv) minv = vtx[i].v;
        if (vtx[i].v > maxv) maxv = vtx[i].v;
      }
      const char *cls = (PTLST    && l >= PTLST)    ? "PT"
                      : (TransLST && l >= TransLST) ? "TR" : "OP";

      printf("[LOGO] #%03d %s lt=%d n=%2d tex=%d isp=%08X tsp=%08X tcw=%08X "
             "x=%.0f..%.0f y=%.0f..%.0f W=%.5f..%.2f uv=%.3f..%.3f/%.3f..%.3f "
             "col0=%08X spc0=%08X tc=%08X\n",
             idx, cls, (int)cur->pcw.ListType, (int)c, (int)cur->pcw.Texture,
             (u32)cur->isp.full, (u32)cur->tsp.full, (u32)cur->tcw.full,
             minx, maxx, miny, maxy, minw, maxw,
             minu, maxu, minv, maxv,
             c > 0 ? (u32)vtx[0].col : 0u, c > 0 ? (u32)vtx[0].spc : 0u,
             listTileClip[cur - listModes]);
      // Second line: the DC state words spelled out field by field, because
      // "82400000 / 949004F6" is exactly what a PVR viewer shows, and
      // comparing the two side by side is how a wrong bitfield gets caught.
      printf("[LOGO]      isp: depth=%u cull=%u zwdis=%u tex=%u off=%u gour=%u uv16=%u "
             "| tsp: shad=%u ignA=%u useA=%u src=%u dst=%u ssel=%u dsel=%u fog=%u "
             "cclamp=%u sup=%u | pcw: uv16=%u gour=%u off=%u tex=%u col=%u vol=%u shd=%u\n",
             (unsigned)cur->isp.DepthMode, (unsigned)cur->isp.CullMode,
             (unsigned)cur->isp.ZWriteDis, (unsigned)cur->isp.Texture,
             (unsigned)cur->isp.Offset, (unsigned)cur->isp.Gouraud,
             (unsigned)cur->isp.UV_16b,
             (unsigned)cur->tsp.ShadInstr, (unsigned)cur->tsp.IgnoreTexA,
             (unsigned)cur->tsp.UseAlpha, (unsigned)cur->tsp.SrcInstr,
             (unsigned)cur->tsp.DstInstr, (unsigned)cur->tsp.SrcSelect,
             (unsigned)cur->tsp.DstSelect, (unsigned)cur->tsp.FogCtrl,
             (unsigned)cur->tsp.ColorClamp, (unsigned)cur->tsp.SupSample,
             (unsigned)cur->pcw.UV_16bit, (unsigned)cur->pcw.Gouraud,
             (unsigned)cur->pcw.Offset, (unsigned)cur->pcw.Texture,
             (unsigned)cur->pcw.Col_Type, (unsigned)cur->pcw.Volume,
             (unsigned)cur->pcw.Shadow);

      // Every vertex, not just v0: PCW.Gouraud=0 means the PVR flat-shades
      // from ONE of them, and a per-vertex alpha fade is invisible in v0 alone.
      // col is post-ABGR8888 (RGBA byte order), so alpha is the top byte.
      for (s32 vi = 0; vi < c && vi < 8; vi++)
        printf("[LOGO]      v%d screen=(%.1f,%.1f) W=%.5f uv=(%.4f,%.4f) "
               "col=%08X (a=%u) spc=%08X\n",
               (int)vi,
               vtx[vi].z != 0.0f ? vtx[vi].x / vtx[vi].z : 0.0f,
               vtx[vi].z != 0.0f ? vtx[vi].y / vtx[vi].z : 0.0f,
               vtx[vi].z, vtx[vi].u, vtx[vi].v,
               (u32)vtx[vi].col, (unsigned)(vtx[vi].col >> 24),
               (u32)vtx[vi].spc);

      if (cur->pcw.Texture && (s_logo_tex_n < LOGO_MAX_TEX || is_sig))
      {
        bool seen = false;
        for (u32 k = 0; k < s_logo_tex_n; k++)
          if (s_logo_tex_done[k] == strip_tex) { seen = true; break; }
        if (is_sig)
          printf("[LOGO]   ^^ this is the hunted texture (LOGO_TRIGGER_TEX)\n");
        // The whole texture once (atlas context), then the rectangle this
        // strip actually samples -- for a full-screen quad reading an 8x8
        // corner, only the second one describes what reaches the screen.
        if (!seen)
        {
          if (s_logo_tex_n < LOGO_MAX_TEX)
            s_logo_tex_done[s_logo_tex_n++] = strip_tex;
          logo_dump_texture(cur, 0.0f, 1.0f, 0.0f, 1.0f, "WHOLE TEXTURE");
        }
        logo_dump_texture(cur, minu, maxu, minv, maxv, "THIS STRIP'S UV RECT");
      }
    }
    vtx += c;
  }
  if (idx > LOGO_MAX_STRIPS)
    printf("[LOGO]  ... %d more strips past the head cap\n", idx - LOGO_MAX_STRIPS);

  s_logo_prev_lst = curLST;
  s_logo_prev_vtx = curVTX;
  s_logo_prev_mod = curMod;

  // stdout is a FILE on the SD card (InitRenderer freopens it to /ndclog.txt),
  // so it is fully buffered: without this, everything logged since the last
  // block boundary is lost when the console is powered off instead of exited
  // cleanly -- which is exactly how you leave a stuck screen.
  fflush(stdout);
}
#endif // LOGO_DEBUG_LOG

void StartRender()
{
  u32 VtxCnt = curVTX - vertices;
  VertexCount += VtxCnt;

#if SCOPE_DEBUG_LOG
  ss_dump_pass();   // must run BEFORE any path below returns
#endif

#if LOGO_DEBUG_LOG
  logo_dump_pass(); // same rule: must run BEFORE any path below returns
#endif

  if (RENDER_DELAY())
  {
    // Hardware-like staggered render-done IRQs: ISP, TSP, then Video
    // (originaldave_'s values — fixes games that pace off render-done,
    // e.g. Marvel vs Capcom 2 reporting >100% speed but rendering ~7 FPS).
    // SPG.cpp counts these down and raises each IRQ separately;
    // render_end_pending_cycles must stay 0 so the legacy single-burst
    // path there never double-fires.
    render_isp_pending_cycles = 800000;
    render_tsp_pending_cycles = 850000;
    render_vd_pending_cycles  = 900000;
    render_end_pending_cycles = 0;
  }
  else
  {
    render_end_pending_cycles = VtxCnt * 15;
    if (render_end_pending_cycles < 50000)
      render_end_pending_cycles = 50000;
  }

  // NOTE on frame-skip: the ShouldSkipFrame() checks below must come AFTER
  // render_end_pending_cycles is set so the game's timing loop receives the
  // render-done interrupt on schedule even for skipped frames.  They must
  // also come AFTER the render-to-texture dispatch: RTT passes produce
  // texture data the game reads back, not displayed frames — skipping one
  // corrupts the texture, and letting it advance the FRAMESKIP_1/2 counter
  // can phase-lock the skip onto every *displayed* frame in games that do
  // one RTT pass + one main render per frame.

  if (FB_W_SOF1 & 0x1000000)
  {
    // Render-to-texture: bit 24 in the write address targets the 64-bit
    // texture area (mirrors, TV screens, some menu effects). With the preset
    // on and real geometry submitted, render the frame and copy it back into
    // VRAM at FB_W_SOF1 instead of dropping it / treating it as a 2D present.
    if (RENDER_TO_TEXTURE() && VtxCnt > 0)
    {
      // Target size from the pixel clip registers (extracted manually — see
      // the ISP_BACKGND_T note in DoRender() about bitfields on the Wii).
      u32 xclip = FB_X_CLIP.full;
      u32 yclip = FB_Y_CLIP.full;
      u32 rtt_w = ((xclip >> 16) & 0x7FF) + 1 - (xclip & 0x7FF); // clip max is inclusive
      u32 rtt_h = ((yclip >> 16) & 0x3FF) + 1 - (yclip & 0x3FF);

      // Not every game programs FB_X_CLIP/FB_Y_CLIP for its RTT pass; some
      // leave the previous frame's full-screen clip in place and describe the
      // target through FB_W_LINESTRIDE alone. The old code collapsed anything
      // it didn't like to 8x8, i.e. it rendered a 64-pixel thumbnail and wrote
      // that back as the texture — visually indistinguishable from "RTT does
      // nothing". Derive a usable size instead, and only fall back to 8 when
      // there is genuinely nothing to go on.
      u32 stride_px = ((FB_W_LINESTRIDE & 0x1FF) * 8) / 2; // 8 bytes/unit, 16bpp
      if ((s32)rtt_w < 8 || rtt_w > FB2D_W || rtt_w > (u32)rmode->fbWidth)
        rtt_w = stride_px;                                  // stride wins over a bogus clip
      if ((s32)rtt_w < 8 || rtt_w > FB2D_W || rtt_w > (u32)rmode->fbWidth)
        rtt_w = 8;                                          // nothing usable
      if ((s32)rtt_h < 8)
        rtt_h = 8;
      // Clamp (don't discard) a target taller than what we can render/copy:
      // the top rtt_h lines are still the right pixels, a 8-line stub never is.
      if (rtt_h > FB2D_H)               rtt_h = FB2D_H;
      if (rtt_h > (u32)rmode->efbHeight) rtt_h = (u32)rmode->efbHeight;

      if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[PATH] RTT: FB_W_SOF1=%08X %ux%u packmode=%d stride=%d VtxCnt=%d\n",
        FB_W_SOF1, rtt_w, rtt_h, (int)(FB_W_CTRL & 7), (int)((FB_W_LINESTRIDE & 0x1FF) * 8), VtxCnt);

      s_rtt_x = xclip & 0x7FF;   // clip ORIGIN: the window's position in the
      s_rtt_y = yclip & 0x3FF;   // pass's screen space (usually, but not
      s_rtt_w = rtt_w;           // always, 0,0)
      s_rtt_h = rtt_h;
      s_rtt_pass = true;
      DoRender();
      s_rtt_pass = false;
      return; // not a presented frame: no FrameCount++, display untouched
    }

    // RTT_CARRY_OVERLAY(): don't render this pass to a texture, but hand its
    // geometry to the next display frame as a proper overlay instead of leaving
    // it to leak (see the macro doc at the top of the file). Everything stays in
    // the buffers exactly where it is — only the bookkeeping moves.
    if (RTT_CARRY_OVERLAY() && VtxCnt > 0 && curLST > lists)
    {
      if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[PATH] RTT-carry: FB_W_SOF1=%08X strips=%d VtxCnt=%d TR=%d\n",
        FB_W_SOF1, (int)(curLST - lists), VtxCnt, TransLST ? (int)(TransLST - lists) : -1);

      carry_lst_end   = curLST;
      carry_trans_lst = TransLST;   // may be 0: an all-opaque carried pass

      // The window this pass was rendering into (same registers the real RTT
      // path reads). Only trusted when it is a real sub-rect; a stale
      // full-screen clip leaves carry_clip_w at 0 and nothing is scissored.
      {
        u32 cx = FB_X_CLIP.full;
        u32 cy = FB_Y_CLIP.full;
        u32 cw = ((cx >> 16) & 0x7FF) + 1 - (cx & 0x7FF);
        u32 ch = ((cy >> 16) & 0x3FF) + 1 - (cy & 0x3FF);
        if ((s32)cw >= 8 && cw <= 1280 && (s32)ch >= 8 && ch <= 1024)
        {
          carry_clip_x = cx & 0x7FF;
          carry_clip_y = cy & 0x3FF;
          carry_clip_w = cw;
          carry_clip_h = ch;
        }
      }

      carry_frame_lst = curLST;     // the display frame starts here
      carry_frame_vtx = curVTX;
      carry_frame_mod = curMod;

      // Give the display frame a clean list-boundary and depth-range slate; the
      // carried pass keeps its own copies above. Vertex/list cursors are NOT
      // rewound — that is the whole point, the geometry has to survive.
      TransLST = 0;  TransVTX = 0;  TransMod = 0;
      PTLST    = 0;  PT_VTX   = 0;  PT_Mod   = 0;
      global_regd = false;
      vtx_min_Z = 131072;
      vtx_max_Z = 0;
      return; // no present, no FrameCount++ — this pass was never a frame
    }

    if(FRAMEBUFFER_2D()){
      if (s_did_3d_render)
      {
        if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[PATH] 2D-after-3D: FB_W_SOF1=%08X FB_R_SOF1=%08X fb_depth=%d VtxCnt=%d\n",
          FB_W_SOF1, FB_R_SOF1, (int)FB_R_CTRL.fb_depth, VtxCnt);
        s_did_3d_render = false;
        gx_sync_pending(); // ASYNC_RENDER(): apply the queued frame's flip first
        GX_DrawDone();
        GX_CopyDisp(frameBuffer[fb], GX_TRUE);
        VIDEO_SetNextFramebuffer(frameBuffer[fb]);
        VIDEO_Flush();
        wii_audio_frame();
        // VIDEO_WaitVSync() // Not necessary here (don't block the SH4 thread)
        FrameCount++;
        return;
      }

      if (ShouldSkipFrame())
        return;   // skip the 2D present, same as the VBlank() path

      if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[PATH] 2D-blit: FB_W_SOF1=%08X FB_R_SOF1=%08X fb_depth=%d VtxCnt=%d\n",
        FB_W_SOF1, FB_R_SOF1, (int)FB_R_CTRL.fb_depth, VtxCnt);

      PresentFramebuffer();
      FrameCount++;
      return;
    } else {
      return; // just return
    }
  }

  // ── Frame-skip early-out (3D display frames only, see NOTE above) ─────────
  if (ShouldSkipFrame())
  {
    reset_vtx_state();   // discard this frame's geometry; free the buffers
    return;              // no GX calls — saves ~10-15 ms on a heavy frame
  }

  if(DEBUG_MESSAGE() || LOGO_ARMED()) printf("[PATH] 3D: FB_W_SOF1=%08X FB_R_SOF1=%08X VtxCnt=%d lists=%d\n",
    FB_W_SOF1, FB_R_SOF1, VtxCnt, (int)(curLST - lists));

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
    {
      TransLST = curLST;
      TransVTX = curVTX;
      TransMod = curMod;
    }
    else if (ListType == ListType_Punch_Through)
    {
      PTLST = curLST;
      PT_VTX = curVTX;
      PT_Mod = curMod;
    }
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
  // Real PVR Intensity (Gouraud) mode: each vertex carries a scalar intensity;
  // the final RGB is the polygon's FaceColor (curFaceColor*, set by
  // AppendPolyParam1/2B/4B) scaled by that intensity. Alpha comes straight
  // from curFaceColorA, not from the intensity scalar. Gated behind
  // g_vertex_color_cached (refreshed once per frame in reset_vtx_state(),
  // see its decl above) — off keeps the original flat grayscale behavior
  // untouched for every game that hasn't opted in.
  static u32 INTESITY(float inte)
  {
    if (!g_vertex_color_cached)
    {
      u32 C = inte * 255;
      if (C > 255)
        C = 255;
      return (0xFF << 24) | (C << 16) | (C << 8) | (C);
    }

    s32 R = (s32)(curFaceColorR * inte * 255.0f);
    s32 G = (s32)(curFaceColorG * inte * 255.0f);
    s32 B = (s32)(curFaceColorB * inte * 255.0f);
    s32 A = (s32)(curFaceColorA * 255.0f);
    colclamp(0, 255, R);
    colclamp(0, 255, G);
    colclamp(0, 255, B);
    colclamp(0, 255, A);
    return ((u32)A << 24) | ((u32)B << 16) | ((u32)G << 8) | (u32)R;
  }

  // Offset-color flavor of INTESITY(): the vertex's offset intensity scalar
  // scales the polygon's FaceOffset color (curFaceOffs*, set by
  // AppendPolyParam2B). Alpha is left 0 — the offset TEV stage only adds RGB
  // and passes the previous stage's alpha through. Callers only invoke this
  // when curPolyOffsMask is set, so OFFSET_COLOR_FIX() is already known on.
  static u32 OFFS_INTESITY(float inte)
  {
    s32 R = (s32)(curFaceOffsR * inte * 255.0f);
    s32 G = (s32)(curFaceOffsG * inte * 255.0f);
    s32 B = (s32)(curFaceOffsB * inte * 255.0f);
    colclamp(0, 255, R);
    colclamp(0, 255, G);
    colclamp(0, 255, B);
    return ((u32)B << 16) | ((u32)G << 8) | (u32)R;
  }

  // Polys
#define glob_param_bdc                 \
  if ((curVTX - vertices) > 38 * 1024) \
    reset_vtx_state();                 \
  global_regd = true;                  \
  curMod->pcw = pp->pcw;               \
  curMod->isp = pp->isp;               \
  curMod->tsp = pp->tsp;               \
  curMod->tcw = pp->tcw;               \
  if (g_split_screen_cached || g_rtt_keep_cached || SCOPE_DEBUG_LOG || LOGO_DEBUG_LOG) \
    listTileClip[curMod - listModes] = ta_tileclip; \
  /* FOG(): with the offset colour dropped, keep its ALPHA anyway - that is the per-vertex fog coefficient. */ \
  curPolyOffsMask = pp->pcw.Offset ? (g_offset_color_fix_cached ? 0xFFFFFFFFu : (g_fog_cached ? 0xFF000000u : 0u)) : 0u;

  __forceinline static void fastcall AppendPolyParam0(TA_PolyParam0 *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam1(TA_PolyParam1 *pp)
  {
    glob_param_bdc;
    curFaceColorR = pp->FaceColorR;
    curFaceColorG = pp->FaceColorG;
    curFaceColorB = pp->FaceColorB;
    curFaceColorA = pp->FaceColorA;
    // Type 1 carries no face offset color — clear the scratch so a stale
    // value from an earlier Type 2 polygon can't leak into OFFS_INTESITY().
    curFaceOffsR = curFaceOffsG = curFaceOffsB = 0.0f;
  }
  __forceinline static void fastcall AppendPolyParam2A(TA_PolyParam2A *pp)
  {
    glob_param_bdc;
  }
  __forceinline static void fastcall AppendPolyParam2B(TA_PolyParam2B *pp)
  {
    curFaceColorR = pp->FaceColorR;
    curFaceColorG = pp->FaceColorG;
    curFaceColorB = pp->FaceColorB;
    curFaceColorA = pp->FaceColorA;
    // Face offset (specular) color, consumed by OFFS_INTESITY() when
    // OFFSET_COLOR_FIX() is on. Offset alpha is not used by the hardware.
    curFaceOffsR = pp->FaceOffsetR;
    curFaceOffsG = pp->FaceOffsetG;
    curFaceOffsB = pp->FaceOffsetB;
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
    // Only volume 0's face color is used: AppendPolyVertex10/13A/14A (the
    // intensity vertex handlers for two-volume polys) only ever read
    // BaseInt0, never blending in volume 1.
    curFaceColorR = pp->FaceColor0R;
    curFaceColorG = pp->FaceColor0G;
    curFaceColorB = pp->FaceColor0B;
    curFaceColorA = pp->FaceColor0A;
    // Type 4 carries no face offset color (see Type 1 note above).
    curFaceOffsR = curFaceOffsG = curFaceOffsB = 0.0f;
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
//
// NOTE: the comparison below is intentionally "_z >= 0.0001f" (not "_z < 0.0001f")
// so that NaN falls into the clamp. Per IEEE-754, every relational comparison
// against NaN is false, so "NaN < 0.0001f" is false and a "<"-based ternary lets
// NaN pass through unclamped, defeating the whole point of this guard.
// FIXED_DEPTH_*(): with fixed near/far planes the scene range is never used,
// so the min/max tracking (2 float compares + branches on EVERY TA vertex,
// including sprites) is skipped entirely. g_fixed_depth_cached is refreshed
// once per frame in reset_vtx_state() like the other cached preset flags.
#define vert_base(dst, _x, _y, _z) /*VertexCount++;*/         \
  float _safe_z = (_z >= g_vert_z_clamp) ? _z : g_vert_z_clamp; \
  float W = 1.0f / _safe_z;                                   \
  curVTX[dst].x = VTX_TFX(_x) * W;                           \
  curVTX[dst].y = VTX_TFY(_y) * W;                           \
  if (!g_fixed_depth_cached)                                  \
  {                                                           \
    if (W > 0.0f && W < vtx_min_Z)                           \
      vtx_min_Z = W;                                          \
    if (W > 0.0f && W > vtx_max_Z)                           \
      vtx_max_Z = W;                                          \
  }                                                           \
  curVTX[dst].z = W; /*Linearly scaled later*/

  // Poly Vertex handlers
#define vert_cvt_base vert_base(0, vtx->xyz[0], vtx->xyz[1], vtx->xyz[2])

  // Handlers for various PVR vertex types (Packed color, Float color, Intensity, etc.)
  //(Non-Textured, Packed Color)
  __forceinline static void AppendPolyVertex0(TA_Vertex0 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);
    curVTX->spc = 0; // non-textured: no offset color on PVR

    curVTX++;
  }

  //(Non-Textured, Floating Color)
  __forceinline static void AppendPolyVertex1(TA_Vertex1 *vtx)
  {
    vert_cvt_base;
    curVTX->col = FLCOL(&vtx->BaseA);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Non-Textured, Intensity)
  __forceinline static void AppendPolyVertex2(TA_Vertex2 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Textured, Packed Color)
  __forceinline static void AppendPolyVertex3(TA_Vertex3 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);
    curVTX->spc = ABGR8888(vtx->OffsCol) & curPolyOffsMask;

    curVTX->u = vtx->u;
    curVTX->v = vtx->v;

    curVTX++;
  }

  //(Textured, Packed Color, 16bit UV)
  __forceinline static void AppendPolyVertex4(TA_Vertex4 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol);
    curVTX->spc = ABGR8888(vtx->OffsCol) & curPolyOffsMask;

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
    curVTX->spc = curPolyOffsMask ? FLCOL(&vtx->OffsA) : 0;
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
    curVTX->spc = curPolyOffsMask ? FLCOL(&vtx->OffsA) : 0;
    curVTX++;
  }

  //(Textured, Intensity)
  __forceinline static void AppendPolyVertex7(TA_Vertex7 *vtx)
  {
    vert_cvt_base;
    curVTX->u = vtx->u;
    curVTX->v = vtx->v;

    curVTX->col = INTESITY(vtx->BaseInt);
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt) : 0;

    curVTX++;
  }

  //(Textured, Intensity, 16bit UV)
  __forceinline static void AppendPolyVertex8(TA_Vertex8 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt);
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt) : 0;

    curVTX->u = CVT16UV(vtx->u);
    curVTX->v = CVT16UV(vtx->v);

    curVTX++;
  }

  //(Non-Textured, Packed Color, with Two Volumes)
  __forceinline static void AppendPolyVertex9(TA_Vertex9 *vtx)
  {
    vert_cvt_base;
    curVTX->col = ABGR8888(vtx->BaseCol0);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Non-Textured, Intensity,	with Two Volumes)
  __forceinline static void AppendPolyVertex10(TA_Vertex10 *vtx)
  {
    vert_cvt_base;
    curVTX->col = INTESITY(vtx->BaseInt0);
    curVTX->spc = 0;

    curVTX++;
  }

  //(Textured, Packed Color,	with Two Volumes)
  __forceinline static void AppendPolyVertex11A(TA_Vertex11A *vtx)
  {
    vert_cvt_base;

    curVTX->u = vtx->u0;
    curVTX->v = vtx->v0;

    curVTX->col = ABGR8888(vtx->BaseCol0);
    curVTX->spc = ABGR8888(vtx->OffsCol0) & curPolyOffsMask;
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
    curVTX->spc = ABGR8888(vtx->OffsCol0) & curPolyOffsMask;
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
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt0) : 0;
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
    curVTX->spc = curPolyOffsMask ? OFFS_INTESITY(vtx->OffsInt0) : 0;
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
    // Sprites carry one packed offset color in the header, shared by all 4
    // vertices (masked to 0 when PCW.Offset is off or the fix is disabled).
    curSpriteSpc = ABGR8888(pp->OffsCol) & curPolyOffsMask;
    // Sprites have no per-vertex colour: this one header value is the colour
    // of all four corners. White when the preset is off = the old hardcode.
    curSpriteCol = g_sprite_color_cached ? ABGR8888(pp->BaseCol) : 0xFFFFFFFFu;
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
    curVTX[0].col = curSpriteCol;
    curVTX[1].col = curSpriteCol;
    curVTX[2].col = curSpriteCol;
    curVTX[3].col = curSpriteCol;
    curVTX[0].spc = curSpriteSpc;
    curVTX[1].spc = curSpriteSpc;
    curVTX[2].spc = curSpriteSpc;
    curVTX[3].spc = curSpriteSpc;

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
    // User Tile Clip control parameter: rect in 32-pixel tile units, max
    // inclusive (already masked to 6/5 bits by the ta.h caller).
    ta_tileclip_rect = (xmin << 24) | (ymin << 16) | (xmax << 8) | ymax;
    ta_tileclip      = (ta_tileclip & 0xC0000000u) | ta_tileclip_rect;
  }
  __forceinline static void TileClipMode(u32 mode)
  {
    // PCW.User_Clip of a global param — ta.h only calls this when Group_En=1,
    // so the mode persists across ungrouped params like real hardware.
    ta_tileclip = (mode << 30) | ta_tileclip_rect;
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
  // No printf here: the text contains literal '%' so printf(text) is UB
  // (it mangled/doubled the line in the log), and SPG.cpp already prints
  // the same string with a newline right after calling us.
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
  // Diagnostic file logger
  printf("Log to file check ndclog.txt\n");
#if (SCOPE_DEBUG_LOG || RTT_DEBUG_LOG) && SCOPE_LOG_TO_CONSOLE
  // Deliberately NOT redirecting: the whole point of the [SS] capture is to read
  // it live. Everything keeps going wherever libogc's stdout points (Dolphin log
  // window / USB Gecko). Restore the redirect by setting SCOPE_LOG_TO_CONSOLE 0.
  printf("[Init Renderer] Logging to console (ndclog.txt redirect skipped)\n");
#else
  freopen("/ndclog.txt", "w", stdout);
  printf("[Init Renderer] Logging started\n");
#endif
#if SCOPE_DEBUG_LOG
  // Unambiguous marker: if this line never appears, the build did not pick up
  // SCOPE_DEBUG_LOG and there is no point looking for [SS] lines at all.
  printf("[SS] scope pass logger ARMED (burst=%d, quiet=%d passes, heartbeat=%d)\n",
         SS_BURST, SS_QUIET_PASSES, SS_HEARTBEAT);
  fflush(stdout);
#endif
#if LOGO_DEBUG_LOG
  // If this line is missing from ndclog.txt the build did not pick up
  // LOGO_DEBUG_LOG and there is no point looking for [LOGO] lines.
  printf("[LOGO] boot-screen probe ARMED (every pass up to %d, then 1 in %d until pass %d, heartbeat=%d)\n",
         LOGO_HEAD_PASSES, LOGO_EVERY, LOGO_LAST_PASS, LOGO_HEARTBEAT);
  fflush(stdout);
#endif
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

  // Force single-sample, full 24-bit colour. rmode->aa selects a
  // 16-bit (RGB565) EFB further below, which dithers to hide banding —
  // that dither noise was previously masked by the deflicker blur and
  // becomes visible once the blur is removed. Pixel accuracy matters
  // more here than the TV anti-aliasing this mode is meant for.
  // rmode->aa = GX_FALSE;

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
  // Draw-done interrupt callback for ASYNC_RENDER()'s non-blocking vblank
  // present (gx_present_if_done). Registered unconditionally: with the preset
  // off it only sets an unused flag.
  GX_SetDrawDoneCallback(gx_drawdone_cb);
  ApplyGraphismPreset();  // LOW/NORMAL filter + GX / lod_bias / aniso presets

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
  GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter); // GX_FALSE or GX_TRUE ?
  GX_SetFieldMode(rmode->field_rendering, ((rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE));

  if (rmode->aa)
    GX_SetPixelFmt(GX_PF_RGB565_Z16, GX_ZC_LINEAR);
  else
    GX_SetPixelFmt(GX_PF_RGB8_Z24, GX_ZC_LINEAR);

  GX_SetCullMode(GX_CULL_NONE);
  GX_CopyDisp(frameBuffer[fb], GX_TRUE);
  GX_SetDispCopyGamma(GX_GM_1_0);

  // AUTOSORT() depth-peeling buffers (MEM2) — allocated unconditionally so
  // the preset can be toggled from the menu at any time; ~700 KB.
  as_init();

  // setup the vertex descriptor
  // tells the flipper to expect direct data

  printf("MEM1 free: %.2f MB\n", ((unat)SYS_GetArena1Hi() - (unat)SYS_GetArena1Lo()) / 1024.f / 1024);
  printf("MEM2 free: %.2f MB\n", ((unat)SYS_GetArena2Hi() - (unat)SYS_GetArena2Lo()) / 1024.f / 1024.f);

  printf("sizeof TextureCacheDesc: %d\n", sizeof(TextureCacheDesc));
  printf("sizeof GXTexObj: %d\n", sizeof(GXTexObj));
  printf("sizeof GXTlutObj: %d\n", sizeof(GXTlutObj));

  tex_cache_init(); // must be after vram_buffer is allocated by _vmem_reserve()

  return TileAccel.Init();
}

void TermRenderer()
{
  gx_sync_pending(); // don't tear down with a queued async frame in flight
  TileAccel.Term();
}

// ============================
// RESET RENDERER
// ============================

void ResetRenderer(bool Manual)
{
  gx_sync_pending(); // ASYNC_RENDER(): settle the in-flight frame before reset
  TileAccel.Reset(Manual);
  tex_cache_init(); // clear texture cache on reset
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