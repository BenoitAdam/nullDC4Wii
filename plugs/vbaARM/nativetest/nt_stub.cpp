// Native-test stubs for platform functions the cached interpreter may reference
// when debug features are compiled in (e.g. ARM7_PROFILE_BLOCKS -> os_GetSeconds).
// Returning a monotonically increasing value keeps the >=1s profiler trigger
// from firing constantly during tests.
double os_GetSeconds() { return 0.0; }

// arm7.cpp references these; the real ones live in arm_mem.cpp and wii/main.cpp,
// neither of which this harness links. ARM7_RESET_FIX on, as in the Wii build.
extern "C" int get_arm7_reset_fix_preset() { return 1; }
void arm_reset_latch() {}
bool arm_in_slice = false;
bool arm_race_pending = false;
