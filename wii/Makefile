#---------------------------------------------------------------------------------
# Environment Validation & Setup)
#---------------------------------------------------------------------------------
.SUFFIXES:

ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

include $(DEVKITPPC)/wii_rules

#---------------------------------------------------------------------------------
# TARGET is the name of the output
# BUILD is the directory where object files & intermediate files will be placed
# SOURCES is a list of directories containing source code
# INCLUDES is a list of directories containing extra header files
# DATA is a list of directories containing binary data files
#---------------------------------------------------------------------------------
TARGET      := boot
BUILD       := build
INCLUDES    := ../
DATA        := data

#--- Core Sources ---
SOURCES     := ../
SOURCES     += ../cl/
SOURCES     += ../config/
SOURCES     += ../debug_mem.o # needed ?
SOURCES     += ../profiler/ # needed ?
SOURCES     += ../serial_ipc/ # needed ?

#--- Dreamcast Emulation Core ---
SOURCES     += ../dc/
SOURCES     += ../dc/aica/
SOURCES     += ../dc/asic/
SOURCES     += ../dc/gdrom/
SOURCES     += ../dc/maple/
SOURCES     += ../dc/mem/
SOURCES     += ../dc/pvr/
SOURCES     += ../dc/sh4/
SOURCES     += ../dc/sh4/rec_v2/

#--- Plugins & Internal Modules ---
SOURCES     += ../plugins/
SOURCES     += ../plugs/nullExtDev/
SOURCES     += ../plugs/drkMapleDevices/
# SOURCES     += ../plugs/EmptyAICA/ // old Empty AICA (till alpha0.25) better performance on SEGA Tetris
SOURCES     += ../plugs/nullAICA/
SOURCES     += ../plugs/vbaARM/
SOURCES     += ../plugs/ImgReader/
SOURCES     += ../plugs/gui/
SOURCES     += ../plugs/drkPvr/
SOURCES     += ../plugs/libwiidrc/ # Wii U GamePad (DRC) support in vWii mode
SOURCES     += ../plugs/libsicksaxis/libsicksaxis/ # Sixaxis/DualShock3 (USB) support

#--- Wii Specific & GUI ---
SOURCES     += ../wii/
SOURCES     += ../wii/dc/sh4/rec_v2/
SOURCES     += ../gui/

#---------------------------------------------------------------------------------
# Compilation Flags (CFLAGS)
#---------------------------------------------------------------------------------

# Base Flags
CFLAGS  := -save-temps -g -O2 -Wall -Wextra
# -Wextra is loud on this codebase: mute the two noisiest categories
# (stubbed plugin callbacks, partial GX/struct literals) so real warnings show
CFLAGS  += -Wno-unused-parameter -Wno-missing-field-initializers
CFLAGS  += $(MACHDEP) $(INCLUDE)
CFLAGS  += -D_WII -DRELEASE

# Optimization & Code Gen
CFLAGS  += -frename-registers
CFLAGS  += -fno-strict-aliasing
CFLAGS  += -fsingle-precision-constant
CFLAGS  += -fomit-frame-pointer
CFLAGS  += -ffast-math

# Small-data addressing through r13/r2 (PowerPC EABI).
#
# devkitPPC defaults to -msdata=data, which still PLACES small globals in
# .sdata/.sbss but addresses them with a full 32-bit `lis sym@ha` + `lwz
# sym@l(rX)` pair. -msdata=eabi keeps the same placement and reaches them in
# ONE instruction, r13-relative (and small read-only data r2-relative through
# .sdata2). Measured instruction counts, same flags otherwise:
#
#   gxRend.cpp        40866 -> 39171  (-4.1%), 2094 `lis` -> 943
#   sh4_interpreter   -12.7%   pvr_if -7.1%   _vmem -5.2%   sgc_if -1.1%
#   TA vertex entry path (once per store-queue block): 35 -> 27 instructions
#
# This does NOT touch code the SH4/ARM7 recompilers emit, only compiled C.
#
# Safe here, verified rather than assumed:
#   * libogc is ALREADY built this way -- common_crt0.o loads r2=_SDA2_BASE_
#     and r13=_SDA_BASE_ at startup, and libogc's objects carry
#     R_PPC_EMB_SDA21 relocations in this very binary.
#   * The linker defines both bases (boot.map: _SDA_BASE_ = .sdata + 0x8000,
#     _SDA2_BASE_ = .sdata2 + 0x8000).
#   * Neither recompiler ever writes r2 or r13. The SH4 JIT pins SH4 r0-r15 to
#     r14..r28 with the context in r30; the ARM7 JIT uses r25..r31. ngen_mainloop
#     only saves and restores r13 as part of the 19 preserved GPRs. The fastmem
#     DSI glue in wii/wii_fastmem.cpp already relies on this and says so.
#   * No inline asm in the tree touches either register.
#
# The one failure mode is LOUD and happens at LINK time, never at runtime:
# r13 reaches +/-32 KB, so .sdata + .sbss must stay under 64 KB or ld reports
# "relocation truncated to fit: R_PPC_EMB_SDA21". Current usage is 3.3 KB
# (704 B .sdata + 2624 B .sbss), so there is ~20x headroom. If it ever does
# overflow, lower the threshold (-G 4, or -G 0 to opt out per build) rather
# than removing this line.
CFLAGS  += -msdata=eabi -G 8

# C++ Specific Flags
CXXFLAGS := $(CFLAGS)
CXXFLAGS += -fno-exceptions
CXXFLAGS += -fno-rtti

# The optimization flags control how aggressively GCC optimizes your code: (choose one)
# !!! Remember to use vs_clean before vs_make if you change optimization flag !!!
# CXXFLAGS += -g -O0  		# for debugging on dolphin using addr2line
# CXXFLAGS += -O1  				# Basic optimization (Safe Default)
# CXXFLAGS += -O2         # Baseline Safe Default (Industry Standard)
CXXFLAGS += -O3           # Maximum & optimum optimization : that goes fast !!
# CXXFLAGS += -Ofast      # Probably will cause bugs and freeze after 15 minutes of game

# C++ Optimization Flags (Claude AI) (only enable for release builds, as they can make debugging difficult)

# CXXFLAGS += -fno-strict-aliasing # Prevents type-punning bugs (with O3)
# CXXFLAGS += -fwrapv              # Defines integer overflow behavior (With O3)
CXXFLAGS += -fomit-frame-pointer # Free up register
CXXFLAGS += -funroll-loops       # Unroll instruction decode
CXXFLAGS += -finline-functions   # Inline sh4op handlers

# C++ Optional aggressive optimization (Claude AI)
CXXFLAGS += -ffast-math          # If no FPU edge cases needed
CXXFLAGS += -fno-exceptions      # Reduce code size

# Experimental: enable SHIL dead-write elimination in the SH4 recompiler
# (dc/sh4/rec_v2/shil.cpp). Off upstream; remove this line to revert.
# CXXFLAGS += -DSHIL_ENABLE_DCE

# Linker Flags
LDFLAGS  := -g $(MACHDEP) -Wl,-Map,$(TARGET).map

# Enable link-time optimization for cross-module inlining (pvrLock optimization)
# (compilation didnt worked but keeping for now for info)
# CFLAGS += -flto
# LDFLAGS += -flto
# Optional: More aggressive optimization
# CFLAGS += -O3 -fomit-frame-pointer
#---------------------------------------------------------------------------------
# Libraries
#---------------------------------------------------------------------------------
LIBS    := -lwiiuse -lbte -lfat -lmad -lasnd -logc -lm

#---------------------------------------------------------------------------------
# list of directories containing libraries, this must be the top level containing
# include and lib
#---------------------------------------------------------------------------------
LIBDIRS	:=

# Build Logic (Avoid editing below this line)
#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT   := $(CURDIR)/$(TARGET)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                   $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

#---------------------------------------------------------------------------------
# automatically build a list of object files for our project
#---------------------------------------------------------------------------------
CFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES    := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
sFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
SFILES      := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))
BINFILES    := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

# Exclude old nullAICA mem.cpp — superseded by aica_mem.cpp
CPPFILES    := $(filter-out mem.cpp,$(CPPFILES))

#---------------------------------------------------------------------------------
# use CXX for linking C++ projects, CC for standard C
#---------------------------------------------------------------------------------
ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export OFILES   := $(addsuffix .o,$(BINFILES)) \
                   $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) \
                   $(sFILES:.s=.o) $(SFILES:.S=.o)

#---------------------------------------------------------------------------------
# build a list of include paths
#---------------------------------------------------------------------------------
export INCLUDE  := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                   $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                   -I$(CURDIR)/$(BUILD) \
                   -I$(LIBOGC_INC)

#---------------------------------------------------------------------------------
# build a list of library paths
#---------------------------------------------------------------------------------
export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
                   -L$(LIBOGC_LIB)

.PHONY: $(BUILD) clean run

#---------------------------------------------------------------------------------
$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

#---------------------------------------------------------------------------------
clean:
	@echo cleaning ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).dol $(OFILES)
#---------------------------------------------------------------------------------
run:
	wiiload $(OUTPUT).dol

#---------------------------------------------------------------------------------
else

DEPENDS := $(OFILES:.o=.d)

#---------------------------------------------------------------------------------
# main targets
#---------------------------------------------------------------------------------
$(OUTPUT).dol: $(OUTPUT).elf
$(OUTPUT).elf: $(OFILES)

#---------------------------------------------------------------------------------
# This rule links in binary data with the .mp3 extension
#---------------------------------------------------------------------------------
%.mp3.o	%_mp3.h :	%.mp3
#---------------------------------------------------------------------------------
	@echo $(notdir $<)
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
