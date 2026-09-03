/*
 * The Arduino shell: brings the hardware up, then every loop reads the clock
 * and the inputs, steps the mode machine, and pushes whatever it drew to the
 * panel. Every rule of the interaction lives in mode.c, which the host can
 * build and test.
 */

#include <Arduino.h>
#include <USB.h>
#include <Wire.h>
#include <string.h>

#include "config.h"
#include "dice.h"
#include "render/canvas.h"
#include "hardware/drive.h"
#include "hardware/encoder.h"
#include "frame.h"
#include "haptics.h"
#include "knob_pins.h"
#include "mode.h"
#include "oracle.h"
#include "hardware/panel.h"
#include "safe_mode.h"
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

static bool ready = false;
static bool safe_mode_announced = false;
static settings_t settings;
static mode_config_t config;

/*
 * Takes the drive from the host, applies the files on it and hands it back.
 * A refused file is reported on the port and in STATUS.txt on the drive.
 */
static void apply_user_files(void) {
    if (!drive_open()) {
        return;
    }

    char message[USER_FILES_MESSAGE_CAPACITY];
    if (!user_files_apply(message, sizeof(message))) {
        Serial.printf("user files: %s\n", message);
    }

    drive_note(message);

    drive_close();
}

void setup() {
    Serial.begin(115200);

    // The defaults, for a store that has never been written.
    settings.display_rotated = true;
    settings.haptics_enabled = true;
    settings.coin_enabled = true;
    strncpy(settings.die_name, "ORACLE", sizeof(settings.die_name));
    settings_begin(&settings);

    // A boot that dies before the loop is counted; the third in a row stays here.
    safe_mode_begin(millis());
    if (safe_mode_is_active()) {
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

    apply_user_files();
    panel_set_rotated(settings.display_rotated);

    /*
     * The panel keeps its RAM through a reset, so the first frame goes up
     * before the backlight does.
     */
    canvas_fill(theme_active()->colors.background);
    panel_present(canvas_pixels());
    panel_set_backlight(255);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);

    haptics_begin();
    haptics_set_enabled(settings.haptics_enabled);

    touch_begin();
    encoder_begin();
    oracle_begin();

    config.die = dice_index_of(settings.die_name);
    config.idle_ms = IDLE_SLEEP_MS;
    config.coin_enabled = settings.coin_enabled;
    mode_begin(millis(), &config);
    ready = true;
}

void loop() {
    if (safe_mode_is_active()) {
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
    safe_mode_step(now);

    // The host's turn with the drive ended: read what it left, and the machine
    // starts over on it, self-test and all.
    bool restart = false;
    if (drive_take_change()) {
        apply_user_files();
        restart = true;
    }

    const mode_input_t input = {encoder_take_detents(), touch_read().pressed, restart};

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
