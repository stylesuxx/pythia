#include "oracle.h"

#include <stdio.h>
#include <string.h>

#include "bootloader_random.h"
#include "esp_cpu.h"
#include "esp_random.h"

/**
 * The six oracle outcomes, ordered as a d6 oracle table reads from worst to
 * best: a "but" softens whichever answer it follows, an "and" strengthens it.
 */
static const struct {
    const char *answer;
    const char *modifier;
} ORACLE_OUTCOMES[] = {
    {"NO", "and"},
    {"NO", NULL},
    {"NO", "but"},
    {"YES", "but"},
    {"YES", NULL},
    {"YES", "and"},
};

const uint8_t ORACLE_OUTCOME_COUNT =
    (uint8_t)(sizeof(ORACLE_OUTCOMES) / sizeof(ORACLE_OUTCOMES[0]));

roll_t oracle_outcome(uint8_t index) {
    roll_t roll = {
      .kind = DIE_ORACLE,
      .answer = "",
      .value = 0,
      .modifier = NULL
    };

    if (index >= ORACLE_OUTCOME_COUNT) {
        index = 0;
    }

    snprintf(roll.answer, sizeof(roll.answer), "%s", ORACLE_OUTCOMES[index].answer);
    roll.modifier = ORACLE_OUTCOMES[index].modifier;

    return roll;
}

/**
 * Keeps the SAR ADC feeding thermal noise into the hardware RNG for the life
 * of the app. It claims the ADC, so any ADC read must bracket itself with
 * bootloader_random_disable().
 */
void oracle_begin(void) {
  bootloader_random_enable();
}

/**
 * One 32-bit draw: hardware RNG mixed with the CPU cycle counter, which at
 * roll time carries the instant of the tap.
 */
static uint32_t draw(void) {
  return esp_random() ^ esp_cpu_get_cycle_count();
}

/**
 * A number in 0 to sides - 1, every value equally likely.
 *
 * A draw is 32 bits, and 2^32 is rarely a multiple of the side count, so
 * taking the draw modulo sides would favour the low faces.
 *
 * Eg.: for a D6, 2^32 is 4 more than a multiple of 6, so 0 to 3 come up one
 *      time in 715 million more often than 4 and 5.
 *
 * The fix is to refuse any draw from that incomplete final window and draw
 * again. Every value below the threshold maps to a face the same number of
 * times, so the result is exact rather than nearly so, and the window is at
 * most sides - 1 values out of 2^32, so a second draw is rare.
 */
static uint32_t roll_below(uint32_t sides) {
    const uint64_t span = 0x100000000ULL;
    const uint64_t threshold = span - (span % sides);

    uint32_t value = draw();
    while ((uint64_t)value >= threshold) {
        value = draw();
    }

    return value % sides;
}

roll_t roll_die(const die_t *die) {
    roll_t roll = {
      .kind = die->kind,
      .effect = die->effect,
      .answer = "",
      .value = 0,
      .modifier = NULL
    };

    switch (die->kind) {
        case DIE_ORACLE: {
            roll = oracle_outcome((uint8_t)roll_below(ORACLE_OUTCOME_COUNT));
        } break;

        case DIE_D66: {
            const uint32_t tens = roll_below(6) + 1;
            const uint32_t units = roll_below(6) + 1;
            roll.value = (uint8_t)(tens * 10 + units);
            snprintf(roll.answer, sizeof(roll.answer), "%u", (unsigned)roll.value);
        } break;

        case DIE_COIN:
        case DIE_NUMERIC:
        default: {
            roll.value = (uint8_t)(roll_below(die->sides) + 1);
            snprintf(roll.answer, sizeof(roll.answer), "%u", (unsigned)roll.value);
        } break;
    }

    return roll;
}
