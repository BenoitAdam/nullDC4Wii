/*
    game_presets.cpp - Per-game preset system for NullDC4Wii

    Config file format (sd:/data/game_presets.cfg):

        ;; comment
        [keyword]           <- case-insensitive substring matched against filename
        accuracy=fast       <- only fields listed are overridden
        graphics=low
        8bpp=i8_stub
        jojo_fix=1          <- 0/1, enables the gxRend.cpp TLUT-checksum-skip
                                and CACHE_FAST PalSelect-masking optimizations
                                (see plugs/drkPvr/gxRend.cpp JOJO_FIX()).
                                Default 0 (off) leaves every other game's
                                texture caching/palette behavior unchanged.
        decal_alpha=1       <- 0/1, selects ShadInstr==2 (DecalAlpha) blending.
                                0=legacy GX_MODULATE (faster, wrong transparency)
                                1=correct DecalAlpha shading (GX_DECAL).
        speed_limiter=1     <- 0/1, caps emulation at real-hardware (100%) speed.
                                0=uncapped (may run >100% on light frames, default)
                                1=sleeps the difference each vblank so speed never
                                  exceeds 100% (never penalizes frames already at
                                  or below 100%; see plugs/drkPvr/SPG.cpp).
        vertex_color_fix=1 <- 0/1, real PVR Intensity (Gouraud) shading: each
                                vertex's scalar intensity is multiplied by the
                                polygon's FaceColor (see gxRend.cpp
                                VERTEX_COLOR_FIX()). Default 0 (off) keeps the
                                old flat-grayscale behavior for every other game;
                                Crazy Taxi needs this on for its HUD arrow/dollar
                                sign to show their real color instead of gray.
        blend_mode=1        <- 0/1, per-polygon TSP SrcInstr/DstInstr blend mode
                                for the translucent list (see gxRend.cpp
                                BLEND_MODE()). 1 (default, correct) applies the
                                polygon's actual blend factors each frame;
                                0 (legacy) skips the per-polygon override and uses
                                the GX default, which is faster but renders
                                Resident Evil 3's translucent polygons incorrectly.
        blend_fps_boost=1   <- 0/1, only used when blend_mode=1 (see gxRend.cpp
                                BLEND_FPS_BOOST()). 1 forces alpha_fmt=0 (skips the
                                alpha-test/ZCompLoc pass) for every polygon outside
                                the translucent list, saving a couple of FPS (e.g.
                                Castlevania) at the cost of incorrect alpha on some
                                opaque/punch-through polys. Default 0 (off, correct).
        punch_through=1     <- 0/1, punch-through list fix (see gxRend.cpp
                                PUNCH_THROUGH_FIX()). 1 (default, correct) draws
                                lists in OP -> PT -> TR order with the PT list
                                alpha-tested against PT_ALPHA_REF and blending off,
                                like real PVR; 0 (legacy) draws PT polys last, in
                                the translucent blend state.
        offset_color=1      <- 0/1, offset (specular) color (see gxRend.cpp
                                OFFSET_COLOR_FIX()). 1 renders textured polys as
                                PIX = base*tex + offset like real PVR (specular
                                highlights on cars/water), costing 4 bytes/vertex
                                of FIFO and a second TEV stage on offset polys;
                                0 (default, legacy) drops the offset color.
        trans_sort=1        <- 0/1, translucent depth sort (see gxRend.cpp
                                TRANS_SORT()). Real PVR autosorts translucent
                                pixels in hardware; 1 sorts the TR strips
                                back-to-front (painter's algorithm) before
                                drawing, fixing wrong overlaps in alpha-heavy
                                scenes, at some CPU cost per frame;
                                0 (default, legacy) draws in submission order.
        render_to_texture=1 <- 0/1, render-to-texture support (see gxRend.cpp
                                RENDER_TO_TEXTURE()). Frames whose write address
                                (FB_W_SOF1) has bit 24 set target the 64-bit
                                texture area — mirrors, TV screens, some menu
                                effects. 1 renders them and copies the EFB back
                                into emulated VRAM at FB_W_SOF1 so the game can
                                bind the result as a texture, at the cost of an
                                EFB copy + CPU convert per RTT frame;
                                0 (default, legacy) drops those frames.
        mipmap=fast         <- off/fast/trilinear (or 0/1/2), GX mipmap
                                generation (see gxRend.cpp MIPMAP_*()).
                                off (default) = legacy base-level-only, fastest;
                                fast = generated mip chain sampled with
                                nearest-mip bilinear — kills distant-texture
                                shimmer at near-zero GPU cost; trilinear = best
                                quality but takes 2 texture cycles/texel on
                                Hollywood, halving texture fill rate (-40% FPS
                                in Test Drive 6).
        fixed_depth=1       <- 0/1/2, fixed depth projection (see gxRend.cpp
                                FIXED_DEPTH_*()). 0 (default, legacy) tracks the
                                scene's min/max depth on every TA vertex to fit
                                the Z range each frame; 1 (wide) and 2 (tight)
                                skip that per-vertex tracking and project with
                                fixed near/far planes — slightly faster vertex
                                decode, but a coarser Z buffer. wide covers
                                W=[0.0001..100000] (safe everywhere, may
                                Z-fight); tight covers W=[0.1..25000] (much
                                finer Z, but geometry outside that range clips,
                                so per-game only).
        async_render=1      <- 0/1, async GPU present (see gxRend.cpp
                                ASYNC_RENDER()). 0 (default, legacy) blocks the
                                CPU in GX_DrawDone() until the GPU finishes the
                                frame; 1 queues the frame and returns at once —
                                the SH4 core emulates the next frame while the
                                GPU draws, and the finished frame is presented
                                at the start of the next one. One frame of
                                extra display latency; big FPS gain whenever
                                the GPU takes a meaningful slice of the frame.
        tmem_cache=1        <- 0/1, persistent GPU texture cache (see gxRend.cpp
                                TMEM_CACHE()). 0 (default, legacy) invalidates
                                the GPU's 1MB texture cache (TMEM) every frame,
                                re-fetching every texel from RAM; 1 invalidates
                                only when a texture is actually re-decoded, so
                                unchanged textures stay cached across frames —
                                more texture fill rate.
        split_screen=1      <- 0/1, split-screen multiplayer (see gxRend.cpp
                                SPLIT_SCREEN()). 2P games (Daytona USA
                                multiplayer) render one pass per player
                                viewport, clipped with FB_X_CLIP/FB_Y_CLIP.
                                1 draws each partial-clip pass scissored into
                                its half of the EFB and presents the assembled
                                frame once per vblank; 0 (default, legacy)
                                presents every pass fullscreen, so only one
                                player's view shows.

    First matching rule wins.
    Unset fields are left at whatever the user selected in the UI.

    IMPORTANT: matching is done by lowercasing BOTH the filename and the keyword,
    then using plain strstr() — no strncasecmp needed (avoids devkitPPC/newlib issues).
*/

#include "game_presets.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ---------------------------------------------------------------------------
// Global presets declared in main.cpp
// ---------------------------------------------------------------------------
extern int g_accuracy_preset;
extern int g_graphism_preset;
extern int g_ratio_preset;
extern int g_advanced_alpha_preset;
extern int g_frameskip_preset;
extern int g_texture_cache_preset;
extern int g_ppz_write_preset;
extern int g_4bpp_preset;
extern int g_8bpp_preset;
extern int g_jojo_fix_preset;
extern int g_decal_alpha_preset;
extern int g_speed_limiter_preset;
extern int g_vertex_color_fix_preset;
extern int g_blend_mode_preset;
extern int g_rgb565_opaque_alpha_preset;
extern int g_blend_fps_boost_preset;
extern int g_punch_through_preset;
extern int g_offset_color_preset;
extern int g_trans_sort_preset;
extern int g_render_to_texture_preset;
extern int g_split_screen_preset;
extern int g_mipmap_preset;
extern int g_fixed_depth_preset;
extern int g_async_render_preset;
extern int g_tmem_cache_preset;
extern int g_player_count;
extern int g_controller_type;
extern int g_framebuffer_2d;
extern int g_fmv_format_preset;

// ---------------------------------------------------------------------------
// Internal structures
// ---------------------------------------------------------------------------

#define MAX_PRESETS     64
#define MAX_KEYWORD_LEN 64

struct GamePreset
{
    char keyword[MAX_KEYWORD_LEN];   // already lowercased at load time

    // -1 = not set (leave user default untouched)
    int accuracy;
    int graphics;
    int ratio;
    int adv_alpha;
    int frameskip;
    int tex_cache;
    int ppz_write;
    int bpp4;
    int bpp8;
    int jojo_fix;
    int decal_alpha;
    int speed_limiter;
    int vertex_color_fix;
    int players;
    int controller;
    int framebuffer_2d;
    int fmv_format;
    int blend_mode;
    int rgb565_opaque_alpha;
    int blend_fps_boost;
    int punch_through;
    int offset_color;
    int trans_sort;
    int render_to_texture;
    int split_screen;
    int mipmap;
    int fixed_depth;
    int async_render;
    int tmem_cache;
};

static GamePreset s_presets[MAX_PRESETS];
static int        s_preset_count = 0;

// Exported so main.cpp can display the matched preset name in the options menu
char g_matched_preset_name[MAX_KEYWORD_LEN] = "";

// ---------------------------------------------------------------------------
// String helpers  (no strncasecmp — not reliable on all devkitPPC newlib builds)
// ---------------------------------------------------------------------------

static void str_tolower_inplace(char* s)
{
    for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

// Trim leading/trailing whitespace in-place, returns pointer into s
static char* str_trim(char* s)
{
    while (*s && isspace((unsigned char)*s)) s++;
    char* end = s + strlen(s);
    while (end > s && isspace((unsigned char)*(end - 1))) end--;
    *end = '\0';
    return s;
}

// Case-insensitive key compare: lowercase both sides then strcmp
// Used for key/value parsing where we control both strings.
static int key_eq(const char* a, const char* b)
{
    char la[64], lb[64];
    strncpy(la, a, sizeof(la) - 1); la[sizeof(la)-1] = '\0';
    strncpy(lb, b, sizeof(lb) - 1); lb[sizeof(lb)-1] = '\0';
    str_tolower_inplace(la);
    str_tolower_inplace(lb);
    return strcmp(la, lb) == 0;
}

// Substring match: both haystack and needle are ALREADY lowercased at call site.
// Plain strstr() is sufficient and avoids any strncasecmp availability issues.
static bool str_contains(const char* haystack, const char* needle)
{
    if (!haystack || !needle || !*needle) return false;
    return strstr(haystack, needle) != NULL;
}

// Strip an inline comment (; or #) from a value string in-place
static void strip_inline_comment(char* s)
{
    // Walk char by char; stop at # or ;
    for (char* p = s; *p; p++)
    {
        if (*p == '#' || *p == ';')
        {
            *p = '\0';
            break;
        }
    }
}

// ---------------------------------------------------------------------------
// Value parsers — return -1 on unknown token
// ---------------------------------------------------------------------------

static int parse_accuracy(const char* v)
{
    if (key_eq(v, "fast"))     return 0;
    if (key_eq(v, "balanced")) return 1;
    if (key_eq(v, "accurate")) return 2;
    printf("[game_presets] Unknown accuracy value: '%s'\n", v);
    return -1;
}

static int parse_graphics(const char* v)
{
    if (key_eq(v, "low"))    return 0;
    if (key_eq(v, "normal")) return 1;
    if (key_eq(v, "high"))   return 2;
    if (key_eq(v, "extra"))  return 3;
    printf("[game_presets] Unknown graphics value: '%s'\n", v);
    return -1;
}

static int parse_ratio(const char* v)
{
    if (key_eq(v, "original"))   return 0;
    if (key_eq(v, "fullscreen")) return 1;
    printf("[game_presets] Unknown ratio value: '%s'\n", v);
    return -1;
}

static int parse_frameskip(const char* v)
{
    if (strcmp(v, "0")      == 0) return 0;
    if (strcmp(v, "1")      == 0) return 1;
    if (strcmp(v, "2")      == 0) return 2;
    if (key_eq(v, "auto"))        return 3;
    printf("[game_presets] Unknown frameskip value: '%s'\n", v);
    return -1;
}

static int parse_tex_cache(const char* v)
{
    if (key_eq(v, "very_fast")) return 0;
    if (key_eq(v, "fast"))      return 1;
    if (key_eq(v, "normal"))    return 2;
    if (key_eq(v, "quality"))   return 3;
    printf("[game_presets] Unknown tex_cache value: '%s'\n", v);
    return -1;
}

static int parse_bpp(const char* v)
{
    if (key_eq(v, "i4_stub"))    return 0;
    if (key_eq(v, "i8_stub"))    return 0;
    if (key_eq(v, "4bpp_optimized")) return 1;
    if (key_eq(v, "8bpp_optimized")) return 1;
    if (key_eq(v, "ci4_fast"))   return 2;
    if (key_eq(v, "ci8_fast"))   return 2;
    if (key_eq(v, "ci4_normal")) return 3;
    if (key_eq(v, "ci8_normal")) return 3;
    if (key_eq(v, "rgb565"))     return 4;
    printf("[game_presets] Unknown bpp value: '%s'\n", v);
    return -1;
}

static int parse_fmv_format(const char* v)
{
    if (key_eq(v, "cmpr"))   return 0;
    if (key_eq(v, "rgba8"))  return 1;
    if (key_eq(v, "rgb565")) return 2;
    printf("[game_presets] Unknown fmv_format value: '%s'\n", v);
    return -1;
}

static int parse_mipmap(const char* v)
{
    if (key_eq(v, "off")       || strcmp(v, "0") == 0) return 0;
    if (key_eq(v, "fast")      || strcmp(v, "1") == 0) return 1;
    if (key_eq(v, "trilinear") || strcmp(v, "2") == 0) return 2;
    printf("[game_presets] Unknown mipmap value: '%s'\n", v);
    return -1;
}

static int parse_controller(const char* v)
{
    if (key_eq(v, "standard"))   return 0;
    if (key_eq(v, "lightgun"))   return 1;
    if (key_eq(v, "maracas"))    return 2;
    if (key_eq(v, "keyboard"))   return 3;
    if (key_eq(v, "fishingrod")) return 4;
    printf("[game_presets] Unknown controller value: '%s'\n", v);
    return -1;
}

// ---------------------------------------------------------------------------
// Apply one key=value pair to a preset slot
// ---------------------------------------------------------------------------

static void apply_kv(GamePreset* p, const char* key, const char* val)
{
    if      (key_eq(key, "accuracy"))   p->accuracy   = parse_accuracy(val);
    else if (key_eq(key, "graphics"))   p->graphics   = parse_graphics(val);
    else if (key_eq(key, "ratio"))      p->ratio      = parse_ratio(val);
    else if (key_eq(key, "adv_alpha"))  p->adv_alpha  = atoi(val);
    else if (key_eq(key, "frameskip"))  p->frameskip  = parse_frameskip(val);
    else if (key_eq(key, "tex_cache"))  p->tex_cache  = parse_tex_cache(val);
    else if (key_eq(key, "ppz_write"))  p->ppz_write  = atoi(val);
    else if (key_eq(key, "4bpp"))       p->bpp4       = parse_bpp(val);
    else if (key_eq(key, "8bpp"))       p->bpp8       = parse_bpp(val);
    else if (key_eq(key, "jojo_fix"))   p->jojo_fix   = atoi(val);
    else if (key_eq(key, "decal_alpha")) p->decal_alpha = atoi(val);
    else if (key_eq(key, "speed_limiter")) p->speed_limiter = atoi(val);
    else if (key_eq(key, "vertex_color_fix")) p->vertex_color_fix = atoi(val);
    else if (key_eq(key, "players"))    p->players    = atoi(val);
    else if (key_eq(key, "controller")) p->controller = parse_controller(val);
    else if (key_eq(key, "framebuffer_2d")) p->framebuffer_2d = atoi(val);
    else if (key_eq(key, "fmv_format"))     p->fmv_format     = parse_fmv_format(val);
    else if (key_eq(key, "blend_mode"))     p->blend_mode     = atoi(val);
    else if (key_eq(key, "rgb565_opaque_alpha")) p->rgb565_opaque_alpha = atoi(val);
    else if (key_eq(key, "blend_fps_boost")) p->blend_fps_boost = atoi(val);
    else if (key_eq(key, "punch_through"))  p->punch_through  = atoi(val);
    else if (key_eq(key, "offset_color"))   p->offset_color   = atoi(val);
    else if (key_eq(key, "trans_sort"))     p->trans_sort     = atoi(val);
    else if (key_eq(key, "render_to_texture")) p->render_to_texture = atoi(val);
    else if (key_eq(key, "split_screen"))   p->split_screen   = atoi(val);
    else if (key_eq(key, "mipmap"))         p->mipmap         = parse_mipmap(val);
    else if (key_eq(key, "fixed_depth"))    p->fixed_depth    = atoi(val);
    else if (key_eq(key, "async_render"))   p->async_render   = atoi(val);
    else if (key_eq(key, "tmem_cache"))     p->tmem_cache     = atoi(val);
    else printf("[game_presets] Unknown key: '%s'\n", key);
}

// ---------------------------------------------------------------------------
// Public: load
// ---------------------------------------------------------------------------

void game_presets_load(const char* cfg_path)
{
    s_preset_count = 0;
    g_matched_preset_name[0] = '\0';

    FILE* f = fopen(cfg_path, "r");
    if (!f)
    {
        printf("[game_presets] No preset file at %s — using UI defaults\n", cfg_path);
        return;
    }

    printf("[game_presets] Loading presets from %s\n", cfg_path);

    GamePreset* cur = NULL;
    char line[256];

    while (fgets(line, sizeof(line), f))
    {
        char* s = str_trim(line);

        // Blank or full-line comment (;; or # or ;)
        if (!*s || *s == '#' || *s == ';') continue;

        // Section header [keyword]
        if (*s == '[')
        {
            // Find the closing bracket
            char* end_bracket = strchr(s, ']');
            if (!end_bracket) continue;
            *end_bracket = '\0';                 // terminate at ']'
            char* kw = str_trim(s + 1);          // content between [ and ]
            if (!*kw) continue;

            if (s_preset_count >= MAX_PRESETS)
            {
                printf("[game_presets] Max presets (%d) reached, skipping [%s]\n",
                       MAX_PRESETS, kw);
                cur = NULL;
                continue;
            }

            cur = &s_presets[s_preset_count++];
            memset(cur, 0, sizeof(*cur));
            // Mark every field as "not set"
            cur->accuracy = cur->graphics  = cur->ratio    = cur->adv_alpha = -1;
            cur->frameskip= cur->tex_cache = cur->bpp4     = cur->bpp8      = -1;
            cur->jojo_fix = -1;
            cur->decal_alpha = -1;
            cur->speed_limiter = -1;
            cur->vertex_color_fix = -1;
            cur->players  = cur->controller                                  = -1;
            cur->ppz_write = -1;
            cur->framebuffer_2d = -1;
            cur->fmv_format = -1;
            cur->blend_mode = -1;
            cur->rgb565_opaque_alpha = -1;
            cur->blend_fps_boost = -1;
            cur->punch_through = -1;
            cur->offset_color = -1;
            cur->trans_sort = -1;
            cur->render_to_texture = -1;
            cur->split_screen = -1;
            cur->mipmap = -1;
            cur->fixed_depth = -1;
            cur->async_render = -1;
            cur->tmem_cache = -1;

            strncpy(cur->keyword, kw, MAX_KEYWORD_LEN - 1);
            cur->keyword[MAX_KEYWORD_LEN - 1] = '\0';
            str_tolower_inplace(cur->keyword);   // lowercase once at load time

            // printf("[game_presets]   Registered: [%s]\n", cur->keyword);
            continue;
        }

        // key=value pair (only if inside a section)
        if (!cur) continue;

        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = str_trim(s);
        char* val = str_trim(eq + 1);

        // Strip trailing inline comment from value
        strip_inline_comment(val);
        val = str_trim(val);   // re-trim after comment removal

        if (*key && *val)
            apply_kv(cur, key, val);
    }

    fclose(f);
    printf("[game_presets] Done — %d preset(s) loaded\n", s_preset_count);
}

// ---------------------------------------------------------------------------
// Public: apply
// ---------------------------------------------------------------------------

void game_presets_apply(const char* filepath)
{
    g_matched_preset_name[0] = '\0';   // clear previous match

    if (!filepath || !*filepath || s_preset_count == 0)
        return;

    // Work on filename only (strip directory part)
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    // Lowercase copy for matching — keywords are already lowercased at load time
    char lower[512];
    strncpy(lower, filename, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    str_tolower_inplace(lower);

    printf("[game_presets] Trying to match: '%s'\n", lower);

    // First match wins
    for (int i = 0; i < s_preset_count; i++)
    {
        GamePreset* p = &s_presets[i];
        if (!str_contains(lower, p->keyword))
            continue;

        printf("[game_presets] Matched [%s] for file: %s\n", p->keyword, filename);

        // Save matched name for display in options menu
        strncpy(g_matched_preset_name, p->keyword, MAX_KEYWORD_LEN - 1);
        g_matched_preset_name[MAX_KEYWORD_LEN - 1] = '\0';

        if (p->accuracy   >= 0) { g_accuracy_preset      = p->accuracy;   printf("  accuracy   -> %d\n", p->accuracy);   }
        if (p->graphics   >= 0) { g_graphism_preset       = p->graphics;   printf("  graphics   -> %d\n", p->graphics);   }
        if (p->ratio      >= 0) { g_ratio_preset          = p->ratio;      printf("  ratio      -> %d\n", p->ratio);      }
        if (p->adv_alpha  >= 0) { g_advanced_alpha_preset = p->adv_alpha;  printf("  adv_alpha  -> %d\n", p->adv_alpha);  }
        if (p->frameskip  >= 0) { g_frameskip_preset      = p->frameskip;  printf("  frameskip  -> %d\n", p->frameskip);  }
        if (p->tex_cache  >= 0) { g_texture_cache_preset  = p->tex_cache;  printf("  tex_cache  -> %d\n", p->tex_cache);  }
        if (p->ppz_write  >= 0) { g_ppz_write_preset      = p->ppz_write;  printf("  ppz_write  -> %d\n", p->ppz_write);  }
        if (p->bpp4       >= 0) { g_4bpp_preset           = p->bpp4;       printf("  4bpp       -> %d\n", p->bpp4);       }
        if (p->bpp8       >= 0) { g_8bpp_preset           = p->bpp8;       printf("  8bpp       -> %d\n", p->bpp8);       }
        if (p->jojo_fix   >= 0) { g_jojo_fix_preset       = p->jojo_fix;   printf("  jojo_fix   -> %d\n", p->jojo_fix);   }
        if (p->decal_alpha >= 0) { g_decal_alpha_preset   = p->decal_alpha; printf("  decal_alpha -> %d\n", p->decal_alpha); }
        if (p->speed_limiter >= 0) { g_speed_limiter_preset = p->speed_limiter; printf("  speed_limiter -> %d\n", p->speed_limiter); }
        if (p->vertex_color_fix >= 0) { g_vertex_color_fix_preset = p->vertex_color_fix; printf("  vertex_color_fix -> %d\n", p->vertex_color_fix); }
        if (p->players    >= 0) { g_player_count          = p->players;    printf("  players    -> %d\n", p->players);    }
        if (p->controller >= 0) { g_controller_type       = p->controller; printf("  controller -> %d\n", p->controller); }
        if (p->framebuffer_2d >= 0) { g_framebuffer_2d    = p->framebuffer_2d; printf("  framebuffer_2d -> %d\n", p->framebuffer_2d); }
        if (p->fmv_format     >= 0) { g_fmv_format_preset = p->fmv_format;     printf("  fmv_format     -> %d\n", p->fmv_format);     }
        if (p->blend_mode     >= 0) { g_blend_mode_preset = p->blend_mode;     printf("  blend_mode     -> %d\n", p->blend_mode);     }
        if (p->rgb565_opaque_alpha >= 0) { g_rgb565_opaque_alpha_preset = p->rgb565_opaque_alpha; printf("  rgb565_opaque_alpha -> %d\n", p->rgb565_opaque_alpha); }
        if (p->blend_fps_boost >= 0) { g_blend_fps_boost_preset = p->blend_fps_boost; printf("  blend_fps_boost -> %d\n", p->blend_fps_boost); }
        if (p->punch_through  >= 0) { g_punch_through_preset = p->punch_through;   printf("  punch_through  -> %d\n", p->punch_through);  }
        if (p->offset_color   >= 0) { g_offset_color_preset  = p->offset_color;    printf("  offset_color   -> %d\n", p->offset_color);   }
        if (p->trans_sort     >= 0) { g_trans_sort_preset    = p->trans_sort;      printf("  trans_sort     -> %d\n", p->trans_sort);     }
        if (p->render_to_texture >= 0) { g_render_to_texture_preset = p->render_to_texture; printf("  render_to_texture -> %d\n", p->render_to_texture); }
        if (p->split_screen   >= 0) { g_split_screen_preset  = p->split_screen;    printf("  split_screen   -> %d\n", p->split_screen);   }
        if (p->mipmap         >= 0) { g_mipmap_preset        = p->mipmap;          printf("  mipmap         -> %d\n", p->mipmap);         }
        if (p->fixed_depth    >= 0) { g_fixed_depth_preset   = p->fixed_depth;     printf("  fixed_depth    -> %d\n", p->fixed_depth);    }
        if (p->async_render   >= 0) { g_async_render_preset  = p->async_render;    printf("  async_render   -> %d\n", p->async_render);   }
        if (p->tmem_cache     >= 0) { g_tmem_cache_preset    = p->tmem_cache;      printf("  tmem_cache     -> %d\n", p->tmem_cache);     }

        return; // First match only
    }

    printf("[game_presets] No preset matched for: %s\n", filename);
}
