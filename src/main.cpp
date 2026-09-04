/*
 * The Arduino shell: brings the hardware up, then every loop reads the clock
 * and the inputs, steps the mode machine, and pushes whatever it drew to the
 * panel. Every rule of the interaction lives in mode.c, which the host can
 * build and test.
 */

#include <Arduino.h>
#include <Wire.h>
#include <string.h>

#include "config.h"
#include "dice.h"
#include "render/canvas.h"
#include "hardware/drive.h"
#include "hardware/encoder.h"
#include "frame.h"
#include "ports/haptics.h"
#include "knob_pins.h"
#include "mode.h"
#include "oracle.h"
#include "hardware/panel.h"
#include "safe_mode.h"
#include "ports/settings.h"
#include "render/theme.h"
#include "hardware/touch_cst816.h"
#include "user_files.h"

static bool ready = false;
static bool safe_mode_announced = false;
static settings_t settings;
static mode_config_t config;

// The two settings the shell applies itself, on every read of the inputs and the light.
static bool reverse_knob = false;
static uint8_t brightness = 100;

// A refused file names itself in STATUS.txt on the drive and on the port.
static void report_user_files(user_files_result_t result) {
    if (result == USER_FILES_REFUSED) {
        Serial.printf("user files: %s", user_files_status());
    }
}

// The machine sleeps after this many milliseconds; a file asking for none never sleeps.
static uint32_t idle_ms_of(uint32_t sleep_after_seconds) {
    return sleep_after_seconds == 0 ? UINT32_MAX : sleep_after_seconds * 1000u;
}

// The light the machine asks for, under the ceiling the settings put on it.
static uint8_t lit(uint8_t level) {
    if (level == 0) {
        return 0;
    }

    const unsigned scaled = ((unsigned)level * brightness) / 100u;
    return (uint8_t)(scaled < 1 ? 1 : scaled);
}

/*
 * Hands each setting a turn applied to the module that acts on it. The idle
 * timeout reaches the machine at the restart that follows the turn.
 */
static void apply_settings(void) {
    const config_settings_t *in_use = user_files_settings();
    panel_set_rotated(in_use->display_rotated);
    haptics_set_enabled(in_use->haptics);
    reverse_knob = in_use->reverse_knob;
    brightness = in_use->brightness;
    config.idle_ms = idle_ms_of(in_use->sleep_after);
    mode_set_idle_ms(config.idle_ms);
}

void setup() {
    Serial.begin(115200);

    // The default, for a store that has never been written.
    strncpy(settings.die_name, "ORACLE", sizeof(settings.die_name));
    settings_begin(&settings);

    /*
     * The USB stack is already up: the core starts it before setup() for the
     * serial console, so safe mode has its port by doing nothing. A boot that
     * dies before the loop is counted; the third in a row stays here.
     */
    safe_mode_begin(millis());
    if (safe_mode_is_active()) {
        return;
    }

    drive_begin();

    if (!panel_begin()) {
        return;
    }

    if (!canvas_begin()) {
        Serial.println("canvas: no PSRAM for the framebuffer");
        return;
    }

    report_user_files(user_files_begin());
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    haptics_begin();
    apply_settings();

    /*
     * The panel keeps its RAM through a reset, so the first frame goes up
     * before the backlight does.
     */
    canvas_fill(theme_active()->colors.background);
    panel_present(canvas_pixels());
    panel_set_backlight(lit(255));

    touch_begin();
    encoder_begin();
    oracle_begin();

    config.die = dice_index_of(settings.die_name);
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

    /*
     * A turn the computer ended is followed by the boot sequence, whatever
     * was refused: it is the one acknowledgement the reader gets without
     * plugging the drive back in.
     */
    const user_files_result_t files = user_files_step();
    if (files != USER_FILES_QUIET) {
        report_user_files(files);
        apply_settings();
    }

    const int32_t detents = encoder_take_detents();
    const mode_input_t input = {
        reverse_knob ? -detents : detents,
        touch_read().pressed,
        files != USER_FILES_QUIET
    };

    const frame_rect_t rows = mode_step(now, input);
    if (rows.height > 0) {
        panel_present_rect(canvas_pixels(), rows.top, rows.height, rows.left, rows.width);
    }

    panel_set_backlight(lit(mode_get_backlight()));

    /*
     * Every pass polls the touch controller over I2C, so this caps the poll
     * rate at a few hundred a second and yields the core to the encoder's
     * sampling timer. Frames are paced inside the machine.
     */
    delay(2);
}
