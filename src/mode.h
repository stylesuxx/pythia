#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The mode machine: every rule of the interaction, from the boot handover to
 * the roll-on-touch shortcut, driven by the clock and the two inputs. Each
 * step draws whatever the panel should now show into the canvas and returns
 * the rows that changed; the caller pushes those to the panel. Haptics and
 * the persisted die go through their own modules, which the host stands in
 * for.
 */

typedef enum {
    MODE_BOOT,     // power-on sequence, inputs drained and ignored
    MODE_CHOOSING, // die name and the selection ring, encoder live
    MODE_ARMED,    // rim caption only, waiting to be consulted
    MODE_RESULT,   // a revealed roll
} ui_mode_t;

// What happened since the last step.
typedef struct {
    int32_t detents; // clicks turned, positive clockwise
    bool touched;    // the touch controller reports a contact
} mode_input_t;

typedef struct {
    uint8_t die;       // armed after boot
    uint32_t idle_ms;  // input-free time before the screen sleeps; see power.h
    bool coin_enabled; // D2 thrown as a coin rather than printed
    uint8_t effect_index; // how a numeric result arrives; an index into EFFECTS
} mode_config_t;

// Starts the boot sequence, armed afterwards on the configured die.
void mode_begin(uint32_t now, const mode_config_t *config);

/**
 * Advances the machine to now and returns the canvas rows to push, none when
 * nothing changed.
 *
 * NOTE: Call every loop; frames are paced inside.
 */
frame_rect_t mode_step(uint32_t now, mode_input_t input);

ui_mode_t mode_get_current(void);
uint8_t mode_get_selected_die(void);
uint8_t mode_get_backlight(void);

#ifdef __cplusplus
}
#endif
