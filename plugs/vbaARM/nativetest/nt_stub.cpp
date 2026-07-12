// Native-test stubs for platform functions the cached interpreter may reference
// when debug features are compiled in (e.g. ARM7_PROFILE_BLOCKS -> os_GetSeconds).
// Returning a monotonically increasing value keeps the >=1s profiler trigger
// from firing constantly during tests.
double os_GetSeconds() { return 0.0; }
