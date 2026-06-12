# Native ARM7DI self-test for the cached interpreter.
#
# Builds the REAL ../arm7.cpp and ../arm7_cached.cpp (no copies) against the
# minimal leaf header stubs in this directory, plus a flat little-endian memory
# backend in nt_main.cpp, and runs the embedded ARM7DI conformance tests on the
# host. This validates the cached path's logic without a Wii toolchain.
#
# Usage:
#   make            # build + nothing else
#   make run        # build and run the self-test
#   make ref        # build the REFERENCE (non-cached) interpreter for comparison
#   make run-ref    # build and run the reference interpreter
#   make clean
#
# Requires the msys2 / Cygwin g++ (gcc 15). The recipes put its bin on PATH so
# cc1plus can find its DLLs.

CXX     := C:/msys64/usr/bin/g++.exe
DLLPATH := /c/msys64/usr/bin
SHELL   := /bin/bash

# Stub leaf headers live here; real vbaARM headers come from ../.
# nativetest dir (.) is searched first for "" includes via -iquote, then ..
CXXFLAGS := -std=c++17 -O1 -g -fno-strict-aliasing -Wall -Wno-unused -iquote . -I ..

SRC := ../arm7.cpp ../arm7_cached.cpp nt_main.cpp nt_stub.cpp

.PHONY: all run ref run-ref clean

all: nt_cached.exe

# Cached interpreter (ARM7_USE_CACHED).
nt_cached.exe: $(SRC) types.h plugin_types.h assert.h ../arm7_cached_gen.inc
	PATH="$(DLLPATH):$$PATH" $(CXX) $(CXXFLAGS) -DARM7_USE_CACHED $(SRC) -o $@

run: nt_cached.exe
	PATH="$(DLLPATH):$$PATH" ./nt_cached.exe

# Reference interpreter (inline arm-new.h path) for A/B comparison.
nt_ref.exe: $(SRC) types.h plugin_types.h assert.h
	PATH="$(DLLPATH):$$PATH" $(CXX) $(CXXFLAGS) $(SRC) -o $@

run-ref: nt_ref.exe
	PATH="$(DLLPATH):$$PATH" ./nt_ref.exe

clean:
	rm -f nt_cached.exe nt_ref.exe nt_trace.exe *.o
