#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Port: what the device remembers across power cycles. hardware/settings.cpp
 * keeps it in NVS; the host adapters have no store. The shell reads the
 * settings once at boot and hands each to the module that acts on it, so no
 * module consults the store, and the die is the one thing written at runtime.
 */

typedef struct {
    bool display_rotated;
    bool haptics_enabled;
    bool coin_enabled;
    uint8_t effect_index; // an index into EFFECTS
    uint8_t die_index;    // an index into DICE
} settings_t;

/**
 * Opens the store and lays every value it holds over settings, so the caller
 * fills in the defaults first and a value the store lacks keeps them. Stored
 * indexes are handed over as they are; the module that indexes a table with
 * one decides what a value past the table means.
 */
void settings_begin(settings_t *settings);

// Written when a choice settles; a write of the value already stored is skipped.
void settings_set_die_index(uint8_t index);

/**
 * Counts boots that never reached the loop. Noted at the top of setup and
 * cleared once the loop has run a while, so a firmware that dies before its
 * USB port is up can still be caught and reflashed.
 */
uint8_t settings_note_boot_attempt(void);
void settings_clear_boot_attempts(void);

#ifdef __cplusplus
}
#endif
