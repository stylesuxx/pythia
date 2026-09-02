#include "oracle.h"

#include <stdio.h>

#include "bootloader_random.h"
#include "esp_cpu.h"
#include "esp_random.h"

const die_t DICE[] = {
    {"D2", DIE_NUMERIC, 2},
    {"D4", DIE_NUMERIC, 4},
    {"D6", DIE_NUMERIC, 6},
    {"D8", DIE_NUMERIC, 8},
    {"D10", DIE_NUMERIC, 10},
    {"D12", DIE_NUMERIC, 12},
    {"D20", DIE_NUMERIC, 20},
    {"D66", DIE_D66, 0},
    {"D100", DIE_NUMERIC, 100},
    {"ORACLE", DIE_ORACLE, 0},
};

const uint8_t DIE_COUNT = (uint8_t)(sizeof(DICE) / sizeof(DICE[0]));

// The six oracle outcomes, ordered as a d6 table reads from worst to best.
static const struct {
    const char *answer;
    const char *modifier;
} ORACLE_OUTCOMES[] = {
    {"NO", NULL},
    {"NO", "and"},
    {"NO", "but"},
    {"YES", NULL},
    {"YES", "but"},
    {"YES", "and"},
};

const uint8_t ORACLE_OUTCOME_COUNT =
    (uint8_t)(sizeof(ORACLE_OUTCOMES) / sizeof(ORACLE_OUTCOMES[0]));

roll_t oracle_outcome(uint8_t index) {
    roll_t roll = {
      .kind = DIE_ORACLE,
      .answer = "",
      .modifier = NULL
    };

    if (index >= ORACLE_OUTCOME_COUNT) {
        index = 0;
    }

    snprintf(roll.answer, sizeof(roll.answer), "%s", ORACLE_OUTCOMES[index].answer);
    roll.modifier = ORACLE_OUTCOMES[index].modifier;

    return roll;
}

// Keeps the SAR ADC feeding thermal noise into the hardware RNG for the life
// of the app. It claims the ADC, so any ADC read must bracket itself with
// bootloader_random_disable().
void oracle_begin(void) {
  bootloader_random_enable();
}

// One 32-bit draw: hardware RNG mixed with the CPU cycle counter, which at
// roll time carries the instant of the tap.
static uint32_t draw(void) {
  return esp_random() ^ esp_cpu_get_cycle_count();
}

// Rejection sampling, so every face stays equally likely regardless of sides.
static uint32_t roll_below(uint32_t sides) {
    const uint64_t span = 0x100000000ULL;
    const uint64_t threshold = span - (span % sides);
    uint32_t value;
    do {
        value = draw();
    } while ((uint64_t)value >= threshold);

    return value % sides;
}

roll_t roll_die(const die_t *die) {
    roll_t roll = {
      .kind = die->kind,
      .answer = "",
      .modifier = NULL
    };

    switch (die->kind) {
        case DIE_ORACLE: {
            roll = oracle_outcome((uint8_t)roll_below(ORACLE_OUTCOME_COUNT));
        } break;

        case DIE_D66: {
            const uint32_t tens = roll_below(6) + 1;
            const uint32_t units = roll_below(6) + 1;
            snprintf(roll.answer, sizeof(roll.answer), "%u", (unsigned)(tens * 10 + units));
        } break;

        case DIE_NUMERIC:
        default: {
            snprintf(roll.answer, sizeof(roll.answer), "%u",
                     (unsigned)(roll_below(die->sides) + 1));
        } break;
    }

    return roll;
}
