#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The stored settings and nothing else: read at boot, written through on every
 * change, clamped to the tables they index. The shell hands each one to the
 * module that acts on it, so those modules never consult the store.
 */

void settings_begin(void);

bool settings_is_display_rotated(void);
void settings_set_display_rotated(bool rotated);

bool settings_is_haptics_enabled(void);
void settings_set_haptics_enabled(bool enabled);

bool settings_is_coin_enabled(void);
void settings_set_coin_enabled(bool enabled);

uint8_t settings_theme_index(void);
void settings_set_theme_index(uint8_t index);

uint8_t settings_effect_index(void);
void settings_set_effect_index(uint8_t index);

uint8_t settings_die_index(void);
void settings_set_die_index(uint8_t index);

#ifdef __cplusplus
}
#endif
