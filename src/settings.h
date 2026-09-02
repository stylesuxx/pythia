#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads the stored settings, falling back to the defaults on first boot. Every
// setter writes through immediately, so a power cut cannot lose a change.
void settings_begin(void);

// The knob can be held either way up. Applied when the frame is pushed, so
// nothing above the panel layer has to know.
bool settings_is_display_rotated(void);
void settings_set_display_rotated(bool rotated);

bool settings_is_haptics_enabled(void);
void settings_set_haptics_enabled(bool enabled);

uint8_t settings_theme_index(void);
void settings_set_theme_index(uint8_t index);

// Index into DICE of the die in use, so a power cycle comes back to it. Falls
// back to the oracle when nothing is stored or the stored index is out of
// range.
uint8_t settings_die_index(void);
void settings_set_die_index(uint8_t index);

#ifdef __cplusplus
}
#endif
