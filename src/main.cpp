/*
 * The Arduino shell: brings the hardware up, then every loop reads the clock
 * and the inputs, steps the mode machine, and pushes whatever it drew to the
 * panel. Every rule of the interaction lives in mode.c, which the host can
 * build and test.
 */

#include <Arduino.h>
#include <USB.h>
#include <Wire.h>

#include "config.h"
#include "render/canvas.h"
#include "hardware/drive.h"
#include "hardware/encoder.h"
#include "frame.h"
#include "haptics.h"
#include "knob_pins.h"
#include "mode.h"
#include "oracle.h"
#include "hardware/panel.h"
#include "settings.h"
#include "render/theme.h"
#include "hardware/touch_cst816.h"
#include "user_files.h"

/**
 * Idle time before the screen goes dark. Overridable from the build flags, so
 * a build that sleeps quickly is a flag rather than an edit:
 * PLATFORMIO_BUILD_FLAGS=-DIDLE_SLEEP_MS=10000
 */
#ifndef IDLE_SLEEP_MS
#define IDLE_SLEEP_MS 120000
#endif

/*
 * Boots that die before the loop runs are counted in NVS. After this many in
 * a row the next boot brings up the USB port and nothing else, so the unit can
 * always be reflashed over the cable. The count clears once a boot has run
 * this long.
 */
#define SAFE_MODE_AFTER_ATTEMPTS 3
#define BOOT_SETTLED_MS 8000

static bool ready = false;
static bool safe_mode = false;
static bool boot_settled = false;
static bool safe_mode_announced = false;
static mode_config_t config;

/*
 * Takes the drive from the host, applies the files on it and hands it back.
 * A refused file is reported on the port and in STATUS.txt on the drive.
 */
static void apply_user_files(void) {
    if (!drive_open()) {
        return;
    }

    char message[CONFIG_ERROR_CAPACITY];
    if (!user_files_apply(message, sizeof(message))) {
        Serial.printf("user files: %s\n", message);
    }

    drive_note(message);

    drive_close();
}

void setup() {
    Serial.begin(115200);
    settings_begin();

    if (settings_note_boot_attempt() >= SAFE_MODE_AFTER_ATTEMPTS) {
        settings_clear_boot_attempts();
        safe_mode = true;
        USB.begin();
        return;
    }

    // The drive registers its interface first; the stack starts once.
    USB.manufacturerName("Delphi Systems");
    USB.productName("PYTHIA");
    drive_begin();
    USB.begin();

    if (!panel_begin()) {
        return;
    }

    if (!canvas_begin()) {
        Serial.println("canvas: no PSRAM for the framebuffer");
        return;
    }

    theme_select(settings_theme_index());
    apply_user_files();
    panel_set_rotated(settings_is_display_rotated());

    /*
     * The panel keeps its RAM through a reset, so the first frame goes up
     * before the backlight does.
     */
    canvas_fill(theme_active()->colors.background);
    panel_present(canvas_pixels());
    panel_set_backlight(255);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    haptics_begin();
    haptics_set_enabled(settings_is_haptics_enabled());

    touch_begin();
    encoder_begin();
    oracle_begin();

    config = {settings_die_index(), IDLE_SLEEP_MS, settings_is_coin_enabled(),
              settings_effect_index()};
    mode_begin(millis(), &config);
    ready = true;
}

void loop() {
    if (safe_mode) {
        if (Serial && !safe_mode_announced) {
            safe_mode_announced = true;
            Serial.printf("pythia: safe mode after %d boots that never came up; reflash over USB\n",
                          SAFE_MODE_AFTER_ATTEMPTS);
        }

        delay(100);
        return;
    }

    if (!ready) {
        delay(100);
        return;
    }

    const uint32_t now = millis();
    if (!boot_settled && now > BOOT_SETTLED_MS) {
        boot_settled = true;
        settings_clear_boot_attempts();
    }

    // The host's turn with the drive ended: read what it left and start over
    // on it, self-test and all.
    if (drive_take_change()) {
        apply_user_files();
        mode_begin(now, &config);
    }

    const mode_input_t input = {encoder_take_detents(), touch_read().pressed};

    const frame_rect_t rows = mode_step(now, input);
    if (rows.height > 0) {
        panel_present_rect(canvas_pixels(), rows.top, rows.height, rows.left, rows.width);
    }

    panel_set_backlight(mode_get_backlight());

    /*
     * Every pass polls the touch controller over I2C, so this caps the poll
     * rate at a few hundred a second and yields the core to the encoder's
     * sampling timer. Frames are paced inside the machine.
     */
    delay(2);
}
