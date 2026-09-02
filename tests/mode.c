// The interaction rules, driven through the mode machine with a scripted clock
// and scripted inputs. Haptics and the persisted die are observed through the
// host adapters this program overrides.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "canvas.h"
#include "haptics.h"
#include "mode.h"
#include "oracle.h"
#include "reveal.h"
#include "settings.h"

#define STEP_MS 2
#define BOOT_LIMIT_MS 10000

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

static void reset_recording(void) {
    memset(haptic_counts, 0, sizeof(haptic_counts));
    persist_calls = 0;
}

static const mode_input_t NOTHING = {0, false};

static frame_rows_t step(uint32_t now, int32_t detents, bool tap) {
    const mode_input_t input = {detents, tap};
    return mode_step(now, input);
}

static bool is_whole(frame_rows_t rows) {
    return rows.top == 0 && rows.height == CANVAS_HEIGHT;
}

static bool is_stage(frame_rows_t rows) {
    const frame_rows_t stage = reveal_stage();
    return rows.top == stage.top && rows.height == stage.height;
}

// A frame may wait for the interval since the last one, so a change asked
// for now is allowed to arrive within that window.
static bool whole_frame_within(uint32_t now, frame_rows_t rows, uint32_t window) {
    bool whole = is_whole(rows);
    for (uint32_t later = now + STEP_MS; !whole && later <= now + window; later += STEP_MS) {
        whole = is_whole(mode_step(later, NOTHING));
    }

    return whole;
}

// Steps quietly from `from` to `to`, returning the first instant the machine
// reached `target`, or 0 if it never did.
static uint32_t step_until(uint32_t from, uint32_t to, ui_mode_t target) {
    for (uint32_t now = from; now <= to; now += STEP_MS) {
        mode_step(now, NOTHING);
        if (mode_current() == target) {
            return now;
        }
    }

    return 0;
}

// Runs boot with inputs that must be ignored. Returns the instant boot ended.
static uint32_t boot_on(uint8_t die) {
    reset_recording();
    mode_begin(0, die);
    EXPECT(mode_current() == MODE_BOOT, "begin does not start in boot");

    uint32_t handover = 0;
    for (uint32_t now = 0; now <= BOOT_LIMIT_MS; now += STEP_MS) {
        const bool poke = now == 1000 || now == 2500;
        const frame_rows_t rows = step(now, poke ? 3 : 0, poke);
        if (mode_current() != MODE_BOOT) {
            handover = now;
            EXPECT(whole_frame_within(now, rows, 16),
                   "boot handover did not present the whole screen within a frame");
            break;
        }
    }

    EXPECT(handover > 0, "boot never ended");
    EXPECT(mode_current() == MODE_ARMED, "boot handed over to mode %d, expected armed",
           (int)mode_current());
    EXPECT(mode_selected_die() == die, "inputs during boot moved the die to %u",
           (unsigned)mode_selected_die());
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

    const frame_rows_t rows = step(now, 1, false);
    EXPECT(mode_current() == MODE_CHOOSING, "a click did not open the list");
    EXPECT(mode_selected_die() == 4, "one click clockwise landed on %u", (unsigned)mode_selected_die());
    EXPECT(is_whole(rows), "opening the list presented %d+%d", rows.top, rows.height);
    EXPECT(haptic_counts[HAPTIC_DETENT] == 1, "a click played %d detent haptics",
           haptic_counts[HAPTIC_DETENT]);

    now += 100;
    step(now, -5, false);
    EXPECT(mode_selected_die() == (uint8_t)((4 + DIE_COUNT - 5) % DIE_COUNT),
           "five clicks back from 4 landed on %u", (unsigned)mode_selected_die());
    EXPECT(persist_calls == 0, "browsing persisted the die");
}

static void check_stillness_arms(void) {
    const uint32_t turned = boot_on(0) + 500;
    reset_recording();
    step(turned, 1, false);

    for (uint32_t now = turned + STEP_MS; now <= turned + 999; now += STEP_MS) {
        mode_step(now, NOTHING);
    }

    EXPECT(mode_current() == MODE_CHOOSING, "the list settled before a second of stillness");
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
    EXPECT(mode_current() == MODE_ARMED, "the armed screen did not hold");
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

    const frame_rows_t rows = step(now, 0, true);
    EXPECT(mode_current() == MODE_RESULT, "a tap when armed did not roll");
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
    EXPECT(mode_current() == MODE_RESULT, "a tap on a result left the result screen");
    for (uint32_t later = now; later <= now + 2000; later += STEP_MS) {
        mode_step(later, NOTHING);
    }
    EXPECT(haptic_counts[HAPTIC_ANSWER] == 2, "two taps played %d answer haptics",
           haptic_counts[HAPTIC_ANSWER]);
    EXPECT(mode_current() == MODE_RESULT, "the second roll did not stay on the result");
}

static void check_turning_leaves_a_result(void) {
    const uint32_t now = boot_on(5) + 500;
    step(now, 0, true);
    step(now + 300, 1, false);

    EXPECT(mode_current() == MODE_CHOOSING, "a click during a result did not open the list");
    EXPECT(mode_selected_die() == 6, "the click landed on %u", (unsigned)mode_selected_die());
}

// A resting screen asks for nothing; an animating one asks for the band, and
// no more often than the frame interval.
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
        const frame_rows_t rows = mode_step(later, NOTHING);
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
    check_turning_leaves_a_result();
    check_frames_are_paced();

    if (failures > 0) {
        fprintf(stderr, "mode: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("mode: ok");
    return 0;
}
