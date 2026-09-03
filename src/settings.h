#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dice.h"

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
    char die_name[DIE_NAME_CAPACITY]; // resolved against the layout in use at boot
} settings_t;

/**
 * Opens the store and lays every value it holds over settings, so the caller
 * fills in the defaults first and a value the store lacks keeps them. The die
 * is a name, so a layout that reorders the table still comes back to it.
 */
void settings_begin(settings_t *settings);

// Written when a choice settles; a write of the name already stored is skipped.
void settings_set_die_name(const char *name);

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
