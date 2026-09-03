#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The power-on sequence. Runs once, ahead of the die list, and uses
 * the active theme's palette with its own monospace faces.
 * The strings the sequence draws, so a test can hold the boot faces to them.
 */
extern const char BOOT_WORDMARK[];
extern const char BOOT_MANUFACTURER[];

// Glyphs a wordmark position cycles through before it settles.
extern const char BOOT_SCRAMBLE_CHARACTERS[];

void boot_begin(uint32_t now);
void boot_draw(uint32_t now);
bool boot_is_running(uint32_t now);

/**
 * Drives the haptic beats. Call every loop, not once per rendered frame, so a
 * dropped frame does not move a tick.
 */
void boot_tick(uint32_t now);

#ifdef __cplusplus
}
#endif
