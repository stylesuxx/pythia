#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

// The mode machine: every rule of the interaction, from the boot handover to
// the roll-on-touch shortcut, driven by the clock and the two inputs. Each
// step draws whatever the panel should now show into the canvas and returns
// the rows that changed; the caller pushes those to the panel. Haptics and
// the persisted die go through their own modules, which the host stands in
// for.

typedef enum {
    MODE_BOOT,     // power-on sequence, inputs drained and ignored
    MODE_CHOOSING, // die name and the selection ring, encoder live
    MODE_ARMED,    // rim caption only, waiting to be consulted
    MODE_RESULT,   // a revealed roll
} ui_mode_t;

// What happened since the last step.
typedef struct {
    int32_t detents; // clicks turned, positive clockwise
    bool tap;        // a touch began
} mode_input_t;

// Starts the boot sequence, armed afterwards on the given die.
void mode_begin(uint32_t now, uint8_t die);

// Advances the machine to now and returns the canvas rows to push, none when
// nothing changed. Call every loop; frames are paced inside.
frame_rows_t mode_step(uint32_t now, mode_input_t input);

ui_mode_t mode_current(void);
uint8_t mode_selected_die(void);

#ifdef __cplusplus
}
#endif
