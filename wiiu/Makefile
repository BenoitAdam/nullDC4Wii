#---------------------------------------------------------------------------------
# wiiu/Makefile - NullDC4Wii, native Wii U build (Cafe OS, wut).
#
# Mirrors wii/Makefile: same shared core, same per-directory wildcard source
# list, minus the Wii-only directories, plus wiiu/. Produces:
#
#   wiiu/nulldc4wiiu.rpx    the executable
#   wiiu/nulldc4wiiu.wuhb   the Aroma bundle (rpx + name/icon), when wuhbtool runs
#
# Needs, besides devkitPPC:
#   * wut             (dkp-pacman -S wiiu-dev), DEVKITPRO set
#   * CafeGLSL's glslcompiler, to turn wiiu/shaders/*.vert|*.frag into the .gsh
#     binaries the renderer embeds (https://github.com/Exzap/CafeGLSL releases).
#     Put it on PATH or pass CAFEGLSL=/path/to/glslcompiler.exe.
#
# Run from wiiu/ (wiiu_build.bat does that), like wii/vs_make.bat for the Wii.
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitPro")
endif

TOPDIR ?= $(CURDIR)

#---------------------------------------------------------------------------------
# Output / bundle metadata. Must come BEFORE wut_rules: it turns APP_* into
# wuhbtool options at include time.
#---------------------------------------------------------------------------------
TARGET        := nulldc4wiiu
BUILD         := build
APP_NAME      := NullDC4WiiU
APP_SHORTNAME := NullDC4WiiU
APP_AUTHOR    := BenoitAdam

# Optional .wuhb artwork (wuhbtool): used only when the files exist, so a
# fresh checkout still builds. Sizes: icon 128x128, TV 1280x720, DRC 854x480.
ifneq ($(wildcard $(TOPDIR)/../apps/apps/nulldc4wiiu/icon.png),)
APP_ICON      := $(TOPDIR)/../apps/apps/nulldc4wiiu/icon.png
endif
ifneq ($(wildcard $(TOPDIR)/../apps/apps/nulldc4wiiu/bootTvTex.png),)
APP_TV_SPLASH := $(TOPDIR)/../apps/apps/nulldc4wiiu/bootTvTex.png
endif
ifneq ($(wildcard $(TOPDIR)/../apps/apps/nulldc4wiiu/bootDrcTex.png),)
APP_DRC_SPLASH := $(TOPDIR)/../apps/apps/nulldc4wiiu/bootDrcTex.png
endif

include $(DEVKITPRO)/wut/share/wut_rules

# CafeGLSL standalone compiler (see the header).
CAFEGLSL      ?= glslcompiler
SHADERS       := pvr blit
SHADERDIR     := $(TOPDIR)/shaders

#--- Core Sources (same list as wii/Makefile) ---
SOURCES     := ../
SOURCES     += ../cl/
SOURCES     += ../config/

#--- Dreamcast Emulation Core ---
SOURCES     += ../dc/
SOURCES     += ../dc/aica/
SOURCES     += ../dc/asic/
SOURCES     += ../dc/gdrom/
SOURCES     += ../dc/maple/
SOURCES     += ../dc/mem/
SOURCES     += ../dc/pvr/
SOURCES     += ../dc/sh4/
SOURCES     += ../dc/sh4/rec_v2/       # compiled with HOST_NO_REC: interpreter only

#--- Plugins & Internal Modules ---
SOURCES     += ../plugins/
SOURCES     += ../plugs/nullExtDev/
SOURCES     += ../plugs/nullAICA/
SOURCES     += ../plugs/vbaARM/
SOURCES     += ../plugs/ImgReader/
SOURCES     += ../plugs/drkPvr/
# NOT the Wii's plugs/drkMapleDevices/ (wiiuse), plugs/libwiidrc/ and
# plugs/libsicksaxis/ (libogc USB/BT): wiiu/wiiu_input.cpp replaces them.

#--- Wii U platform layer ---
SOURCES     += ../wiiu/
# NOT ../wii/: libogc-only. The two shared files from it are pulled in by
# wrappers: wiiu/wiiu_presets.cpp (wii/main.cpp preset block) and
# wiiu/wiiu_game_presets.cpp (wii/game_presets.cpp).

#---------------------------------------------------------------------------------
# Compilation flags
#---------------------------------------------------------------------------------
CFLAGS  := -g -O2 -Wall -Wextra -ffunction-sections
CFLAGS  += -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS  += $(MACHDEP) $(INCLUDE)
CFLAGS  += -D__WIIU__ -D__WUT__ -D_WIIU -DRELEASE
CFLAGS  += -frename-registers -fno-strict-aliasing -fsingle-precision-constant
CFLAGS  += -fomit-frame-pointer -ffast-math
# No -msdata=eabi here (wii/Makefile has it): the RPX loader and wut's crt are
# not set up for r13/r2 small-data bases.

CXXFLAGS := $(CFLAGS) -fno-exceptions -fno-rtti -O3

ASFLAGS  := -g $(MACHDEP)
LDFLAGS   = -g $(MACHDEP) $(RPXSPECS) -Wl,-Map,$(notdir $*.map)

LIBS     := -lwhb -lgfd -lwut -lm
LIBDIRS  := $(PORTLIBS) $(WUT_ROOT)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

# Same exclusion as wii/Makefile: old nullAICA mem.cpp, superseded by aica_mem.cpp
CPPFILES    := $(filter-out mem.cpp,$(CPPFILES))
# The Wii's GX renderer: gx2Rend.cpp is the Wii U one (same directory)
CPPFILES    := $(filter-out gxRend.cpp,$(CPPFILES))

ifeq ($(strip $(CPPFILES)),)
	export LD := $(CC)
else
	export LD := $(CXX)
endif

export SHADERFILES := $(addsuffix .gsh,$(SHADERS))
export OFILES_BIN  := $(addsuffix .o,$(SHADERFILES))
export OFILES_SRC  := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.S=.o)
export OFILES      := $(OFILES_BIN) $(OFILES_SRC)
export HFILES_BIN  := $(addsuffix .h,$(subst .,_,$(SHADERFILES)))

# wiiu/compat/ FIRST: its gccore.h / ogc/*.h shadow libogc's names for the
# shared core (see wiiu/compat/gccore.h). Then the repo root, like the Wii.
export INCLUDE  := -I$(CURDIR)/compat \
                   -I$(CURDIR)/.. \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo cleaning ...
	@rm -fr $(BUILD) $(TARGET).wuhb $(TARGET).rpx $(TARGET).elf

#---------------------------------------------------------------------------------
else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all : $(OUTPUT).wuhb

$(OUTPUT).wuhb : $(OUTPUT).rpx
$(OUTPUT).rpx  : $(OUTPUT).elf
$(OUTPUT).elf  : $(OFILES)

# gx2Rend.cpp includes the generated pvr_gsh.h / blit_gsh.h
$(OFILES_SRC) : $(HFILES_BIN)

#---------------------------------------------------------------------------------
# Shaders: GLSL -> .gsh (CafeGLSL, register uniform mode, one vertex/pixel pair
# per file) -> object + header (bin2o, symbols pvr_gsh / blit_gsh)
#---------------------------------------------------------------------------------
%.gsh : $(SHADERDIR)/%.vert $(SHADERDIR)/%.frag
	@echo shader $(notdir $@)
	$(SILENTCMD)$(CAFEGLSL) -vs $(SHADERDIR)/$*.vert -ps $(SHADERDIR)/$*.frag --uniform-mode register -o $@

%.gsh.o %_gsh.h : %.gsh
	@echo $(notdir $<)
	@$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
