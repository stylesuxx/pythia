/*
 * The interaction rules, driven through the mode machine with a scripted clock
 * and scripted inputs. Haptics and the persisted die are observed through the
 * host adapters this program overrides.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "coin.h"
#include "effects/effect.h"
#include "haptics.h"
#include "mode.h"
#include "oracle.h"
#include "power.h"
#include "reveal.h"
#include "settings.h"

#define STEP_MS 2
#define BOOT_LIMIT_MS 10000

/**
 * The timeout handed to the machine. Short, so the sleep checks run in a
 * moment; the ramps themselves are proven in tests/power.c.
 */
#define IDLE_SLEEP_MS 5000

static int failures = 0;

#define EXPECT(condition, ...)                                                                    \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            fputs("FAIL: ", stderr);                                                              \
            fprintf(stderr, __VA_ARGS__);                                                         \
            fputc('\n', stderr);                                                                  \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

// Recording adapters, overriding the weak defaults in tools/host/adapters.c.
static int haptic_counts[256];
static int persist_calls = 0;
static uint8_t persisted_die = 0;

void haptics_play(uint8_t effect) {
    haptic_counts[effect]++;
}

void settings_set_die_index(uint8_t index) {
    persist_calls++;
    persisted_die = index;
}

// Whether D2 is thrown as a coin, handed to the machine at begin.
static bool coin_setting = true;

/**
 * The interaction rules hold under every effect. The slide is pinned so the
 * roll cue counted below is one known haptic.
 */
uint8_t settings_effect_index(void) {
    return effect_index_of("slide");
}

static void reset_recording(void) {
    memset(haptic_counts, 0, sizeof(haptic_counts));
    persist_calls = 0;
}

static const mode_input_t NOTHING = {0, false};

static frame_rect_t step(uint32_t now, int32_t detents, bool tap) {
    const mode_input_t input = {detents, tap};
    return mode_step(now, input);
}

static bool is_whole(frame_rect_t rows) {
    return rows.top == 0 && rows.height == CANVAS_HEIGHT;
}

static bool is_stage(frame_rect_t rows) {
    const frame_rect_t stage = reveal_stage();
    return rows.top == stage.top && rows.height == stage.height;
}

/**
 * A frame may wait for the interval since the last one, so a change asked
 * for now is allowed to arrive within that window.
 */
static bool whole_frame_within(uint32_t now, frame_rect_t rows, uint32_t window) {
    bool whole = is_whole(rows);
    for (uint32_t later = now + STEP_MS; !whole && later <= now + window; later += STEP_MS) {
        whole = is_whole(mode_step(later, NOTHING));
    }

    return whole;
}

/**
 * Steps quietly from `from` to `to`, returning the first instant the machine
 * reached `target`, or 0 if it never did.
 */
static uint32_t step_until(uint32_t from, uint32_t to, ui_mode_t target) {
    for (uint32_t now = from; now <= to; now += STEP_MS) {
        mode_step(now, NOTHING);
        if (mode_get_current() == target) {
            return now;
        }
    }

    return 0;
}

// Runs boot with inputs that must be ignored. Returns the instant boot ended.
static uint32_t boot_on(uint8_t die) {
    reset_recording();
    const mode_config_t config = {.die = die, .idle_ms = IDLE_SLEEP_MS, .coin_enabled = coin_setting};
    mode_begin(0, &config);
    EXPECT(mode_get_current() == MODE_BOOT, "begin does not start in boot");

    uint32_t handover = 0;
    for (uint32_t now = 0; now <= BOOT_LIMIT_MS; now += STEP_MS) {
        const bool poke = now == 1000 || now == 2500;
        const frame_rect_t rows = step(now, poke ? 3 : 0, poke);
        if (mode_get_current() != MODE_BOOT) {
            handover = now;
            EXPECT(whole_frame_within(now, rows, 16),
                   "boot handover did not present the whole screen within a frame");
            break;
        }
    }

    EXPECT(handover > 0, "boot never ended");
    EXPECT(mode_get_current() == MODE_ARMED, "boot handed over to mode %d, expected armed",
           (int)mode_get_current());
    EXPECT(mode_get_selected_die() == die, "inputs during boot moved the die to %u",
           (unsigned)mode_get_selected_die());
    EXPECT(persist_calls == 0, "boot persisted the die %d times", persist_calls);
    EXPECT(haptic_counts[HAPTIC_ANSWER] == 0, "a tap during boot rolled");

    return handover;
}

static void check_boot_ignores_inputs(void) {
    boot_on(3);
}

static void check_turning_browses_the_list(void) {
    uint32_t now = boot_on(3) + 500;
    reset_recording();

    const frame_rect_t rows = step(now, 1, false);
    EXPECT(mode_get_current() == MODE_CHOOSING, "a click did not open the list");
    EXPECT(mode_get_selected_die() == 4, "one click clockwise landed on %u", (unsigned)mode_get_selected_die());
    EXPECT(is_whole(rows), "opening the list presented %d+%d", rows.top, rows.height);
    EXPECT(haptic_counts[HAPTIC_DETENT] == 1, "a click played %d detent haptics",
           haptic_counts[HAPTIC_DETENT]);

    now += 100;
    step(now, -5, false);
    EXPECT(mode_get_selected_die() == (uint8_t)((4 + DIE_COUNT - 5) % DIE_COUNT),
           "five clicks back from 4 landed on %u", (unsigned)mode_get_selected_die());
    EXPECT(persist_calls == 0, "browsing persisted the die");
}

static void check_stillness_arms(void) {
    const uint32_t turned = boot_on(0) + 500;
    reset_recording();
    step(turned, 1, false);

    for (uint32_t now = turned + STEP_MS; now <= turned + 999; now += STEP_MS) {
        mode_step(now, NOTHING);
    }

    EXPECT(mode_get_current() == MODE_CHOOSING, "the list settled before a second of stillness");
    EXPECT(persist_calls == 0, "the die was persisted before the choice settled");

    const uint32_t armed = step_until(turned + 1000, turned + 1600, MODE_ARMED);
    EXPECT(armed > 0, "a second of stillness did not arm");
    EXPECT(armed > turned + 1000, "armed at %u ms after the turn, before the second was up",
           (unsigned)(armed - turned));
    EXPECT(persist_calls == 1 && persisted_die == 1, "settling persisted die %u %d times",
           (unsigned)persisted_die, persist_calls);

    for (uint32_t now = armed; now <= armed + 3000; now += STEP_MS) {
        mode_step(now, NOTHING);
    }

    EXPECT(persist_calls == 1, "staying armed persisted the die again");
    EXPECT(mode_get_current() == MODE_ARMED, "the armed screen did not hold");
}

static void check_tap_on_the_list_rolls_at_once(void) {
    const uint32_t turned = boot_on(0) + 500;
    reset_recording();
    step(turned, 2, false);

    step(turned + 100, 0, true);
    const uint32_t rolled = step_until(turned + 100, turned + 600, MODE_RESULT);
    EXPECT(rolled > 0, "a tap on the list did not roll");
    EXPECT(rolled < turned + 1000, "the tap waited for the stillness second");
    EXPECT(persist_calls == 1 && persisted_die == 2, "the tap persisted die %u %d times",
           (unsigned)persisted_die, persist_calls);

    for (uint32_t now = rolled; now <= rolled + 1500; now += STEP_MS) {
        mode_step(now, NOTHING);
    }

    EXPECT(haptic_counts[HAPTIC_ANSWER] == 1, "the roll played %d answer haptics",
           haptic_counts[HAPTIC_ANSWER]);
}

static void check_tap_when_armed_rolls(void) {
    const uint32_t now = boot_on(5) + 500;
    reset_recording();

    const frame_rect_t rows = step(now, 0, true);
    EXPECT(mode_get_current() == MODE_RESULT, "a tap when armed did not roll");
    EXPECT(is_whole(rows), "the roll presented %d+%d", rows.top, rows.height);
    EXPECT(persist_calls == 0, "rolling persisted the die");
}

static void check_tap_on_a_result_rolls_again(void) {
    uint32_t now = boot_on(5) + 500;
    reset_recording();
    step(now, 0, true);

    // Let the first reveal play out entirely before the second tap.
    for (uint32_t later = now + STEP_MS; later <= now + 2000; later += STEP_MS) {
        mode_step(later, NOTHING);
    }
    now += 2000;
    EXPECT(haptic_counts[HAPTIC_ANSWER] == 1, "the first roll played %d answer haptics",
           haptic_counts[HAPTIC_ANSWER]);

    step(now, 0, true);
    EXPECT(mode_get_current() == MODE_RESULT, "a tap on a result left the result screen");
    for (uint32_t later = now; later <= now + 2000; later += STEP_MS) {
        mode_step(later, NOTHING);
    }
    EXPECT(haptic_counts[HAPTIC_ANSWER] == 2, "two taps played %d answer haptics",
           haptic_counts[HAPTIC_ANSWER]);
    EXPECT(mode_get_current() == MODE_RESULT, "the second roll did not stay on the result");
}

/**
 * D2 is thrown as a coin unless the setting turns that off, in which case it
 * prints a number like every other die. The two draw different stages, so the
 * rows a roll asks for say which one ran.
 */
static void check_the_coin_can_be_switched_off(void) {
    uint8_t coin_die = 0;
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        if (DICE[index].kind == DIE_COIN) {
            coin_die = index;
        }
    }

    const frame_rect_t thrown = coin_stage();
    const frame_rect_t printed = reveal_stage();
    EXPECT(thrown.top != printed.top, "the coin and the reveal claim the same stage");

    for (int enabled = 1; enabled >= 0; enabled--) {
        coin_setting = enabled != 0;
        const char *state = enabled ? "on" : "off";

        const uint32_t now = boot_on(coin_die) + 500;
        step(now, 0, true);
        EXPECT(mode_get_current() == MODE_RESULT, "a tap on the coin die did not roll");

        const frame_rect_t wanted = enabled ? thrown : printed;
        bool matched = false;
        for (uint32_t later = now + STEP_MS; later <= now + 300; later += STEP_MS) {
            const frame_rect_t rows = mode_step(later, NOTHING);
            if (rows.height == 0) {
                continue;
            }

            matched = rows.top == wanted.top && rows.height == wanted.height;
            break;
        }

        EXPECT(matched, "with the coin %s the roll did not animate stage %d+%d", state,
               wanted.top, wanted.height);

        // The cues belong to what is on stage: the coin has none, the reveal has its answer beat.
        for (uint32_t later = now; later <= now + 2000; later += STEP_MS) {
            mode_step(later, NOTHING);
        }

        const int expected_cues = enabled ? 0 : 1;
        EXPECT(haptic_counts[HAPTIC_ANSWER] == expected_cues,
               "with the coin %s the roll played %d answer cues, expected %d", state,
               haptic_counts[HAPTIC_ANSWER], expected_cues);
    }

    coin_setting = true;
}

// The coin fires no cue of its own, and the reveal's cues belong to the
// reveal: a coin flip must not borrow one from a reveal that is not on stage.
static void check_a_coin_flip_fires_no_cue(void) {
    uint8_t coin_die = 0;
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        if (DICE[index].kind == DIE_COIN) {
            coin_die = index;
        }
    }

    coin_setting = true;
    const uint32_t now = boot_on(coin_die) + 500;
    reset_recording();
    step(now, 0, true);
    EXPECT(mode_get_current() == MODE_RESULT, "a tap on the coin die did not roll");

    for (uint32_t later = now + STEP_MS; later <= now + 2000; later += STEP_MS) {
        mode_step(later, NOTHING);
    }

    EXPECT(haptic_counts[HAPTIC_ANSWER] == 0, "a coin flip played %d answer cues",
           haptic_counts[HAPTIC_ANSWER]);
    EXPECT(haptic_counts[HAPTIC_MODIFIER] == 0, "a coin flip played %d modifier cues",
           haptic_counts[HAPTIC_MODIFIER]);
}

static void check_turning_leaves_a_result(void) {
    const uint32_t now = boot_on(5) + 500;
    step(now, 0, true);
    step(now + 300, 1, false);

    EXPECT(mode_get_current() == MODE_CHOOSING, "a click during a result did not open the list");
    EXPECT(mode_get_selected_die() == 6, "the click landed on %u", (unsigned)mode_get_selected_die());
}

/**
 * A resting screen asks for nothing; an animating one asks for the band, and
 * no more often than the frame interval.
 */
static void check_frames_are_paced(void) {
    const uint32_t now = boot_on(5) + 1000;
    mode_step(now, NOTHING);
    int idle_presents = 0;
    for (uint32_t later = now; later <= now + 500; later += STEP_MS) {
        idle_presents += mode_step(later, NOTHING).height != 0;
    }

    EXPECT(idle_presents == 0, "the armed screen presented %d frames while idle", idle_presents);

    step(now + 500, 0, true);
    int stage = 0;
    int other = 0;
    for (uint32_t later = now + 500 + STEP_MS; later <= now + 500 + 320; later += STEP_MS) {
        const frame_rect_t rows = mode_step(later, NOTHING);
        if (rows.height == 0) {
            continue;
        }

        if (is_stage(rows)) {
            stage++;
        } else {
            other++;
        }
    }

    EXPECT(other == 0, "the reveal presented %d frames beyond its stage", other);
    EXPECT(stage >= 15 && stage <= 21, "the reveal presented %d band frames in 320 ms", stage);
}

/**
 * Runs the clock forward until the screen is fully dark, then returns that
 * instant. Zero if it never slept.
 */
static uint32_t sleep_from(uint32_t from) {
    for (uint32_t now = from; now <= from + IDLE_SLEEP_MS + 5000; now += STEP_MS) {
        mode_step(now, NOTHING);
        if (power_is_asleep()) {
            return now;
        }
    }

    return 0;
}

static void check_idle_dims_then_sleeps(void) {
    const uint32_t armed = boot_on(3);

    EXPECT(mode_get_backlight() == 255, "the armed screen is not at full backlight");
    EXPECT(!power_is_asleep(), "the armed screen is asleep");

    // Well before the timeout nothing has moved.
    mode_step(armed + IDLE_SLEEP_MS / 2, NOTHING);
    EXPECT(mode_get_backlight() == 255, "the backlight fell after only half the timeout");

    const uint32_t dark = sleep_from(armed);
    EXPECT(dark > 0, "the screen never slept");
    EXPECT(dark > armed + IDLE_SLEEP_MS, "the screen slept before the timeout elapsed");
    EXPECT(mode_get_backlight() == 0, "output went off at backlight %u", (unsigned)mode_get_backlight());
    EXPECT(mode_get_current() == MODE_ARMED, "sleeping changed the mode to %d", (int)mode_get_current());
}

static void check_a_touch_wakes_without_rolling(void) {
    const uint32_t armed = boot_on(3);
    const uint32_t dark = sleep_from(armed);
    reset_recording();

    step(dark + 100, 0, true);
    EXPECT(!power_is_asleep(), "a touch did not wake the screen");
    EXPECT(mode_get_current() == MODE_ARMED, "the waking touch left mode %d, expected armed",
           (int)mode_get_current());
    EXPECT(haptic_counts[HAPTIC_ANSWER] == 0, "the waking touch rolled");

    // It only woke: the next touch is the one that rolls.
    step(dark + 400, 0, true);
    EXPECT(mode_get_current() == MODE_RESULT, "the touch after waking did not roll");
}

static void check_a_turn_wakes_and_is_acted_on(void) {
    const uint32_t armed = boot_on(3);
    const uint32_t dark = sleep_from(armed);
    reset_recording();

    step(dark + 100, 1, false);
    EXPECT(!power_is_asleep(), "a turn did not wake the screen");
    EXPECT(mode_get_current() == MODE_CHOOSING, "the waking turn left mode %d, expected the list",
           (int)mode_get_current());
    EXPECT(mode_get_selected_die() == 4, "the waking turn was swallowed; die is %u",
           (unsigned)mode_get_selected_die());
}

int main(void) {
    if (!canvas_begin()) {
        fputs("mode: no framebuffer\n", stderr);
        return 1;
    }

    check_boot_ignores_inputs();
    check_turning_browses_the_list();
    check_stillness_arms();
    check_tap_on_the_list_rolls_at_once();
    check_tap_when_armed_rolls();
    check_tap_on_a_result_rolls_again();
    check_the_coin_can_be_switched_off();
    check_a_coin_flip_fires_no_cue();
    check_turning_leaves_a_result();
    check_frames_are_paced();
    check_idle_dims_then_sleeps();
    check_a_touch_wakes_without_rolling();
    check_a_turn_wakes_and_is_acted_on();

    if (failures > 0) {
        fprintf(stderr, "mode: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("mode: ok");
    return 0;
}
