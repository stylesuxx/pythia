#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// TS2200 library A effect numbers, chosen so the three events feel distinct.
#define HAPTIC_DETENT 24   // Sharp Tick 1, light enough to repeat while turning
#define HAPTIC_ANSWER 4    // Sharp Click, the first beat landing
#define HAPTIC_MODIFIER 10 // Double Click, the second beat arriving

// Requires Wire.begin() to have run first; the DRV2605 shares the touch bus.
void haptics_begin(void);

void haptics_play(uint8_t effect);

#ifdef __cplusplus
}
#endif
