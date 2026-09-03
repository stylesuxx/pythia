/*
 * Safe mode, driven with a scripted clock and a scripted boot count. A boot
 * that reaches the loop and runs a while clears the count; one that never
 * does leaves it; the third in a row stays in safe mode and clears it, so the
 * boot after safe mode is a normal one.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "safe_mode.h"
#include "settings.h"

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

// The count port, scripted: what the store holds across these boots.
static uint8_t stored_attempts = 0;

uint8_t settings_note_boot_attempt(void) {
    stored_attempts++;
    return stored_attempts;
}

void settings_clear_boot_attempts(void) {
    stored_attempts = 0;
}

// A boot that never reaches the loop: begun, never stepped.
static bool boot_and_die(uint32_t now) {
    safe_mode_begin(now);
    return safe_mode_is_active();
}

// A boot that reaches the loop and runs for this long.
static bool boot_and_run(uint32_t now, uint32_t for_ms) {
    safe_mode_begin(now);
    for (uint32_t elapsed = 0; elapsed <= for_ms; elapsed += 100) {
        safe_mode_step(now + elapsed);
    }

    return safe_mode_is_active();
}

static void check_a_settled_boot_clears_the_count(void) {
    stored_attempts = 0;
    EXPECT(!boot_and_run(0, SAFE_MODE_SETTLED_MS + 100), "a first boot stayed in safe mode");
    EXPECT(stored_attempts == 0, "a settled boot left the count at %u", (unsigned)stored_attempts);
}

static void check_an_unsettled_boot_keeps_the_count(void) {
    stored_attempts = 0;
    EXPECT(!boot_and_run(0, SAFE_MODE_SETTLED_MS - 100), "a short boot stayed in safe mode");
    EXPECT(stored_attempts == 1, "a boot that never settled left the count at %u",
           (unsigned)stored_attempts);
}

static void check_the_third_failed_boot_is_safe(void) {
    stored_attempts = 0;
    EXPECT(!boot_and_die(0), "the first failed boot was safe mode");
    EXPECT(!boot_and_die(0), "the second failed boot was safe mode");
    EXPECT(boot_and_die(0), "the third failed boot was not safe mode");
    EXPECT(stored_attempts == 0, "safe mode left the count at %u, so the next boot is safe too",
           (unsigned)stored_attempts);
    EXPECT(!boot_and_run(0, SAFE_MODE_SETTLED_MS + 100), "the boot after safe mode was safe mode");
}

// Only failures in a row count: a settled boot between them starts over.
static void check_a_settled_boot_between_failures_starts_over(void) {
    stored_attempts = 0;
    EXPECT(!boot_and_die(0), "the first failed boot was safe mode");
    EXPECT(!boot_and_run(0, SAFE_MODE_SETTLED_MS + 100), "a settled boot was safe mode");
    EXPECT(!boot_and_die(0), "one failure after a settled boot was safe mode");
    EXPECT(!boot_and_die(0), "two failures after a settled boot were safe mode");
    EXPECT(boot_and_die(0), "three failures after a settled boot were not safe mode");
}

// The clock the shell hands over need not start at zero, and may wrap.
static void check_settling_counts_from_begin(void) {
    stored_attempts = 0;
    EXPECT(!boot_and_run(UINT32_MAX - 1000, SAFE_MODE_SETTLED_MS + 100),
           "a boot across the clock's wrap stayed in safe mode");
    EXPECT(stored_attempts == 0, "a boot across the clock's wrap never settled");
}

int main(void) {
    check_a_settled_boot_clears_the_count();
    check_an_unsettled_boot_keeps_the_count();
    check_the_third_failed_boot_is_safe();
    check_a_settled_boot_between_failures_starts_over();
    check_settling_counts_from_begin();

    if (failures > 0) {
        fprintf(stderr, "safe_mode: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    printf("safe_mode: after %d failed boots, settled at %u ms\n", SAFE_MODE_AFTER_ATTEMPTS,
           (unsigned)SAFE_MODE_SETTLED_MS);
    return 0;
}
