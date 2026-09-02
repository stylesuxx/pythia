// The Arduino shell: brings the hardware up, then every loop reads the clock
// and the inputs, steps the mode machine, and pushes whatever it drew to the
// panel. Every rule of the interaction lives in mode.c, which the host can
// build and test.

#include <Arduino.h>
#include <Wire.h>

#include "canvas.h"
#include "encoder.h"
#include "frame.h"
#include "haptics.h"
#include "knob_pins.h"
#include "mode.h"
#include "oracle.h"
#include "panel.h"
#include "settings.h"
#include "theme.h"
#include "touch_cst816.h"

#define TOUCH_DEBOUNCE_MS 250

// Idle time before the screen goes dark. Overridable from the build flags, so
// a build that sleeps quickly is a flag rather than an edit:
// PLATFORMIO_BUILD_FLAGS=-DIDLE_SLEEP_MS=10000
#ifndef IDLE_SLEEP_MS
#define IDLE_SLEEP_MS 120000
#endif

static bool ready = false;
static uint32_t last_touch_ms = 0;
static bool was_touched = false;

static bool touch_began(uint32_t now) {
    uint16_t x = 0;
    uint16_t y = 0;
    const bool pressed = is_touch_pressed(x, y);
    const bool began = pressed && !was_touched && (now - last_touch_ms) > TOUCH_DEBOUNCE_MS;

    was_touched = pressed;
    if (began) {
        last_touch_ms = now;
    }

    return began;
}

void setup() {
    Serial.begin(115200);

    if (!panel_begin()) {
        return;
    }

    if (!canvas_begin()) {
        Serial.println("canvas: no PSRAM for the framebuffer");
        return;
    }

    settings_begin();

    // The panel keeps its RAM through a reset, so the first frame goes up
    // before the backlight does.
    canvas_fill(theme_active()->background);
    panel_present(canvas_pixels());
    panel_set_backlight(255);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    touch_init();
    haptics_begin();
    encoder_begin();
    oracle_begin();

    mode_begin(millis(), settings_die_index(), IDLE_SLEEP_MS);
    ready = true;
}

void loop() {
    if (!ready) {
        delay(100);
        return;
    }

    const uint32_t now = millis();
    const mode_input_t input = {encoder_take_detents(), touch_began(now)};

    const frame_rect_t rows = mode_step(now, input);
    if (rows.height > 0) {
        panel_present_rect(canvas_pixels(), rows.top, rows.height, rows.left, rows.width);
    }

    panel_set_backlight(mode_backlight());

    // Every pass polls the touch controller over I2C, so this caps the poll
    // rate at a few hundred a second and yields the core to the encoder's
    // sampling timer. Frames are paced inside the machine.
    delay(2);
}
