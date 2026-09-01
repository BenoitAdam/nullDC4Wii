/*
    game_presets.cpp - Per-game preset system for NullDC4Wii

    Config file format (sd:/discs/game_presets.cfg):

        ;; comment
        [keyword]           <- case-insensitive substring matched against filename
        [kw1][kw2][kw3]     <- a section may list several aliases; the section
                                applies if ANY of them matches the filename
                                (e.g. [streetfighter32][doubleimpact][double impact]).
                                The FIRST alias is the canonical name shown in
                                the options menu. Any number of aliases per section.
        <kw1><kw2>          <- angle brackets are the SAME alias match as
                                square brackets, but ONLY apply on real Wii U
                                hardware running vWii (see main.cpp g_is_wiiu,
                                set from WiiDRC_Inited() — independent of
                                whether a GamePad is actually paired). [square]
                                and <angle> alias groups may be mixed freely
                                on one line, in any order, any number of
                                times; if ANY group on the line uses <angle>
                                brackets, the whole section is Wii-U-gated.
                                Put the <angle> section BEFORE the plain
                                [square] section for the same game (first
                                match wins, more specific first — same rule
                                as before):
                                    <segatetris><sega tetris>   ; Wii U
                                    accuracy=accurate
                                    [segatetris][sega tetris]   ; Wii (fallback)
                                    accuracy=fast
                                A real Wii simply fails the Wii U gate and
                                falls through to the plain section below it.
        accuracy=fast       <- only fields listed are overridden
        graphics=low        <- low | normal. TEXTURE FILTER ONLY:
                                low=GX_NEAR (point sampling, best for 240p
                                games whose art already matches the output
                                resolution), normal=GX_LINEAR (bilinear).
                                The former high/extra levels were just a fixed
                                (lod_bias, bias clamp, aniso) bundle glued onto
                                normal; those are the three separate keys below
                                now. Old configs saying high/extra still load —
                                they map to normal, and a log line points at
                                gx=/lod_bias=/aniso= for the rest.
        gx=on               <- on/off (GX_ENABLE/GX_DISABLE), the biasclamp +
                                edgelod pair of GX_InitTexObjLOD() — the only
                                two GX_DISABLE/GX_ENABLE arguments that call
                                takes (see gxRend.cpp ApplyGraphismPreset).
                                biasclamp stops lod_bias from pushing a
                                minified texel past the point where its
                                footprint no longer covers the pixel (blur /
                                shimmer on steep surfaces); edgelod computes
                                the LOD from adjacent instead of diagonal
                                texels. off (default) = legacy. NOTE aniso
                                below forces edgelod on by itself — libogc
                                requires it whenever maxaniso > 1.
        lod_bias=0.0        <- -1 | -0.75 | -0.5 | 0.0 | 0.5, texture LOD bias
                                added to the computed LOD before the mip /
                                filter decision. Negative sharpens (samples a
                                larger level than the footprint asks for),
                                positive blurs. 0.0 (default) is the hardware
                                default and a true no-op.
        aniso=0x            <- 0x | 2x | 4x, anisotropic filtering: the TX unit
                                iterates its square filter along the axis of
                                anisotropy, one extra filter cycle per level
                                (sharper textures on surfaces seen at a steep
                                angle, at texture fill-rate cost). Hollywood
                                only implements 1x/2x/4x — there is no 8x mode;
                                an old aniso=8x line loads clamped to 4x.
                                REQUIRES mipmap=fast or mipmap=trilinear: the
                                TX unit only iterates anisotropy when the min
                                filter is GX_LIN_MIP_LIN, which needs a
                                generated mip chain, so aniso does NOTHING on a
                                game left at mipmap=off (the default). Asking
                                for 2x/4x does promote an already-mipmapped
                                texture to trilinear on its own.
                                0x (default) = off.
        8bpp=i8_stub
        vq_cmpr=on          <- on/off, decodes VQ textures to GX CMPR (DXT1,
                                4 bits/texel) instead of 16bpp. Only useful with
                                tex_cache=very_fast_plus: at that size a VQ
                                texture fits its address-derived cache slot, so
                                it no longer overruns the neighbouring texture
                                (the classic very_fast VQ corruption) and no
                                longer needs the overflow arena. Costs a second
                                lossy pass on top of VQ, so smooth gradients
                                band. 565 VQ source only; 1555/4444 keep the
                                16bpp path since CMPR has no usable alpha.
                                Default off.
        jojo_fix=on         <- on/off, enables the gxRend.cpp TLUT-checksum-skip
                                and CACHE_FAST PalSelect-masking optimizations
                                (see plugs/drkPvr/gxRend.cpp JOJO_FIX()).
                                Default off leaves every other game's
                                texture caching/palette behavior unchanged.
        decal_alpha=on      <- on/off, selects ShadInstr==2 (DecalAlpha) blending.
                                off=legacy GX_MODULATE (faster, wrong transparency)
                                on=correct DecalAlpha shading (GX_DECAL).
        speed_limiter=on    <- on/off, caps emulation at real-hardware (100%) speed.
                                off=uncapped (may run >100% on light frames, default)
                                on=sleeps the difference each vblank so speed never
                                  exceeds 100% (never penalizes frames already at
                                  or below 100%; see plugs/drkPvr/SPG.cpp).
        render_delay=on     <- on/off, hardware-like HOLLY IRQ delays (see
                                plugs/drkPvr/SPG.cpp RENDER_DELAY()). Games
                                that pace their main loop off the TA
                                list-complete / render-done interrupts break
                                when those fire (nearly) instantly: emulation
                                speed reads >100% while the game renders only
                                a few frames per second (Marvel vs Capcom 2).
                                on delays each list-complete IRQ by 200 SH4
                                cycles and staggers render-done ISP/TSP/Video
                                at 800k/850k/900k cycles after STARTRENDER;
                                off (default, legacy) keeps instant list IRQs
                                and the single render-done burst.
        arm7_speed=1        <- 0/1/2, ARM7 sound-CPU speed divider stage:
                                effective clock = ~10 MHz >> stage (see
                                plugs/vbaARM/arm_aica.cpp). The AICA driver
                                free-runs its poll/scan main loop far more
                                than any driver needs, so 1 (half) or 2
                                (quarter) reclaims host CPU. Default 0
                                (off/legacy); per-game, verify music/SFX
                                timing by ear before keeping — stage 2 has
                                been found to break audio timing.
        sh4_clock=175       <- 150..200, SH4 underclock: effective SH4 core
                                clock in MHz (clamped to [150,200]; see
                                plugins/plugin_types.h SH4_CLOCK_EFF). 200
                                (default) is real-Dreamcast full speed. Lower
                                budgets fewer emulated SH4 cycles per audio/
                                video/RTC frame, so the Wii host renders more
                                output frames per second (smoother), at the
                                cost of the emulated machine acting like a
                                slower Dreamcast — CPU-heavy games drop
                                internal frames. Audio stays in sync (the AICA
                                anchor scales with the same clock). A pure
                                performance knob — A/B per game.
        jit_sbp=1           <- 0/1/2, JIT_SBP (Stale Block Protection). Gates
                                all three defenses against executing stale or
                                self-modified translations (see
                                dc/sh4/rec_v2/driver.cpp): the boot-entry cache
                                flush at 0x..08300 / 0x..10000 (IP.BIN -> game
                                binary transition), the runtime block-check
                                guard (a source-byte compare emitted at each
                                guarded block's entry), and the full cache
                                clear on a guard failure. 1 guards the
                                addresses known to self-modify (DOA2LE, Shenmue
                                1/2); 2 guards every RAM block, which is slow
                                but catches unknown cases. Default 1 (known).
                                0 reproduces the legacy zero-protection
                                behavior, for A/B comparison only.
        dma_fix=on          <- on/off, bundles the ch2/PVR/Sort/AICA-G2 DMA
                                correctness fixes found by diffing against the
                                verified-working NullDC PSP port (see
                                dc/sh4/dmac.cpp, dc/pvr/pvr_sb_regs.cpp,
                                dc/aica/aica_if.cpp): correct CHCR TE/DE
                                writeback (real SH4 sets TE and never clears
                                DE), no SB_C2DSTAT clobber, no bogus PVR-DMA
                                alignment check, the Sort-DMA link-sentinel fix
                                (WinCE games), and deferred (non-instant) AICA
                                G2-DMA completion + SB_E2ST. Default on. off
                                reproduces the legacy pre-fix behavior, for A/B
                                comparison only.
        fastmem=on          <- on/off, PPC-MMU fastmem for the SH4 dynarec
                                (wii/wii_fastmem.cpp). Maps the DC address
                                space at EA 0-0x1FFFFFFF via segment regs +
                                a hashed page table so JIT loads/stores are
                                branchless (rlwinm+load, no compares, no
                                table lookup); MMIO/SQ/BIOS accesses DSI-
                                fault once and get back-patched to slow-path
                                trampolines. Default off (legacy inline
                                table). Experimental — A/B per game.
        bcache=on           <- on/off, flat dynamic-branch dispatch cache for
                                the SH4 dynarec (blockmanager.cpp bm_bcache).
                                Dynamic jumps (jmp/rts/bsrf) hit one 8-byte
                                {addr, code} entry = one data cache line,
                                instead of chasing cache[] -> DynarecBlock
                                across two lines + a counter write. Default
                                off (legacy). Perf preset — A/B per game.
        fpu_pin=on          <- on/off, pins SH4 fr0-15 to real PPC FPU
                                registers f14-f29 for the whole session (see
                                wii_driver.cpp FPU_PIN), the same scheme int
                                GPRs already use. Speeds up geometry-heavy
                                games (fadd/fmul/fmac/fipr/ftrv/cvt_* stop
                                round-tripping through memory). xf[] (the
                                FTRV matrix bank) is never pinned. New and
                                unproven — default off. Perf preset — A/B
                                per game, especially anything 3D-transform
                                heavy.
        jit_align=on        <- on/off, pads each SH4-dynarec block entry to a
                                32-byte Broadway L1 cache line (see
                                wii_driver.cpp ngen_Compile). Every branch/link
                                target then begins on a clean line boundary
                                instead of possibly splitting its first fetch.
                                Cache-hygiene only, no logic change; marginal.
                                Default off. Perf preset — A/B per game.
        sched=on            <- on/off, unified cycle-deadline event scheduler
                                (dc/sh4/sh4_sched.cpp). Fires the completion/IRQ
                                events whose RELATIVE ordering matters (GD-ROM
                                read-done, ch2/PVR/AICA-DMA completion, render-
                                done, TA list-end) through one deadline queue in
                                true hardware order, instead of at their own tier
                                of the Medium/Slow/VerySlow timeslice cascade.
                                Leading suspect for the cross-game post-logo
                                stall (Rez). Default off. EXPERIMENTAL — A/B.
        vertex_color=on <- on/off, real PVR Intensity (Gouraud) shading: each
                                vertex's scalar intensity is multiplied by the
                                polygon's FaceColor (see gxRend.cpp
                                VERTEX_COLOR()). Default off keeps the
                                old flat-grayscale behavior for every other game;
                                Crazy Taxi needs this on for its HUD arrow/dollar
                                sign to show their real color instead of gray.
        blend_mode=on       <- on/off, per-polygon TSP SrcInstr/DstInstr blend mode
                                for the translucent list (see gxRend.cpp
                                BLEND_MODE()). on (default, correct) applies the
                                polygon's actual blend factors each frame;
                                off (legacy) skips the per-polygon override and uses
                                the GX default, which is faster but renders
                                Resident Evil 3's translucent polygons incorrectly.
        fps_boost=on        <- on/off, only used when blend_mode=on (see gxRend.cpp
                                BLEND_FPS_BOOST()). on forces alpha_fmt=0 (skips the
                                alpha-test/ZCompLoc pass) for every polygon outside
                                the translucent list, saving a couple of FPS (e.g.
                                Castlevania) at the cost of incorrect alpha on some
                                opaque/punch-through polys. Default off (correct).
        punch_through=on    <- on/off, punch-through list fix (see gxRend.cpp
                                PUNCH_THROUGH_FIX()). on (default, correct) draws
                                lists in OP -> PT -> TR order with the PT list
                                alpha-tested against PT_ALPHA_REF and blending off,
                                like real PVR; off (legacy) draws PT polys last, in
                                the translucent blend state.
        offset_color=on     <- on/off, offset (specular) color (see gxRend.cpp
                                OFFSET_COLOR_FIX()). on renders textured polys as
                                PIX = base*tex + offset like real PVR (specular
                                highlights on cars/water), costing 4 bytes/vertex
                                of FIFO and a second TEV stage on offset polys;
                                off (default, legacy) drops the offset color.
        trans_sort=on       <- on/off, translucent depth sort (see gxRend.cpp
                                TRANS_SORT()). Real PVR autosorts translucent
                                pixels in hardware; on sorts the TR strips
                                back-to-front (painter's algorithm) before
                                drawing, fixing wrong overlaps in alpha-heavy
                                scenes, at some CPU cost per frame;
                                off (default, legacy) draws in submission order.
        autosort=2          <- 0..4, REAL per-pixel PVR autosort via GX depth
                                peeling (see gxRend.cpp AUTOSORT()). The value
                                is the max number of translucent depth LAYERS
                                composited per pixel: each layer costs ~2 extra
                                walks of the translucent geometry plus 2 EFB Z
                                copies, so this is very GPU-heavy — use 2 or 3,
                                per-game, only where trans_sort's per-strip
                                painter sort is not enough (intersecting or
                                interleaved translucent geometry). Overrides
                                trans_sort; hokuto_hack overrides it. Best with
                                punch_through=on. 0 (default): off.
        render_to_texture=on <- on/off/overlay/keep, render-to-texture support (see
                                gxRend.cpp RENDER_TO_TEXTURE()). Frames whose
                                write address (FB_W_SOF1) has bit 24 set target
                                the 64-bit texture area — mirrors, TV screens,
                                sniper scopes, some menu effects.
                                on renders them and copies the EFB back into
                                emulated VRAM at FB_W_SOF1 so the game can bind
                                the result as a texture, at the cost of an EFB
                                copy + CPU convert per RTT frame;
                                keep is on PLUS the render does not consume
                                the TA list, so the display render that follows
                                draws the whole accumulated list -- needed by a
                                game that renders one list twice (Silent Scope's
                                sniper scope). The shared list is split by
                                user tile clip: clipped strips go to the
                                texture, unclipped ones to the screen;
                                overlay does not resolve a texture but carries
                                the pass's geometry into the next display frame
                                and draws it last, flat on top (near-plane
                                parked, GX_ALWAYS, no Z-write) — for passes we
                                cannot resolve, e.g. Silent Scope's crosshair;
                                off (default, legacy) drops those frames, but
                                leaves their geometry in the buffers where it
                                leaks into the next frame uncontrolled.
        mipmap=fast         <- off/fast/trilinear (or 0/1/2), GX mipmap
                                generation (see gxRend.cpp MIPMAP_*()).
                                off (default) = legacy base-level-only, fastest;
                                fast = generated mip chain sampled with
                                nearest-mip bilinear — kills distant-texture
                                shimmer at near-zero GPU cost; trilinear = best
                                quality but takes 2 texture cycles/texel on
                                Hollywood, halving texture fill rate (-40% FPS
                                in Test Drive 6).
        yuv_twiddle_fix=on  <- on/off (default off). Fixes TWIDDLED YUV422
                                textures (static YUV art, not FMV): the legacy
                                decoder swapped the two u16 halves of each source
                                u32 and read luma/chroma from the wrong byte, so
                                the picture collapses onto a green<->magenta
                                duotone. Virtua Fighter 3tb "FIRST MATCH".
        seam_fix=on         <- on/off (default off), half-texel UV inset via a
                                per-texture GX matrix (see gxRend.cpp SEAM_FIX()).
                                Stops GX_LINEAR from sampling past a sprite's own
                                texels, killing the thin black "seam" line between
                                2D tiles/sprites without dropping to GX_NEAR. Keeps
                                wrap/tiling intact (sub-texel shift only).
        fog=on              <- on/off (default off). Per-polygon PVR2 fog (see
                                gxRend.cpp FOG()): honours each polygon's
                                TSP.FogCtrl - look-up table (FOG_TABLE +
                                FOG_DENSITY, colour FOG_COL_RAM), per-vertex
                                (coefficient = the vertex's offset-colour alpha,
                                colour FOG_COL_VERT), no fog, and LUT mode 2.
                                Evaluated per vertex on the CPU and blended by one
                                extra TEV stage. Gives racers/outdoor games their
                                distance haze back instead of hard geometry that
                                pops in. Costs 4 bytes/vertex plus a TEV stage on
                                fogged polygons only.
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
        legacy_depth=on     <- reproduce the depth pipeline exactly as it stood
                                at commit 1bb8c27, the last state known to render
                                certain games correctly. Three things differ from
                                today and all three are restored together: fixed
                                planes NEAR=0.001 / FAR=10000*1.001; vert_base
                                clamps 1/W at 0.001 instead of 0.0001; and no
                                per-vertex min/max tracking or margin/HUD vertex
                                fixups. The clamp is the part fixed_depth cannot
                                express, which is why no fixed_depth value
                                reproduces 1bb8c27. Overrides fixed_depth.
        depth_clip=1        <- 0/1/2, real-Wii Z-clip workaround (see gxRend.cpp
                                DEPTH_CLIP_*()). Dolphin never Z-clips, so
                                menus/intros that sit on or beyond the depth
                                planes show there but vanish on real hardware.
                                0 (default, legacy) leaves XF clipping on;
                                1 pads the dynamic near plane by 0.1% so the
                                nearest 2D layer can't land exactly on it;
                                2 disables XF clipping entirely (Dolphin
                                behaviour: out-of-range Z clamps instead of
                                the poly vanishing).
        hud_pass=2          <- 0/1/2 (default 0=off), rescue the 2D HUD that
                                fixed_depth=tight clips (see gxRend.cpp HUD_PASS()).
                                Tight's near plane (W=0.1) fixes 3D Z-fighting but
                                clips the HUD, which parks nearer than that; this
                                re-parks the HUD onto the near plane (screen
                                position preserved) and draws it GX_ALWAYS.
                                1 = overlay (no Z-write) — HUD may be overdrawn by
                                geometry drawn after it. 2 = protect (Z-write at
                                the near plane) — later scene polys fail GEQUAL so
                                the HUD stays on top; avoid on games with a large
                                near-clipped quad drawn early (it would stamp the
                                whole scene's depth). Companion to fixed_depth=2 —
                                a no-op without it.
        subpass_zclear=on   <- on/off, sub-pass depth-only Z clear (see gxRend.cpp
                                SUBPASS_ZCLEAR()). Re-parks the whole Z buffer at a
                                known W (a full-canvas GX_ALWAYS quad, color/alpha
                                writes off — GX has no partial/depth-only EFB clear
                                outside a real copy-out) WITHOUT touching color, so
                                a geometry group drawn right after (e.g. a
                                hud_pass=2 PROTECT-mode overlay) starts from a
                                clean depth baseline instead of whatever the main
                                scene left behind. off (default): single shared
                                depth pass, legacy. on: re-park before that later
                                pass — opt-in per-game, since a game that
                                interleaves HUD strips back into the middle of its
                                3D geometry would have its main scene's depth
                                wiped early.
        poly_offset=1       <- 0..3, native polygon offset / Z bias tier (see
                                gxRend.cpp POLY_OFFSET()). Real hardware has no
                                glPolygonOffset-style register — no programmable
                                shaders means there is no way to nudge a
                                fragment's post-transform Z from the pixel
                                pipeline. The GX equivalent is a Z-TEXTURE in ADD
                                mode (GX_SetZTexture): every fragment drawn while
                                it's bound gets a constant bias added to its depth
                                before the Z test/write, a slope-independent
                                "units" bias. Applied to the Punch-Through (PT)
                                list, where the Dreamcast pipes co-planar decals /
                                shadows / road-markings that PVR's tile order used
                                to sort for free. 0 (default): off, legacy.
                                1..3: increasing bias strength tier (see
                                s_poly_offset_frac[] in gxRend.cpp).
        async_render=on     <- on/off, async GPU present (see gxRend.cpp
                                ASYNC_RENDER()). off (default, legacy) blocks the
                                CPU in GX_DrawDone() until the GPU finishes the
                                frame; on queues the frame and returns at once —
                                the SH4 core emulates the next frame while the
                                GPU draws, and the finished frame is presented
                                at the start of the next one. One frame of
                                extra display latency; big FPS gain whenever
                                the GPU takes a meaningful slice of the frame.
        tmem_cache=on       <- on/off, persistent GPU texture cache (see gxRend.cpp
                                TMEM_CACHE()). off (default, legacy) invalidates
                                the GPU's 1MB texture cache (TMEM) every frame,
                                re-fetching every texel from RAM; on invalidates
                                only when a texture is actually re-decoded, so
                                unchanged textures stay cached across frames —
                                more texture fill rate.
        cdda=on             <- on/off, CD audio (Red Book) music (see sgc_if.cpp
                                CDDA_FIX()). off (default, legacy) leaves the
                                AICA EXTS0 input silent, so games that play
                                music as CD audio tracks have no music; on pulls
                                the playing track's sectors from the disc image
                                (~75 reads/s) and mixes them into the output.
        mute_pcm16=on       <- on/off, silence 16-bit PCM channels (see sgc_if.cpp
                                MUTE_PCM16_FIX()). off (default, legacy) plays all
                                sample formats. on drops any AICA channel whose
                                PCMS==0 (16-bit PCM) at KEY_ON — a workaround for
                                ChuChu Rocket's echoey/slow-motion 16-bit SFX. Note
                                this also mutes 16-bit music/voices, so it is a
                                per-game hack, not a general fix.
        bg_poly=on          <- on/off, background polygon rendering (see gxRend.cpp
                                BG_POLY_FIX()). ISP_BACKGND_T's 3 vertices are
                                normally only used for the EFB clear color;
                                on additionally barycentric-extrapolates them
                                into a full-screen textured/Gouraud quad
                                (needed by Who Wants to Be a Millionaire's
                                background), at the cost of an extra texture
                                bind + polygon draw every frame;
                                off (default, legacy) draws nothing extra.
        x_scaler=on         <- on/off, PVR horizontal (X) scaler support (see
                                plugs/drkPvr/SPG.cpp X_SCALER() and regs.cpp
                                SCALER_CTL). A few games (Omicron The Nomad
                                Soul, Wacky Races) set SCALER_CTL.hscale and
                                render the scene 1280 pixels wide; real
                                hardware's video scaler then halves it 2:1
                                on framebuffer write (horizontal SSAA).
                                on widens the projected canvas to 1280 so
                                the whole scene maps to the screen; off
                                (default, legacy) leaves the canvas at 640,
                                showing only the left half in those games.
        y_scaler=on         <- on/off, PVR vertical (Y) scaler support (see
                                gxRend.cpp Y_SCALER()). The other half of the
                                same register x_scaler reads: SCALER_CTL's
                                vscalefactor field (6.10 fixed point, 0x400 =
                                1.0). Above 1.0 the CORE renders the scene
                                that many times TALLER and the video scaler
                                shrinks it back down on framebuffer write
                                (vertical SSAA / flicker filter); below 1.0
                                it renders shorter and the scaler stretches.
                                on scales the projected canvas height by the
                                factor so the whole scene maps to the screen;
                                off (default, legacy) ignores the factor and
                                shows only a slice in those games.
        h_scaler=on         <- on/off, PVR horizontal (H) scaler at video
                                output (see gxRend.cpp H_SCALER()):
                                VO_CONTROL.pixel_double. In the low-res video
                                modes the framebuffer holds a HALF-width (320)
                                image and the video DAC emits every pixel
                                twice to fill the 640-pixel line, so the scene
                                is drawn in a 320-wide screen space. on halves
                                the canvas to match; off (default, legacy)
                                projects it into a 640 canvas, where it fills
                                only the left half. This is the register-
                                driven counterpart of canvas_width below (for
                                games that render narrow WITHOUT setting the
                                bit), so an explicit canvas_width wins.
        canvas_width=384    <- integer, forced canvas width in 240p modes
                                (see gxRend.cpp CANVAS_WIDTH()). Some low-res
                                games draw their scene narrower than 640 and
                                pad the rest with filler, so no register or
                                measurement reveals the real width. This
                                forces the projected canvas width (320..1280)
                                whenever the video mode is non-interlaced
                                NTSC/PAL (240p); interlaced screens (boot
                                logos) keep the 640 canvas. Street Fighter
                                III / Double Impact: 384 (CPS3 arcade width).
                                0 (default): off, legacy canvas.
        split_screen=on     <- on/off, split-screen multiplayer (see gxRend.cpp
                                SPLIT_SCREEN()). 2P games (Daytona USA
                                multiplayer) render one pass per player
                                viewport, clipped with FB_X_CLIP/FB_Y_CLIP.
                                on draws each partial-clip pass scissored into
                                its half of the EFB and presents the assembled
                                frame once per vblank; off (default, legacy)
                                presents every pass fullscreen, so only one
                                player's view shows.
        layout_chuchu=on    <- on/off, ChuChu Rocket special controller
                                layout (see main.cpp g_special_layout_preset,
                                drkMapleDevices.cpp MapButtons()). All players:
                                Wiimote D-Pad Down/Right/Left/Up -> DC A/B/X/Y
                                (DC D-Pad unassigned in this mode); GameCube
                                A/B/X/Y -> DC A/X/B/Y; Classic Controller
                                keeps its normal by-position mapping (already
                                the same layout). off (default) restores the
                                normal per-button mapping.
        audio_buffers=1     <- 0..3 or default/auto/saved, forces
                                settings.emulator.AudioBuffers (see
                                nullDC.cpp LoadSettings() and
                                wii/wii_audio.cpp). 0 never blocks the
                                emulation thread — samples are dropped on
                                overrun; 1..3 blocks the caller each push
                                until fewer than N buffers are queued, trading
                                emulation speed for smoother audio pacing on
                                games that produce audio in bursts.
                                default/auto/saved explicitly resets it to
                                the saved/cfg value (undoing a [default]
                                section override, or a previous game's
                                setting carried over this boot) — leaving
                                the key out entirely does the same thing
                                UNLESS an earlier section already forced it.

    [default] is a special section, not matched against the filename: its
    fields are applied first, on every launch, before the per-game match
    below runs. Per-game sections only need to list fields that differ
    from [default].

    First matching rule wins.
    Fields left unset by both [default] and the matched section stay at
    whatever the user selected in the UI.

    The file is never kept in RAM: game_presets_apply() streams it from SD
    (one pass for [default], one for the per-game match), parsing into a
    single scratch slot — the old 4096-entry table cost ~2.6 MB of MEM2.

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
extern int g_gx_preset;
extern int g_lod_bias_preset;
extern int g_aniso_preset;
extern int g_ratio_preset;
extern int g_advanced_alpha_preset;
extern int g_frameskip_preset;
extern int g_texture_cache_preset;
extern int g_ppz_write_preset;
extern int g_trans_zwrite_preset;
extern int g_sprite_color_preset;
extern int g_vtx_alpha_preset;
extern int g_x_scaler_preset;
extern int g_y_scaler_preset;
extern int g_h_scaler_preset;
extern int g_canvas_width_preset;
extern int g_4bpp_preset;
extern int g_8bpp_preset;
extern int g_jojo_fix_preset;
extern int g_vq_cmpr_preset;
extern int g_decal_alpha_preset;
extern int g_speed_limiter_preset;
extern int g_render_delay_preset;
extern int g_vertex_color_preset;
extern int g_blend_mode_preset;
extern int g_rgb565_opaque_alpha_preset;
extern int g_blend_fps_boost_preset;
extern int g_show_fps_overlay;
extern int g_punch_through_preset;
extern int g_offset_color_preset;
extern int g_trans_sort_preset;
extern int g_autosort_preset;
extern int g_render_to_texture_preset;
extern int g_split_screen_preset;
extern int g_special_layout_preset;
extern int g_mipmap_preset;
extern int g_seam_fix_preset;
extern int g_fog_preset;
extern int g_yuv_twiddle_fix_preset;
extern int g_fixed_depth_preset;
extern int g_legacy_depth_preset;
extern int g_depth_clip_preset;
extern int g_hud_pass_preset;
extern int g_async_render_preset;
extern int g_tmem_cache_preset;
extern int g_cdda_preset;
extern int g_mute_pcm16_preset;
extern int g_bg_poly_preset;
extern int g_hokuto_hack_preset;
extern int g_isp_depth_func_preset;
extern int g_isp_cull_preset;
extern int g_subpass_zclear_preset;
extern int g_poly_offset_preset;
extern int g_audio_buffers_preset;
extern int g_arm7_speed_preset;
extern int g_sh4_clock_preset;
extern int g_jit_sbp_preset;
extern int g_dma_fix_preset;
extern int g_fastmem_preset;
extern int g_bcache_preset;
extern int g_fpu_pin_preset;
extern int g_jit_align_preset;
extern int g_sched_preset;
extern int g_player_count;
extern int g_controller_type;
extern int g_framebuffer_2d;
extern int g_fmv_format_preset;

// Set once at boot (main.cpp) from WiiDRC_Inited() — true only on real Wii U
// hardware in vWii mode. Consumed below by <angle-bracket> alias groups.
extern bool g_is_wiiu;

// ---------------------------------------------------------------------------
// Internal structures
// ---------------------------------------------------------------------------

#define MAX_KEYWORD_LEN 64

struct GamePreset
{
    // -1 = not set (leave user default untouched)
    int accuracy;
    int graphics;
    int gx;
    int lod_bias;
    int aniso;
    int ratio;
    int adv_alpha;
    int frameskip;
    int tex_cache;
    int ppz_write;
    int trans_zwrite;
    int sprite_color;
    int vtx_alpha;
    int x_scaler;
    int y_scaler;
    int h_scaler;
    int canvas_width;
    int bpp4;
    int bpp8;
    int jojo_fix;
    int vq_cmpr;
    int decal_alpha;
    int speed_limiter;
    int render_delay;
    int vertex_color;
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
    int autosort;
    int render_to_texture;
    int split_screen;
    int layout; // -1=not set, 0=SPECIAL_LAYOUT_OFF, 1=SPECIAL_LAYOUT_CHUCHU (see main.cpp)
    int mipmap;
    int seam_fix;
    int fog;
    int yuv_twiddle_fix;
    int fixed_depth;
    int legacy_depth;
    int depth_clip;
    int hud_pass;
    int async_render;
    int tmem_cache;
    int show_fps;
    int cdda;
    int mute_pcm16;
    int bg_poly;
    int hokuto_hack;
    int isp_depth_func;
    int isp_cull;
    int subpass_zclear;
    int poly_offset;
    int audio_buffers;
    int arm7_speed;
    int sh4_clock;
    int jit_sbp;
    int dma_fix;
    int fastmem;
    int bcache;
    int fpu_pin;
    int jit_align;
    int sched;
};

// Nothing from the .cfg stays in RAM: game_presets_apply() streams the file
// from SD and parses the one section it needs into this single scratch slot,
// so the whole system costs ~130 bytes of BSS instead of a MEM2 table.
static GamePreset s_scratch;

// Path remembered by game_presets_load() for the apply() streaming passes
static char s_cfg_path[256] = "";

// Exported so main.cpp can display the matched preset name in the options menu
char g_matched_preset_name[MAX_KEYWORD_LEN] = "";

// True when the section that matched used <angle> brackets (the Wii U gate),
// so main.cpp shows <name> rather than [name].
bool g_matched_preset_is_wiiu = false;

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

// Binary fields: "on"/"off" is the documented format; "1"/"0" and
// "true"/"false" are also accepted so hand-edited or older config lines
// keep working.
static int parse_bool(const char* v)
{
    if (key_eq(v, "on")  || key_eq(v, "true")  || strcmp(v, "1") == 0) return 1;
    if (key_eq(v, "off") || key_eq(v, "false") || strcmp(v, "0") == 0) return 0;
    printf("[game_presets] Unknown on/off value: '%s'\n", v);
    return -1;
}

// render_to_texture is on/off plus a third "overlay" state (carry the dropped
// RTT pass into the next display frame and draw it flat on top — see
// RTT_CARRY_OVERLAY() in gxRend.cpp). Every existing on/off line keeps working.
static int parse_rtt(const char* v)
{
    if (key_eq(v, "overlay") || key_eq(v, "carry") || strcmp(v, "2") == 0) return 2;
    if (key_eq(v, "keep")    || key_eq(v, "keep_list") || strcmp(v, "3") == 0) return 3;
    return parse_bool(v);
}

static int parse_accuracy(const char* v)
{
    if (key_eq(v, "fast"))     return 0;
    if (key_eq(v, "balanced")) return 1;
    if (key_eq(v, "accurate")) return 2;
    printf("[game_presets] Unknown accuracy value: '%s'\n", v);
    return -1;
}

// graphics is the texture FILTER only: low=GX_NEAR, normal=GX_LINEAR. The old
// high/extra levels were normal plus a fixed lod_bias/bias-clamp/aniso bundle,
// which are their own keys now — old configs keep loading (mapped to normal)
// with a one-line hint instead of an "unknown value" rejection.
static int parse_graphics(const char* v)
{
    if (key_eq(v, "low"))    return 0;
    if (key_eq(v, "normal")) return 1;
    if (key_eq(v, "high") || key_eq(v, "extra"))
    {
        printf("[game_presets] graphics=%s is gone (filter only now) -> normal;"
               " use gx=/lod_bias=/aniso= for the rest\n", v);
        return 1;
    }
    printf("[game_presets] Unknown graphics value: '%s'\n", v);
    return -1;
}

// gx= is on/off like every other binary field, but the menu labels it with the
// GX constant names it actually sets, so those spellings are accepted too.
static int parse_gx(const char* v)
{
    if (key_eq(v, "gx_enable")  || key_eq(v, "gx_enabled"))  return 1;
    if (key_eq(v, "gx_disable") || key_eq(v, "gx_disabled")) return 0;
    return parse_bool(v);
}

// Texture LOD bias -> index into gxRend.cpp s_lod_bias_steps[]. Written as the
// value itself in the .cfg (lod_bias=-0.5); the menu index is an implementation
// detail. Both "-0.5" and "-.5" spellings are accepted, as is a bare "0".
static int parse_lod_bias(const char* v)
{
    if (key_eq(v, "-1")   || key_eq(v, "-1.0")  || key_eq(v, "-1.00")) return 0;
    if (key_eq(v, "-0.75")|| key_eq(v, "-.75"))                        return 1;
    if (key_eq(v, "-0.5") || key_eq(v, "-.5")   || key_eq(v, "-0.50")) return 2;
    if (key_eq(v, "0")    || key_eq(v, "0.0")   || key_eq(v, "0.00")
                          || key_eq(v, "off")   || key_eq(v, "default")) return 3;
    if (key_eq(v, "0.5")  || key_eq(v, ".5")    || key_eq(v, "0.50")
                          || key_eq(v, "+0.5"))                        return 4;
    printf("[game_presets] Unknown lod_bias value: '%s'"
           " (use -1 | -0.75 | -0.5 | 0.0 | 0.5)\n", v);
    return -1;
}

// Anisotropic filtering -> index into gxRend.cpp s_aniso_steps[]. Hollywood has
// no 8x mode, so the menu stops at 4x; an aniso=8x line still loads, clamped.
static int parse_aniso(const char* v)
{
    if (key_eq(v, "0") || key_eq(v, "0x") || key_eq(v, "off") || key_eq(v, "1x")) return 0;
    if (key_eq(v, "2") || key_eq(v, "2x")) return 1;
    if (key_eq(v, "4") || key_eq(v, "4x")) return 2;
    if (key_eq(v, "8") || key_eq(v, "8x"))
    {
        printf("[game_presets] aniso=8x does not exist on Hollywood -> 4x\n");
        return 2;
    }
    printf("[game_presets] Unknown aniso value: '%s' (use 0x | 2x | 4x)\n", v);
    return -1;
}

static int parse_ratio(const char* v)
{
    if (key_eq(v, "original"))   return 0;
    if (key_eq(v, "fullscreen")) return 1;
    if (key_eq(v, "auto"))       return 2;
    printf("[game_presets] Unknown ratio value: '%s'\n", v);
    return -1;
}

static int parse_frameskip(const char* v)
{
    if (strcmp(v, "0")      == 0) return 0;
    if (strcmp(v, "1")      == 0) return 1;
    if (strcmp(v, "2")      == 0) return 2;
    if (key_eq(v, "auto"))        return 3;
    if (key_eq(v, "auto_max") || key_eq(v, "automax")) return 4;
    printf("[game_presets] Unknown frameskip value: '%s'\n", v);
    return -1;
}

static int parse_tex_cache(const char* v)
{
    if (key_eq(v, "very_fast")) return 0;
    if (key_eq(v, "very_fast_plus")) return 6; // fast + sentinel at the un-mipped address
    if (key_eq(v, "very_fast+"))     return 6; // accepted spelling of the same preset
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

// -1 is itself a valid, meaningful audio_buffers value (DEFAULT/SAVED — leave
// settings.emulator.AudioBuffers alone), unlike every other field where -1 is
// only ever the "key absent from this section" sentinel. So this field uses
// -2 for absent (see preset_clear / preset_apply_fields) and this parser
// returns -1 for the explicit "default"/"auto"/"saved" keyword.
static int parse_audio_buffers(const char* v)
{
    if (key_eq(v, "default") || key_eq(v, "auto") || key_eq(v, "saved")) return -1;
    int n = atoi(v);
    if (n >= 0 && n <= 3) return n;
    printf("[game_presets] Unknown audio_buffers value: '%s'\n", v);
    return -2;
}

static int parse_players(const char* v)
{
    int n = atoi(v);
    if (n >= 1 && n <= 4) return n;
    printf("[game_presets] Unknown players value: '%s'\n", v);
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

// SH4 underclock: effective SH4 clock in MHz. The menu steps 150..200 by 5, but
// the .cfg accepts any integer in [150,200] (clamped). Returns -1 on garbage so
// the field is left at the user/UI value.
static int parse_sh4_clock(const char* v)
{
    int n = atoi(v);
    if (n <= 0)
    {
        printf("[game_presets] Unknown sh4_clock value: '%s'\n", v);
        return -1;
    }
    if (n < 150) n = 150;
    if (n > 200) n = 200;
    return n;
}

// ---------------------------------------------------------------------------
// Apply one key=value pair to a preset slot
// ---------------------------------------------------------------------------

static void apply_kv(GamePreset* p, const char* key, const char* val)
{
    if      (key_eq(key, "accuracy"))   p->accuracy   = parse_accuracy(val);
    else if (key_eq(key, "graphics"))   p->graphics   = parse_graphics(val);
    else if (key_eq(key, "gx"))         p->gx         = parse_gx(val);
    else if (key_eq(key, "lod_bias"))   p->lod_bias   = parse_lod_bias(val);
    else if (key_eq(key, "aniso"))      p->aniso      = parse_aniso(val);
    else if (key_eq(key, "ratio"))      p->ratio      = parse_ratio(val);
    else if (key_eq(key, "adv_alpha"))  p->adv_alpha  = parse_bool(val);
    else if (key_eq(key, "frameskip"))  p->frameskip  = parse_frameskip(val);
    else if (key_eq(key, "tex_cache"))  p->tex_cache  = parse_tex_cache(val);
    else if (key_eq(key, "ppz_write"))  p->ppz_write  = parse_bool(val);
    else if (key_eq(key, "trans_zwrite")) p->trans_zwrite = parse_bool(val);
    else if (key_eq(key, "sprite_color")) p->sprite_color = parse_bool(val);
    else if (key_eq(key, "vtx_alpha"))    p->vtx_alpha    = parse_bool(val);
    else if (key_eq(key, "x_scaler"))   p->x_scaler   = parse_bool(val);
    else if (key_eq(key, "y_scaler"))   p->y_scaler   = parse_bool(val);
    else if (key_eq(key, "h_scaler"))   p->h_scaler   = parse_bool(val);
    else if (key_eq(key, "canvas_width")) p->canvas_width = atoi(val);
    else if (key_eq(key, "4bpp"))       p->bpp4       = parse_bpp(val);
    else if (key_eq(key, "8bpp"))       p->bpp8       = parse_bpp(val);
    else if (key_eq(key, "jojo_fix"))   p->jojo_fix   = parse_bool(val);
    else if (key_eq(key, "vq_cmpr"))    p->vq_cmpr    = parse_bool(val);
    else if (key_eq(key, "decal_alpha")) p->decal_alpha = parse_bool(val);
    else if (key_eq(key, "speed_limiter")) p->speed_limiter = parse_bool(val);
    else if (key_eq(key, "render_delay"))  p->render_delay  = parse_bool(val);
    else if (key_eq(key, "vertex_color")) p->vertex_color = parse_bool(val);
    else if (key_eq(key, "players"))    p->players    = parse_players(val);
    else if (key_eq(key, "controller")) p->controller = parse_controller(val);
    else if (key_eq(key, "framebuffer_2d")) p->framebuffer_2d = parse_bool(val);
    else if (key_eq(key, "fmv_format"))     p->fmv_format     = parse_fmv_format(val);
    else if (key_eq(key, "blend_mode"))     p->blend_mode     = parse_bool(val);
    else if (key_eq(key, "rgb565_opaque_alpha")) p->rgb565_opaque_alpha = parse_bool(val);
    else if (key_eq(key, "fps_boost"))      p->blend_fps_boost = parse_bool(val);
    else if (key_eq(key, "punch_through"))  p->punch_through  = parse_bool(val);
    else if (key_eq(key, "offset_color"))   p->offset_color   = parse_bool(val);
    else if (key_eq(key, "trans_sort"))     p->trans_sort     = parse_bool(val);
    else if (key_eq(key, "autosort"))       p->autosort       = atoi(val);
    else if (key_eq(key, "render_to_texture")) p->render_to_texture = parse_rtt(val);
    else if (key_eq(key, "split_screen"))   p->split_screen   = parse_bool(val);
    else if (key_eq(key, "layout_chuchu"))  { int b = parse_bool(val); if (b >= 0) p->layout = b ? 1 /* SPECIAL_LAYOUT_CHUCHU */ : 0 /* SPECIAL_LAYOUT_OFF */; }
    else if (key_eq(key, "mipmap"))         p->mipmap         = parse_mipmap(val);
    else if (key_eq(key, "seam_fix"))       p->seam_fix       = parse_bool(val);
    else if (key_eq(key, "fog"))            p->fog            = parse_bool(val);
    else if (key_eq(key, "yuv_twiddle_fix")) p->yuv_twiddle_fix = parse_bool(val);
    else if (key_eq(key, "fixed_depth"))    p->fixed_depth    = atoi(val);
    else if (key_eq(key, "legacy_depth"))   p->legacy_depth   = parse_bool(val);
    else if (key_eq(key, "depth_clip"))     p->depth_clip     = atoi(val);
    else if (key_eq(key, "hud_pass"))       p->hud_pass       = atoi(val);
    else if (key_eq(key, "async_render"))   p->async_render   = parse_bool(val);
    else if (key_eq(key, "tmem_cache"))     p->tmem_cache     = parse_bool(val);
    else if (key_eq(key, "show_fps"))       p->show_fps       = parse_bool(val);
    else if (key_eq(key, "cdda"))           p->cdda           = parse_bool(val);
    else if (key_eq(key, "mute_pcm16"))     p->mute_pcm16     = parse_bool(val);
    else if (key_eq(key, "bg_poly"))        p->bg_poly        = parse_bool(val);
    else if (key_eq(key, "hokuto_hack"))    p->hokuto_hack    = parse_bool(val);
    else if (key_eq(key, "isp_depth_func")) p->isp_depth_func = atoi(val);
    else if (key_eq(key, "isp_cull"))       p->isp_cull       = atoi(val);
    else if (key_eq(key, "subpass_zclear")) p->subpass_zclear = parse_bool(val);
    else if (key_eq(key, "poly_offset"))    p->poly_offset    = atoi(val);
    else if (key_eq(key, "audio_buffers"))  p->audio_buffers  = parse_audio_buffers(val);
    else if (key_eq(key, "arm7_speed"))     p->arm7_speed     = atoi(val);
    else if (key_eq(key, "sh4_clock"))      p->sh4_clock      = parse_sh4_clock(val);
    else if (key_eq(key, "jit_sbp"))        p->jit_sbp        = atoi(val);
    else if (key_eq(key, "dma_fix"))        p->dma_fix        = parse_bool(val);
    else if (key_eq(key, "fastmem"))        p->fastmem        = parse_bool(val);
    else if (key_eq(key, "bcache"))         p->bcache         = parse_bool(val);
    else if (key_eq(key, "fpu_pin"))        p->fpu_pin        = parse_bool(val);
    else if (key_eq(key, "jit_align"))      p->jit_align      = parse_bool(val);
    else if (key_eq(key, "sched"))          p->sched          = parse_bool(val);
    else printf("[game_presets] Unknown key: '%s'\n", key);
}

// Mark every field of a preset slot as "not set"
static void preset_clear(GamePreset* cur)
{
    memset(cur, 0, sizeof(*cur));
    cur->accuracy = cur->graphics  = cur->ratio    = cur->adv_alpha = -1;
    cur->frameskip= cur->tex_cache = cur->bpp4     = cur->bpp8      = -1;
    cur->gx = cur->lod_bias = cur->aniso = -1;
    cur->jojo_fix = -1;
    cur->vq_cmpr = -1;
    cur->decal_alpha = -1;
    cur->speed_limiter = -1;
    cur->render_delay = -1;
    cur->vertex_color = -1;
    cur->players  = cur->controller                                  = -1;
    cur->ppz_write = -1;
    cur->trans_zwrite = -1;
    cur->sprite_color = -1;
    cur->vtx_alpha = -1;
    cur->x_scaler = -1;
    cur->y_scaler = -1;
    cur->h_scaler = -1;
    cur->canvas_width = -1;
    cur->framebuffer_2d = -1;
    cur->fmv_format = -1;
    cur->blend_mode = -1;
    cur->rgb565_opaque_alpha = -1;
    cur->blend_fps_boost = -1;
    cur->punch_through = -1;
    cur->offset_color = -1;
    cur->trans_sort = -1;
    cur->autosort = -1;
    cur->render_to_texture = -1;
    cur->split_screen = -1;
    cur->layout = -1;
    cur->mipmap = -1;
    cur->seam_fix = -1;
    cur->fog = -1;
    cur->yuv_twiddle_fix = -1;
    cur->fixed_depth = -1;
    cur->legacy_depth = -1;
    cur->depth_clip = -1;
    cur->hud_pass = -1;
    cur->async_render = -1;
    cur->tmem_cache = -1;
    cur->show_fps = -1;
    cur->cdda = -1;
    cur->mute_pcm16 = -1;
    cur->bg_poly = -1;
    cur->hokuto_hack = -1;
    cur->isp_depth_func = -1;
    cur->isp_cull = -1;
    cur->subpass_zclear = -1;
    cur->poly_offset = -1;
    cur->audio_buffers = -2; // -2 = absent (leave live state alone); -1 is a real value here (see parse_audio_buffers)
    cur->arm7_speed = -1;
    cur->sh4_clock = -1;
    cur->jit_sbp = -1;
    cur->dma_fix = -1;
    cur->fastmem = -1;
    cur->bcache = -1;
    cur->fpu_pin = -1;
    cur->jit_align = -1;
    cur->sched = -1;
}

// Apply every set field of a preset slot onto the live g_*_preset globals
static void preset_apply_fields(const GamePreset* p)
{
    if (p->accuracy   >= 0) { g_accuracy_preset      = p->accuracy;   printf("  accuracy   -> %d\n", p->accuracy);   }
    if (p->graphics   >= 0) { g_graphism_preset       = p->graphics;   printf("  graphics   -> %d\n", p->graphics);   }
    if (p->gx         >= 0) { g_gx_preset             = p->gx;         printf("  gx         -> %d\n", p->gx);         }
    if (p->lod_bias   >= 0) { g_lod_bias_preset       = p->lod_bias;   printf("  lod_bias   -> %d\n", p->lod_bias);   }
    if (p->aniso      >= 0) { g_aniso_preset          = p->aniso;      printf("  aniso      -> %d\n", p->aniso);      }
    if (p->ratio      >= 0) { g_ratio_preset          = p->ratio;      printf("  ratio      -> %d\n", p->ratio);      }
    if (p->adv_alpha  >= 0) { g_advanced_alpha_preset = p->adv_alpha;  printf("  adv_alpha  -> %d\n", p->adv_alpha);  }
    if (p->frameskip  >= 0) { g_frameskip_preset      = p->frameskip;  printf("  frameskip  -> %d\n", p->frameskip);  }
    if (p->tex_cache  >= 0) { g_texture_cache_preset  = p->tex_cache;  printf("  tex_cache  -> %d\n", p->tex_cache);  }
    if (p->ppz_write  >= 0) { g_ppz_write_preset      = p->ppz_write;  printf("  ppz_write  -> %d\n", p->ppz_write);  }
    if (p->trans_zwrite >= 0) { g_trans_zwrite_preset = p->trans_zwrite; printf("  trans_zwrite -> %d\n", p->trans_zwrite); }
    if (p->sprite_color >= 0) { g_sprite_color_preset = p->sprite_color; printf("  sprite_color -> %d\n", p->sprite_color); }
    if (p->vtx_alpha    >= 0) { g_vtx_alpha_preset    = p->vtx_alpha;    printf("  vtx_alpha    -> %d\n", p->vtx_alpha); }
    if (p->x_scaler   >= 0) { g_x_scaler_preset       = p->x_scaler;   printf("  x_scaler   -> %d\n", p->x_scaler);   }
    if (p->y_scaler   >= 0) { g_y_scaler_preset       = p->y_scaler;   printf("  y_scaler   -> %d\n", p->y_scaler);   }
    if (p->h_scaler   >= 0) { g_h_scaler_preset       = p->h_scaler;   printf("  h_scaler   -> %d\n", p->h_scaler);   }
    if (p->canvas_width >= 0) { g_canvas_width_preset  = p->canvas_width; printf("  canvas_width -> %d\n", p->canvas_width); }
    if (p->bpp4       >= 0) { g_4bpp_preset           = p->bpp4;       printf("  4bpp       -> %d\n", p->bpp4);       }
    if (p->bpp8       >= 0) { g_8bpp_preset           = p->bpp8;       printf("  8bpp       -> %d\n", p->bpp8);       }
    if (p->jojo_fix   >= 0) { g_jojo_fix_preset       = p->jojo_fix;   printf("  jojo_fix   -> %d\n", p->jojo_fix);   }
    if (p->vq_cmpr    >= 0) { g_vq_cmpr_preset        = p->vq_cmpr;    printf("  vq_cmpr    -> %d\n", p->vq_cmpr);    }
    if (p->decal_alpha >= 0) { g_decal_alpha_preset   = p->decal_alpha; printf("  decal_alpha -> %d\n", p->decal_alpha); }
    if (p->speed_limiter >= 0) { g_speed_limiter_preset = p->speed_limiter; printf("  speed_limiter -> %d\n", p->speed_limiter); }
    if (p->render_delay  >= 0) { g_render_delay_preset  = p->render_delay;  printf("  render_delay  -> %d\n", p->render_delay);  }
    if (p->vertex_color >= 0) { g_vertex_color_preset = p->vertex_color; printf("  vertex_color -> %d\n", p->vertex_color); }
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
    if (p->autosort       >= 0) { g_autosort_preset      = p->autosort;        printf("  autosort       -> %d\n", p->autosort);       }
    if (p->render_to_texture >= 0) { g_render_to_texture_preset = p->render_to_texture; printf("  render_to_texture -> %d\n", p->render_to_texture); }
    if (p->split_screen   >= 0) { g_split_screen_preset  = p->split_screen;    printf("  split_screen   -> %d\n", p->split_screen);   }
    if (p->layout         >= 0) { g_special_layout_preset = p->layout;         printf("  layout_chuchu  -> %d\n", p->layout);         }
    if (p->mipmap         >= 0) { g_mipmap_preset        = p->mipmap;          printf("  mipmap         -> %d\n", p->mipmap);         }
    if (p->seam_fix       >= 0) { g_seam_fix_preset      = p->seam_fix;        printf("  seam_fix       -> %d\n", p->seam_fix);       }
    if (p->fog            >= 0) { g_fog_preset           = p->fog;             printf("  fog            -> %d\n", p->fog);            }
    if (p->yuv_twiddle_fix >= 0) { g_yuv_twiddle_fix_preset = p->yuv_twiddle_fix; printf("  yuv_twiddle_fix -> %d\n", p->yuv_twiddle_fix); }
    if (p->fixed_depth    >= 0) { g_fixed_depth_preset   = p->fixed_depth;     printf("  fixed_depth    -> %d\n", p->fixed_depth);    }
    if (p->legacy_depth    >= 0) { g_legacy_depth_preset  = p->legacy_depth;     printf("  legacy_depth   -> %d\n", p->legacy_depth);    }
    if (p->depth_clip     >= 0) { g_depth_clip_preset    = p->depth_clip;      printf("  depth_clip     -> %d\n", p->depth_clip);     }
    if (p->hud_pass       >= 0) { g_hud_pass_preset      = p->hud_pass;        printf("  hud_pass       -> %d\n", p->hud_pass);       }
    if (p->async_render   >= 0) { g_async_render_preset  = p->async_render;    printf("  async_render   -> %d\n", p->async_render);   }
    if (p->tmem_cache     >= 0) { g_tmem_cache_preset    = p->tmem_cache;      printf("  tmem_cache     -> %d\n", p->tmem_cache);     }
    if (p->show_fps       >= 0) { g_show_fps_overlay     = p->show_fps;        printf("  show_fps       -> %d\n", p->show_fps);       }
    if (p->cdda           >= 0) { g_cdda_preset          = p->cdda;            printf("  cdda           -> %d\n", p->cdda);           }
    if (p->mute_pcm16     >= 0) { g_mute_pcm16_preset    = p->mute_pcm16;      printf("  mute_pcm16     -> %d\n", p->mute_pcm16);     }
    if (p->bg_poly        >= 0) { g_bg_poly_preset       = p->bg_poly;         printf("  bg_poly        -> %d\n", p->bg_poly);        }
    if (p->hokuto_hack    >= 0) { g_hokuto_hack_preset   = p->hokuto_hack;     printf("  hokuto_hack    -> %d\n", p->hokuto_hack);    }
    if (p->isp_depth_func >= 0) { g_isp_depth_func_preset = p->isp_depth_func; printf("  isp_depth_func -> %d\n", p->isp_depth_func); }
    if (p->isp_cull       >= 0) { g_isp_cull_preset      = p->isp_cull;        printf("  isp_cull       -> %d\n", p->isp_cull);       }
    if (p->subpass_zclear >= 0) { g_subpass_zclear_preset = p->subpass_zclear; printf("  subpass_zclear -> %d\n", p->subpass_zclear); }
    if (p->poly_offset    >= 0) { g_poly_offset_preset   = p->poly_offset;     printf("  poly_offset    -> %d\n", p->poly_offset);    }
    if (p->audio_buffers  != -2) { g_audio_buffers_preset = p->audio_buffers;  printf("  audio_buffers  -> %d\n", p->audio_buffers);  }
    if (p->arm7_speed     >= 0) { g_arm7_speed_preset     = p->arm7_speed;     printf("  arm7_speed     -> %d\n", p->arm7_speed);     }
    if (p->sh4_clock      >= 0) { g_sh4_clock_preset      = p->sh4_clock;      printf("  sh4_clock      -> %d\n", p->sh4_clock);      }
    if (p->jit_sbp        >= 0) { g_jit_sbp_preset        = p->jit_sbp;        printf("  jit_sbp        -> %d\n", p->jit_sbp);        }
    if (p->dma_fix        >= 0) { g_dma_fix_preset        = p->dma_fix;        printf("  dma_fix        -> %d\n", p->dma_fix);        }
    if (p->fastmem        >= 0) { g_fastmem_preset        = p->fastmem;        printf("  fastmem        -> %d\n", p->fastmem);        }
    if (p->bcache         >= 0) { g_bcache_preset         = p->bcache;         printf("  bcache         -> %d\n", p->bcache);         }
    if (p->fpu_pin        >= 0) { g_fpu_pin_preset        = p->fpu_pin;        printf("  fpu_pin        -> %d\n", p->fpu_pin);        }
    if (p->jit_align      >= 0) { g_jit_align_preset      = p->jit_align;      printf("  jit_align      -> %d\n", p->jit_align);      }
    if (p->sched          >= 0) { g_sched_preset          = p->sched;          printf("  sched          -> %d\n", p->sched);          }
}

// ---------------------------------------------------------------------------
// Section header matching
// ---------------------------------------------------------------------------

// Parse a section header line (s points at the first '[' or '<') and decide
// whether the section applies. Two kinds of bracket group can appear, in any
// order, any number of times, and both are filename OR-matched the same way:
//   [alias]   matches on any hardware (Wii or Wii U).
//   <alias>   matches ONLY on real Wii U hardware running vWii — the angle
//             brackets themselves are the Wii U gate, no separate condition
//             tag needed. If ANY group on the line uses <angle> brackets,
//             the whole section requires g_is_wiiu.
// The FIRST group on the line (whichever bracket style) is the canonical
// name shown in the options menu.
// want_default selects the special [default]/<default> section, which the
// Wii-U gate can still apply to (e.g. <default> for Wii-U-wide overrides)
// but which is never matched against a filename. On a filename match,
// canonical and hit (both MAX_KEYWORD_LEN) receive the first alias and the
// alias that matched.
static bool section_matches(char* s, const char* lower_name, bool want_default,
                            char* canonical, char* hit, bool* is_wiiu)
{
    bool matched    = false;
    bool have_alias = false;
    bool wiiu_only  = false;
    bool is_default = false;
    bool first      = true;
    *is_wiiu = false;
    canonical[0] = hit[0] = '\0';

    // Walk every [alias]/<alias> group on the line; stop at the first
    // thing that isn't another group (e.g. a trailing ; comment).
    char* pos = s;
    while (*pos == '[' || *pos == '<')
    {
        char close = (*pos == '[') ? ']' : '>';
        bool is_wiiu_group = (*pos == '<');
        char* end_bracket = strchr(pos, close);
        if (!end_bracket) break;
        *end_bracket = '\0';             // terminate this group
        char* kw = str_trim(pos + 1);    // content between the brackets

        if (*kw)
        {
            char alias[MAX_KEYWORD_LEN];
            strncpy(alias, kw, MAX_KEYWORD_LEN - 1);
            alias[MAX_KEYWORD_LEN - 1] = '\0';
            str_tolower_inplace(alias);

            have_alias = true;
            if (is_wiiu_group) wiiu_only = true;

            if (first)
            {
                strcpy(canonical, alias);
                first = false;
                is_default = key_eq(canonical, "default");
            }

            if (!matched && str_contains(lower_name, alias))
            {
                matched = true;
                strcpy(hit, alias);
            }
        }
        pos = end_bracket + 1;
        while (*pos && isspace((unsigned char)*pos)) pos++;
    }

    if (!have_alias) return false;

    // Reported regardless of the outcome, so the caller can label the
    // section even when the Wii U gate rejects it.
    *is_wiiu = wiiu_only;

    bool conditions_ok = !wiiu_only || g_is_wiiu;

    // [default]/<default> is special: only ever picked by the want_default
    // pass, never matched against a filename — but the Wii U gate still
    // applies to it.
    if (is_default) return want_default && conditions_ok;
    if (want_default) return false;

    return conditions_ok && matched;
}

// ---------------------------------------------------------------------------
// Streaming pass — parse and apply the first section that matches
// ---------------------------------------------------------------------------

// Re-reads the .cfg from SD and applies the first [default] section
// (want_default) or the first section with an alias matching lower_name.
// Only the matched section's key=value lines are parsed, into s_scratch.
// Returns true if a section was found and applied.
static bool stream_apply(const char* lower_name, bool want_default)
{
    FILE* f = fopen(s_cfg_path, "r");
    if (!f) return false;

    bool collecting = false;
    char line[256];

    while (fgets(line, sizeof(line), f))
    {
        char* s = str_trim(line);

        // Blank or full-line comment (;; or # or ;)
        if (!*s || *s == '#' || *s == ';') continue;

        // Section header: [keyword] or several aliases [name1][name2][name3].
        // A <angle> group is a section header too — the brackets themselves
        // are the Wii U gate — so '<' must be accepted here as well, or the
        // whole Wii U section is skipped and its key=value lines are either
        // dropped or (with no [wiiu] guard above them) absorbed by whatever
        // section was still collecting.
        if (*s == '[' || *s == '<')
        {
            if (collecting) break;   // next section starts — first match wins

            char canonical[MAX_KEYWORD_LEN], hit[MAX_KEYWORD_LEN];
            bool sec_is_wiiu = false;
            if (section_matches(s, lower_name, want_default, canonical, hit, &sec_is_wiiu))
            {
                collecting = true;
                preset_clear(&s_scratch);

                if (want_default)
                    printf("[game_presets] Applying [default]\n");
                else
                {
                    printf("[game_presets] Matched %c%s%c (via '%s')\n",
                           sec_is_wiiu ? '<' : '[', canonical,
                           sec_is_wiiu ? '>' : ']', hit);

                    // Save canonical name (first alias) for the options menu
                    strncpy(g_matched_preset_name, canonical, MAX_KEYWORD_LEN - 1);
                    g_matched_preset_name[MAX_KEYWORD_LEN - 1] = '\0';
                    g_matched_preset_is_wiiu = sec_is_wiiu;
                }
            }
            continue;
        }

        // key=value pair (only inside the matched section)
        if (!collecting) continue;

        char* eq = strchr(s, '=');
        if (!eq) continue;
        *eq = '\0';

        char* key = str_trim(s);
        char* val = str_trim(eq + 1);

        // Strip trailing inline comment from value
        strip_inline_comment(val);
        val = str_trim(val);   // re-trim after comment removal

        if (*key && *val)
            apply_kv(&s_scratch, key, val);
    }

    fclose(f);

    if (collecting)
        preset_apply_fields(&s_scratch);
    return collecting;
}

// ---------------------------------------------------------------------------
// Public: load
// ---------------------------------------------------------------------------

void game_presets_load(const char* cfg_path)
{
    g_matched_preset_name[0] = '\0';
    g_matched_preset_is_wiiu = false;

    strncpy(s_cfg_path, cfg_path, sizeof(s_cfg_path) - 1);
    s_cfg_path[sizeof(s_cfg_path) - 1] = '\0';

    // Nothing is parsed or stored here — apply() streams the file straight
    // from SD each launch. Just count the sections so the boot log still
    // shows whether the file was found and how many presets it holds.
    FILE* f = fopen(s_cfg_path, "r");
    if (!f)
    {
        printf("[game_presets] No preset file at %s — using UI defaults\n", cfg_path);
        return;
    }

    int  sections = 0;
    char line[256];
    while (fgets(line, sizeof(line), f))
    {
        char c = *str_trim(line);
        if (c == '[' || c == '<')
            sections++;
    }
    fclose(f);

    printf("[game_presets] Done — %d section(s) found\n", sections);
}

// ---------------------------------------------------------------------------
// Public: apply
// ---------------------------------------------------------------------------

void game_presets_apply(const char* filepath)
{
    g_matched_preset_name[0] = '\0';   // clear previous match
    g_matched_preset_is_wiiu = false;

    if (!s_cfg_path[0])
        return;

    // Pass 1: apply [default] first — wherever it sits in the file — so
    // every launch starts from the same baseline and a per-game match
    // below only needs to override what differs.
    stream_apply(NULL, true);

    if (!filepath || !*filepath)
        return;

    // Work on filename only (strip directory part)
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;

    // Lowercase copy for matching — aliases are lowercased as they're parsed
    char lower[512];
    strncpy(lower, filename, sizeof(lower) - 1);
    lower[sizeof(lower) - 1] = '\0';
    str_tolower_inplace(lower);

    printf("[game_presets] Trying to match: '%s'\n", lower);

    // Pass 2: first per-game section with an alias contained in the filename
    if (!stream_apply(lower, false))
        printf("[game_presets] No preset matched for: %s\n", filename);
}
