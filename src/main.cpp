#include <Arduino.h>
#include <Wire.h>
#include <math.h>

#include "boot.h"
#include "canvas.h"
#include "encoder.h"
#include "haptics.h"
#include "knob_pins.h"
#include "menu.h"
#include "oracle.h"
#include "panel.h"
#include "reveal.h"
#include "settings.h"
#include "theme.h"
#include "touch_cst816.h"

// The knob has no button, so stillness is what confirms a choice.
#define SELECTION_IDLE_MS 1000
#define CHOICE_FADE_MS 180
#define STAGE_FADE_MS 200
#define FRAME_INTERVAL_MS 16
#define TOUCH_DEBOUNCE_MS 250


typedef enum {
    MODE_BOOT,     // power-on sequence, inputs ignored
    MODE_CHOOSING, // die name and the selection ring, encoder live
    MODE_ARMED,    // blank, waiting to be consulted
    MODE_RESULT,   // a revealed roll
} ui_mode_t;

typedef enum {
    PENDING_NONE,
    PENDING_ARM,
    PENDING_ROLL,
} pending_t;

static bool ready = false;
static ui_mode_t mode = MODE_BOOT;
static pending_t pending = PENDING_NONE;
static uint8_t selected = 0;

static uint32_t last_rotation_ms = 0;
static uint32_t last_touch_ms = 0;
static uint32_t last_frame_ms = 0;
static bool was_touched = false;
static bool full_frame_pending = true;

static uint32_t fade_started_ms = 0;
static uint16_t fade_duration_ms = 0;
static float fade_from = 255.0f;
static float fade_to = 255.0f;

static void start_fade(float from, float to, uint16_t duration, uint32_t now) {
    fade_from = from;
    fade_to = to;
    fade_duration_ms = duration;
    fade_started_ms = now;
}

static bool is_fading(uint32_t now) {
    return (now - fade_started_ms) < fade_duration_ms;
}

static uint8_t fade_alpha(uint32_t now) {
    const uint32_t elapsed = now - fade_started_ms;
    if (elapsed >= fade_duration_ms) {
        return (uint8_t)lroundf(fade_to);
    }

    const float progress = (float)elapsed / (float)fade_duration_ms;
    return (uint8_t)lroundf(fade_from + (fade_to - fade_from) * progress);
}

static void roll_and_reveal(uint32_t now) {
    const die_t *die = &DICE[selected];
    const roll_t roll = roll_die(die);

    reveal_begin(&roll, now);
    mode = MODE_RESULT;
    start_fade(255.0f, 255.0f, 0, now);
    full_frame_pending = true;
}

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

static void render(uint32_t now) {
    const bool animating = mode == MODE_BOOT || is_fading(now) ||
                           (mode == MODE_RESULT && reveal_is_animating(now));
    if (!animating && !full_frame_pending) {
        return;
    }

    if (!full_frame_pending && (now - last_frame_ms) < FRAME_INTERVAL_MS) {
        return;
    }

    last_frame_ms = now;

    const theme_t *theme = theme_active();
    const uint8_t alpha = fade_alpha(now);

    // The armed screen only fades when the boot sequence hands over to it,
    // and that fade is the rim caption's, which lives outside the band.
    const bool whole_screen = full_frame_pending || mode == MODE_BOOT ||
                              mode == MODE_CHOOSING || (mode == MODE_ARMED && is_fading(now));

    if (whole_screen) {
        canvas_fill(theme->background);
    } else {
        canvas_fill_rows(REVEAL_STAGE_TOP, REVEAL_STAGE_HEIGHT, theme->background);
    }

    if (alpha > 0) {
        if (mode == MODE_BOOT) {
            boot_draw(now);
        } else if (mode == MODE_CHOOSING) {
            menu_draw(selected, alpha);
        } else if (mode == MODE_RESULT) {
            reveal_draw(now, alpha);
        }
    }

    // The rim caption trades places with the centred name as the choice settles,
    // then holds. Band frames never reach it, so it survives every roll.
    if (whole_screen && mode != MODE_BOOT) {
        uint8_t caption_alpha = 255;
        if (mode == MODE_CHOOSING) {
            caption_alpha = (uint8_t)(255 - alpha);
        } else if (mode == MODE_ARMED) {
            caption_alpha = alpha;
        }
        reveal_draw_caption(DICE[selected].name, caption_alpha);
    }

    if (whole_screen) {
        panel_present(canvas_pixels());
        full_frame_pending = false;
    } else {
        panel_present_rows(canvas_pixels(), REVEAL_STAGE_TOP, REVEAL_STAGE_HEIGHT);
    }
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
    selected = settings_die_index();

    canvas_fill(theme_active()->background);
    panel_present(canvas_pixels());
    panel_set_backlight(255);

    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
    touch_init();
    haptics_begin();
    encoder_begin();
    oracle_begin();

    const uint32_t now = millis();
    last_rotation_ms = now;
    boot_begin(now);
    start_fade(255.0f, 255.0f, 0, now);
    ready = true;
}

void loop() {
    if (!ready) {
        delay(100);
        return;
    }

    const uint32_t now = millis();

    // Inputs are drained but ignored until the sequence ends, so a turn during
    // boot does not land on a different die afterwards.
    if (mode == MODE_BOOT) {
        encoder_take_detents();
        touch_began(now);
        boot_tick(now);
        if (!boot_is_running(now)) {
            // Straight to the die that was in use before the power cycle,
            // armed, with its rim caption fading in.
            mode = MODE_ARMED;
            start_fade(0.0f, 255.0f, STAGE_FADE_MS, now);
            full_frame_pending = true;
        }

        render(now);
        delay(2);

        return;
    }

    const int32_t detents = encoder_take_detents();
    if (detents != 0) {
        int32_t next = ((int32_t)selected + detents) % (int32_t)DIE_COUNT;
        if (next < 0) {
            next += DIE_COUNT;
        }
        selected = (uint8_t)next;

        haptics_play(HAPTIC_DETENT);
        mode = MODE_CHOOSING;
        pending = PENDING_NONE;
        last_rotation_ms = now;
        start_fade(fade_alpha(now), 255.0f, CHOICE_FADE_MS, now);
        full_frame_pending = true;
    }

    if (touch_began(now)) {
        if (mode == MODE_ARMED) {
            roll_and_reveal(now);
        } else if (mode == MODE_RESULT) {
            start_fade(fade_alpha(now), 0.0f, STAGE_FADE_MS, now);
            pending = PENDING_ROLL;
        } else if (mode == MODE_CHOOSING) {
            // A touch confirms the choice and rolls it in one go.
            settings_set_die_index(selected);
            start_fade(fade_alpha(now), 0.0f, STAGE_FADE_MS, now);
            pending = PENDING_ROLL;
        }
    }

    if (mode == MODE_CHOOSING && pending == PENDING_NONE && !is_fading(now) &&
        (now - last_rotation_ms) > SELECTION_IDLE_MS) {
        start_fade(255.0f, 0.0f, STAGE_FADE_MS, now);
        pending = PENDING_ARM;
    }

    if (pending != PENDING_NONE && !is_fading(now)) {
        const pending_t action = pending;
        pending = PENDING_NONE;

        if (action == PENDING_ARM) {
            mode = MODE_ARMED;
            settings_set_die_index(selected);
            // The choice has faded out; the rim caption is drawn at full
            // strength from here on.
            start_fade(255.0f, 255.0f, 0, now);
            full_frame_pending = true;
        } else {
            roll_and_reveal(now);
        }
    }

    if (mode == MODE_RESULT) {
        reveal_tick(now);
    }

    render(now);
    delay(2);
}
