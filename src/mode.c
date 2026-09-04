#include "mode.h"

#include <math.h>
#include <string.h>

#include "scenes/boot.h"
#include "render/canvas.h"
#include "frame.h"
#include "haptics.h"
#include "scenes/caption.h"
#include "dice.h"
#include "scenes/menu.h"
#include "oracle.h"
#include "power.h"
#include "settings.h"
#include "stage.h"
#include "render/theme.h"

// The knob has no button, so stillness is what confirms a choice.
#define SELECTION_IDLE_MS 1000
#define CHOICE_FADE_MS 180
#define STAGE_FADE_MS 200

// Presses closer together than this are one touch: contacts bounce.
#define TOUCH_DEBOUNCE_MS 250

typedef enum {
    PENDING_NONE,
    PENDING_ARM,
    PENDING_ROLL,
} pending_t;

static ui_mode_t mode = MODE_BOOT;
static pending_t pending = PENDING_NONE;
static uint8_t selected = 0;

// The die in use by name, and how the machine was begun, for a restart.
static char selected_name[DIE_NAME_CAPACITY];
static mode_config_t config;

static uint32_t last_rotation_ms = 0;

// The touch level at the previous step and the instant of the last tap.
static bool was_touched = false;
static uint32_t last_tap_ms = 0;

/**
 * One alpha moving between two values. Restarting it from its current value
 * is what lets an interrupted fade continue smoothly.
 */
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
    const roll_t roll = roll_die(&dice_active()[selected]);
    stage_begin(&roll, config.coin_enabled, now);
    mode = MODE_RESULT;
    start_fade(255.0f, 255.0f, 0, now);
    frame_mark_whole();
}

static void settle_choice(void) {
    memcpy(selected_name, dice_active()[selected].name, sizeof(selected_name));
    settings_set_die_name(selected_name);
}

static void handle_boot(uint32_t now) {
    boot_tick(now);
    if (boot_is_running(now)) {
        return;
    }

    /*
     * Straight to the die that was in use before the power cycle, armed, with
     * its rim caption fading in.
     */
    mode = MODE_ARMED;
    power_notice_input(now);
    start_fade(0.0f, 255.0f, STAGE_FADE_MS, now);
    frame_mark_whole();
}

static void handle_rotation(uint32_t now, int32_t detents) {
    const int32_t count = (int32_t)dice_count();
    int32_t next = ((int32_t)selected + detents) % count;
    if (next < 0) {
        next += count;
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
            // What is on stage may take the next roll where it lies.
            if (stage_is_rerolled_in_place()) {
                roll_and_reveal(now);
                break;
            }

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

        /*
         * The choice has faded out; the rim caption is drawn at full strength
         * from here on.
         */
        start_fade(255.0f, 255.0f, 0, now);
        frame_mark_whole();

        return;
    }

    roll_and_reveal(now);
}

static void draw_scene(void *context, frame_rect_t rows) {
    const uint32_t now = *(const uint32_t *)context;
    const uint8_t alpha = fade_alpha(now);

    /*
     * The rim caption trades places with the centred name as the choice
     * settles, then holds. It is drawn whenever a frame covers its rows; a
     * roll's band lies clear of them, so it survives every roll untouched.
     */
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
            stage_draw(now, alpha);
            caption_alpha = 255;
        } break;
    }

    if (frame_rect_is_overlapping(rows, caption_get_rect())) {
        caption_draw(dice_active()[selected].name, caption_alpha);
    }
}

/**
 * Marks what is moving on the current screen, then lets the frame decide
 * whether this instant gets drawn.
 */
static frame_rect_t render(uint32_t now) {
    switch (mode) {
        case MODE_BOOT: {
            frame_mark_whole();
        } break;

        case MODE_CHOOSING: {
            if (is_fading(now)) {
                frame_mark_whole();
            }
        } break;

        case MODE_ARMED: {
            if (is_fading(now)) {
                frame_mark_whole();
            }
        } break;

        case MODE_RESULT: {
            // Nothing can be seen while asleep, so an animation stops costing frames.
            if (is_fading(now) || (stage_is_animating(now) && !power_is_asleep())) {
                frame_mark(stage_get_rect());
            }
        } break;
    }

    return frame_render(now, theme_active()->colors.background, draw_scene, &now);
}

void mode_begin(uint32_t now, const mode_config_t *begun_with) {
    config = *begun_with;
    selected = config.die < dice_count() ? config.die : dice_index_of("ORACLE");
    memcpy(selected_name, dice_active()[selected].name, sizeof(selected_name));
    mode = MODE_BOOT;
    pending = PENDING_NONE;
    last_rotation_ms = now;
    was_touched = false;
    last_tap_ms = now - TOUCH_DEBOUNCE_MS;
    power_begin(now, config.idle_ms);

    frame_begin(now);
    frame_mark_whole();
    boot_begin(now);
    start_fade(255.0f, 255.0f, 0, now);
}

frame_rect_t mode_step(uint32_t now, mode_input_t input) {
    // The files changed under the machine, the layout perhaps among them.
    if (input.restart) {
        mode_config_t again = config;
        again.die = dice_index_of(selected_name);
        mode_begin(now, &again);
    }

    /*
     * Inputs are drained but ignored until the boot sequence ends, so a turn
     * during boot does not land on a different die afterwards.
     */
    if (mode == MODE_BOOT) {
        was_touched = input.touched;
        handle_boot(now);
        power_step(now, false);
        return render(now);
    }

    /*
     * A tap is the instant a contact begins, once per press however long the
     * finger stays, and never within the debounce of the previous tap.
     */
    bool tap = input.touched && !was_touched && (now - last_tap_ms) > TOUCH_DEBOUNCE_MS;
    was_touched = input.touched;
    if (tap) {
        last_tap_ms = now;
    }

    /*
     * Only the armed and result screens can doze: SELECTION_IDLE_MS is a
     * second, so choosing has always settled long before the timeout. The
     * step runs before the inputs are read, so a step after a long gap
     * decides whether a touch is a wake or a roll from the state as it is now.
     */
    power_step(now, mode == MODE_ARMED || mode == MODE_RESULT);

    if (input.detents != 0 || tap) {
        /*
         * A touch that wakes the screen is spent on waking: letting it through
         * would consult the die the instant the light comes up, and there
         * would be no way to wake the terminal without spending a roll. A
         * detent is passed through, because a click that moves nothing reads
         * as a fault, and a turn already means "go to the die list".
         */
        if (!power_is_awake()) {
            tap = false;
        }

        power_notice_input(now);
    }

    if (input.detents != 0) {
        handle_rotation(now, input.detents);
    }

    if (tap) {
        handle_tap(now);
    }

    handle_stillness(now);
    handle_pending(now);

    if (mode == MODE_RESULT) {
        stage_tick(now);
    }

    return render(now);
}

ui_mode_t mode_get_current(void) {
    return mode;
}

uint8_t mode_get_selected_die(void) {
    return selected;
}

uint8_t mode_get_backlight(void) {
    return power_get_level();
}
