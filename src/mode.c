#include "mode.h"

#include <math.h>

#include "boot.h"
#include "canvas.h"
#include "frame.h"
#include "haptics.h"
#include "menu.h"
#include "oracle.h"
#include "reveal.h"
#include "settings.h"
#include "theme.h"

// The knob has no button, so stillness is what confirms a choice.
#define SELECTION_IDLE_MS 1000
#define CHOICE_FADE_MS 180
#define STAGE_FADE_MS 200

// Idle time before the screen goes dark. Overridable from build_flags so a
// test build can be a flag rather than an edit: -DIDLE_SLEEP_MS=10000.
#ifndef IDLE_SLEEP_MS
#define IDLE_SLEEP_MS 120000
#endif

// Slow enough out that a hand reaching for the device can catch it, brisk
// enough back that waking feels immediate.
#define SLEEP_FADE_MS 700
#define WAKE_FADE_MS 140

typedef enum {
    PENDING_NONE,
    PENDING_ARM,
    PENDING_ROLL,
} pending_t;

typedef enum {
    POWER_AWAKE,
    POWER_DIMMING,  // fading out, still catchable
    POWER_ASLEEP,   // backlight off and frame buffer output disabled
    POWER_LIGHTING, // fading back in
} power_t;

static ui_mode_t mode = MODE_BOOT;
static pending_t pending = PENDING_NONE;
static uint8_t selected = 0;

static uint32_t last_rotation_ms = 0;

// Nothing is redrawn when the screen sleeps or wakes. The canvas keeps the
// last frame and so does the panel's own memory, so the image is still there
// behind a dark backlight and a disabled output.
static power_t power = POWER_AWAKE;
static uint32_t power_started_ms = 0;
static float power_from = 255.0f;
static uint32_t last_input_ms = 0;
static uint8_t backlight = 255;
static bool display_on = true;

// One alpha moving between two values. Restarting it from its current value
// is what lets an interrupted fade continue smoothly.
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

static uint8_t ramp(uint32_t now, uint16_t duration, float to) {
    const uint32_t elapsed = now - power_started_ms;
    if (elapsed >= duration) {
        return (uint8_t)lroundf(to);
    }

    const float progress = (float)elapsed / (float)duration;
    return (uint8_t)lroundf(power_from + (to - power_from) * progress);
}

// Only ARMED and RESULT can ever doze: SELECTION_IDLE_MS is a second, so
// CHOOSING has always settled long before the sleep timeout could elapse.
// Transitions that only depend on the clock. Runs before the inputs are read,
// so a step after a long gap decides whether a touch is a wake or a roll from
// the state as it is now, not as it was at the previous step.
static void power_settle(uint32_t now) {
    if (power == POWER_DIMMING && (now - power_started_ms) >= SLEEP_FADE_MS) {
        power = POWER_ASLEEP;
    } else if (power == POWER_LIGHTING && (now - power_started_ms) >= WAKE_FADE_MS) {
        power = POWER_AWAKE;
    }
}

// Only ARMED and RESULT can ever doze: SELECTION_IDLE_MS is a second, so
// CHOOSING has always settled long before the sleep timeout could elapse.
static void power_check_idle(uint32_t now) {
    if (power == POWER_AWAKE && (mode == MODE_ARMED || mode == MODE_RESULT) &&
        (now - last_input_ms) > IDLE_SLEEP_MS) {
        power = POWER_DIMMING;
        power_started_ms = now;
        power_from = 255.0f;
    }
}

static void power_output(uint32_t now) {
    switch (power) {
        case POWER_AWAKE: {
            backlight = 255;
        } break;

        case POWER_ASLEEP: {
            backlight = 0;
        } break;

        case POWER_DIMMING: {
            backlight = ramp(now, SLEEP_FADE_MS, 0.0f);
        } break;

        case POWER_LIGHTING: {
            backlight = ramp(now, WAKE_FADE_MS, 255.0f);
        } break;
    }

    display_on = power != POWER_ASLEEP;
}

// Picks the ramp up from wherever the dim had reached, so catching a fade
// mid-way brightens from there instead of restarting from black.
static void power_wake(uint32_t now) {
    power_from = (float)backlight;
    power_started_ms = now;
    power = POWER_LIGHTING;
}

static void roll_and_reveal(uint32_t now) {
    const roll_t roll = roll_die(&DICE[selected]);
    reveal_begin(&roll, now);
    mode = MODE_RESULT;
    start_fade(255.0f, 255.0f, 0, now);
    frame_mark_whole();
}

static void settle_choice(void) {
    settings_set_die_index(selected);
}

static void handle_boot(uint32_t now) {
    boot_tick(now);
    if (boot_is_running(now)) {
        return;
    }

    // Straight to the die that was in use before the power cycle, armed, with
    // its rim caption fading in.
    mode = MODE_ARMED;
    last_input_ms = now;
    start_fade(0.0f, 255.0f, STAGE_FADE_MS, now);
    frame_mark_whole();
}

static void handle_rotation(uint32_t now, int32_t detents) {
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
    frame_mark_whole();
}

static void handle_tap(uint32_t now) {
    switch (mode) {
        case MODE_ARMED: {
            roll_and_reveal(now);
        } break;

        case MODE_RESULT: {
            start_fade(fade_alpha(now), 0.0f, STAGE_FADE_MS, now);
            pending = PENDING_ROLL;
        } break;

        case MODE_CHOOSING: {
            // A touch confirms the choice and rolls it in one go.
            settle_choice();
            start_fade(fade_alpha(now), 0.0f, STAGE_FADE_MS, now);
            pending = PENDING_ROLL;
        } break;

        case MODE_BOOT:
            break;
    }
}

static void handle_stillness(uint32_t now) {
    if (mode == MODE_CHOOSING && pending == PENDING_NONE && !is_fading(now) &&
        (now - last_rotation_ms) > SELECTION_IDLE_MS) {
        start_fade(255.0f, 0.0f, STAGE_FADE_MS, now);
        pending = PENDING_ARM;
    }
}

// A pending action waits for the fade that precedes it to finish.
static void handle_pending(uint32_t now) {
    if (pending == PENDING_NONE || is_fading(now)) {
        return;
    }

    const pending_t action = pending;
    pending = PENDING_NONE;

    if (action == PENDING_ARM) {
        mode = MODE_ARMED;
        settle_choice();

        // The choice has faded out; the rim caption is drawn at full strength
        // from here on.
        start_fade(255.0f, 255.0f, 0, now);
        frame_mark_whole();

        return;
    }

    roll_and_reveal(now);
}

static void draw_scene(void *context, frame_rows_t rows) {
    const uint32_t now = *(const uint32_t *)context;
    const uint8_t alpha = fade_alpha(now);

    // The rim caption trades places with the centred name as the choice
    // settles, then holds. It lies outside the reveal's stage, so a band
    // frame never reaches it and it survives every roll.
    uint8_t caption_alpha = 0;
    switch (mode) {
        case MODE_BOOT: {
            boot_draw(now);
        } break;

        case MODE_CHOOSING: {
            menu_draw(selected, alpha);
            caption_alpha = (uint8_t)(255 - alpha);
        } break;

        case MODE_ARMED: {
            caption_alpha = alpha;
        } break;

        case MODE_RESULT: {
            reveal_draw(now, alpha);
            caption_alpha = 255;
        } break;
    }

    if (rows.height == CANVAS_HEIGHT) {
        reveal_draw_caption(DICE[selected].name, caption_alpha);
    }
}

// Marks what is moving on the current screen, then lets the frame decide
// whether this instant gets drawn.
static frame_rows_t render(uint32_t now) {
    switch (mode) {
        case MODE_BOOT: {
            frame_mark_whole();
        } break;

        case MODE_CHOOSING:
        case MODE_ARMED: {
            if (is_fading(now)) {
                frame_mark_whole();
            }
        } break;

        case MODE_RESULT: {
            if (is_fading(now) || reveal_is_animating(now)) {
                frame_mark(reveal_stage());
            }
        } break;
    }

    return frame_render(now, theme_active()->background, draw_scene, &now);
}

void mode_begin(uint32_t now, uint8_t die) {
    selected = die < DIE_COUNT ? die : 0;
    mode = MODE_BOOT;
    pending = PENDING_NONE;
    last_rotation_ms = now;
    last_input_ms = now;
    power = POWER_AWAKE;
    backlight = 255;
    display_on = true;

    frame_begin(now);
    frame_mark_whole();
    boot_begin(now);
    start_fade(255.0f, 255.0f, 0, now);
}

frame_rows_t mode_step(uint32_t now, mode_input_t input) {
    // Inputs are drained but ignored until the boot sequence ends, so a turn
    // during boot does not land on a different die afterwards.
    if (mode == MODE_BOOT) {
        handle_boot(now);
        power_output(now);
        return render(now);
    }

    power_settle(now);

    if (input.detents != 0 || input.tap) {
        // A touch that wakes the screen is spent on waking: letting it through
        // would consult the die the instant the light comes up, and there
        // would be no way to wake the terminal without spending a roll. A
        // detent is passed through, because a click that moves nothing reads
        // as a fault, and a turn already means "go to the die list".
        if (power != POWER_AWAKE) {
            input.tap = false;
            power_wake(now);
        }

        last_input_ms = now;
    }

    if (input.detents != 0) {
        handle_rotation(now, input.detents);
    }

    if (input.tap) {
        handle_tap(now);
    }

    handle_stillness(now);
    handle_pending(now);
    power_check_idle(now);
    power_output(now);

    if (mode == MODE_RESULT) {
        reveal_tick(now);
    }

    return render(now);
}

ui_mode_t mode_current(void) {
    return mode;
}

uint8_t mode_selected_die(void) {
    return selected;
}

uint8_t mode_backlight(void) {
    return backlight;
}

bool mode_is_display_on(void) {
    return display_on;
}
