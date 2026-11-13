// wiiu/wiiu_input.h
// Wii U input (wiiu/wiiu_input.cpp). The maple-facing API (kcode[],
// UpdateInputState()...) is declared by its consumer, dc/maple/maple_cfg.cpp.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// VPAD + KPAD (Wii remotes, Classic, Pro Controller). Idempotent; main() calls
// it before the menu reads the pads, the maple side calls it again harmlessly.
void wiiu_input_init(void);

#ifdef __cplusplus
}
#endif
