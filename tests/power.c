// Proves the screen power machine through its interface: the screen stays lit
// while input keeps coming or sleep is not permitted, dims gradually once the
// idle timeout passes, is dark and asleep afterwards, and any input brings it
// back with a ramp that continues from whatever level the dim had reached.

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "power.h"

#define IDLE_MS 5000
#define STEP_MS 2
#define DIM_MS 700
#define WAKE_MS 140

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

// Steps from `from` until the screen is asleep and returns that instant, or 0
// if it never sleeps within twice the timeout.
static uint32_t sleep_from(uint32_t from) {
    for (uint32_t now = from; now <= from + 2 * IDLE_MS; now += STEP_MS) {
        power_step(now, true);
        if (power_is_asleep()) {
            return now;
        }
    }

    return 0;
}

static void check_stays_lit_until_the_timeout(void) {
    power_begin(0, IDLE_MS);
    EXPECT(power_level() == 255, "the screen does not start lit");
    EXPECT(power_is_awake(), "the screen does not start awake");

    for (uint32_t now = 0; now <= IDLE_MS; now += STEP_MS) {
        power_step(now, true);
        EXPECT(power_level() == 255, "the light fell at %u ms, before the timeout", (unsigned)now);
    }
}

static void check_never_sleeps_when_not_permitted(void) {
    power_begin(0, IDLE_MS);
    for (uint32_t now = 0; now <= 3 * IDLE_MS; now += STEP_MS) {
        power_step(now, false);
    }

    EXPECT(power_level() == 255, "the screen dimmed while sleep was not permitted");
    EXPECT(power_is_awake(), "the screen stopped being awake while sleep was not permitted");
}

static void check_dims_gradually_then_sleeps(void) {
    power_begin(0, IDLE_MS);

    uint8_t previous = 255;
    bool partial = false;
    uint32_t dim_started = 0;
    uint32_t dark = 0;
    for (uint32_t now = 0; now <= 2 * IDLE_MS; now += STEP_MS) {
        const uint8_t level = power_step(now, true);
        EXPECT(level <= previous, "the light rose from %u to %u while dimming", previous, level);

        if (level < 255 && dim_started == 0) {
            dim_started = now;
        }

        if (level > 0 && level < 255) {
            partial = true;
            EXPECT(!power_is_asleep(), "asleep at level %u, while the picture was still lit", level);
            EXPECT(!power_is_awake(), "awake at level %u, in the middle of the dim", level);
        }

        if (power_is_asleep()) {
            dark = now;
            break;
        }

        previous = level;
    }

    EXPECT(dim_started > IDLE_MS, "the dim began at %u ms, before the timeout", (unsigned)dim_started);
    EXPECT(partial, "the light jumped straight from full to off");
    EXPECT(dark > 0, "the screen never slept");
    EXPECT(power_level() == 0, "asleep at level %u rather than dark", power_level());
    EXPECT(dark - dim_started >= DIM_MS - STEP_MS && dark - dim_started <= DIM_MS + STEP_MS,
           "the dim took %u ms, expected about %u", (unsigned)(dark - dim_started), DIM_MS);

    // Asleep is a resting state: the clock alone never brings the light back.
    for (uint32_t now = dark; now <= dark + IDLE_MS; now += STEP_MS) {
        power_step(now, true);
    }

    EXPECT(power_is_asleep() && power_level() == 0, "the screen woke on its own");
}

static void check_input_while_awake_resets_the_timer(void) {
    power_begin(0, IDLE_MS);
    for (uint32_t now = 0; now <= IDLE_MS / 2; now += STEP_MS) {
        power_step(now, true);
    }
    power_notice_input(IDLE_MS / 2);
    EXPECT(power_is_awake() && power_level() == 255, "input while awake disturbed the light");

    for (uint32_t now = IDLE_MS / 2; now <= IDLE_MS + IDLE_MS / 4; now += STEP_MS) {
        power_step(now, true);
        EXPECT(power_level() == 255, "the light fell at %u ms, inside the reset timeout",
               (unsigned)now);
    }

    const uint32_t dark = sleep_from(IDLE_MS + IDLE_MS / 4);
    EXPECT(dark > IDLE_MS / 2 + IDLE_MS, "the screen slept at %u ms, before the reset timeout",
           (unsigned)dark);
}

static void check_wake_from_dark_ramps_up(void) {
    power_begin(0, IDLE_MS);
    const uint32_t dark = sleep_from(0);
    EXPECT(dark > 0, "the screen never slept");

    power_notice_input(dark + 100);
    EXPECT(!power_is_asleep(), "input did not wake the screen");

    uint8_t previous = power_step(dark + 100, true);
    EXPECT(previous < 255, "waking jumped straight to full light");

    uint32_t full = 0;
    for (uint32_t now = dark + 100; now <= dark + 100 + 4 * WAKE_MS; now += STEP_MS) {
        const uint8_t level = power_step(now, true);
        EXPECT(level >= previous, "the light fell from %u to %u while waking", previous, level);
        previous = level;
        if (level == 255) {
            full = now;
            break;
        }
    }

    EXPECT(full > 0, "the light never came back to full");
    EXPECT(full - (dark + 100) <= WAKE_MS + STEP_MS, "waking took %u ms, expected about %u",
           (unsigned)(full - (dark + 100)), WAKE_MS);
    EXPECT(power_is_awake(), "the screen is not awake at full light");
}

// A hand that catches the dim gets the light back from where it was, and the
// touch counts as a wake, since the machine only acts on input while awake.
static void check_a_caught_dim_brightens_from_where_it_was(void) {
    power_begin(0, IDLE_MS);

    uint32_t caught = 0;
    uint8_t at_catch = 0;
    for (uint32_t now = 0; now <= 2 * IDLE_MS; now += STEP_MS) {
        const uint8_t level = power_step(now, true);
        if (level < 128) {
            caught = now;
            at_catch = level;
            break;
        }
    }

    EXPECT(caught > 0 && at_catch > 0, "the dim never reached a catchable level");
    EXPECT(!power_is_awake(), "the screen counted as awake mid-dim, so a touch would roll");

    power_notice_input(caught);
    uint8_t previous = power_step(caught, true);
    EXPECT(previous >= at_catch, "the caught dim restarted from %u, below the %u it had reached",
           previous, at_catch);

    for (uint32_t now = caught; now <= caught + 4 * WAKE_MS; now += STEP_MS) {
        const uint8_t level = power_step(now, true);
        EXPECT(level >= previous, "the light fell from %u to %u after being caught", previous,
               level);

        previous = level;
        if (level == 255) {
            break;
        }
    }
    EXPECT(previous == 255 && power_is_awake(), "the caught dim never came back to full");
}

int main(void) {
    check_stays_lit_until_the_timeout();
    check_never_sleeps_when_not_permitted();
    check_dims_gradually_then_sleeps();
    check_input_while_awake_resets_the_timer();
    check_wake_from_dark_ramps_up();
    check_a_caught_dim_brightens_from_where_it_was();

    if (failures > 0) {
        fprintf(stderr, "power: %d failure(s)\n", failures);
        return 1;
    }

    printf("power: lit, dimmed, asleep and woken, timeout %u ms\n", (unsigned)IDLE_MS);
    return 0;
}
