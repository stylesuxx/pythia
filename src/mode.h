#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// The mode machine: every rule of the interaction, from the boot handover to
// the roll-on-touch shortcut, driven by the clock and the two inputs. Each
// step draws whatever the panel should now show into the canvas and reports
// how much of it changed; the caller pushes that to the panel. Haptics and the
// persisted die go through their own modules, which the host stands in for.

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

// How much of the canvas a step changed, and so how much the panel needs.
typedef enum {
    PRESENT_NONE,
    PRESENT_WHOLE,
    PRESENT_STAGE, // the reveal's band only; see REVEAL_STAGE_TOP
} mode_present_t;

// Starts the boot sequence, armed afterwards on the given die.
void mode_begin(uint32_t now, uint8_t die);

// Advances the machine to now. Call every loop; frames are paced inside.
mode_present_t mode_step(uint32_t now, mode_input_t input);

ui_mode_t mode_current(void);
uint8_t mode_selected_die(void);

#ifdef __cplusplus
}
#endif
