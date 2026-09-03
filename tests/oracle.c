/*
 * Every face of every die must be exactly equally likely. That rests on the
 * roll discarding any draw that falls in the incomplete final window of
 * 2^32 modulo the side count and drawing again, so this program scripts the
 * entropy source and watches which draws a roll consumes.
 *
 * The host stands in a cycle counter of zero, so a scripted draw reaches the
 * roll unchanged.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_random.h"
#include "oracle.h"

#define SCRIPT_MAX 8

static uint32_t script[SCRIPT_MAX];
static int script_length = 0;
static int script_position = 0;
static bool script_exhausted = false;

// Overrides the weak default in tools/host/adapters.c.
uint32_t esp_random(void) {
    if (script_position >= script_length) {
        script_exhausted = true;
        return 0;
    }

    return script[script_position++];
}

static int failures = 0;

static const die_t *find_die(const char *name) {
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        if (strcmp(DICE[index].name, name) == 0) {
            return &DICE[index];
        }
    }

    return NULL;
}

/**
 * Rolls a die against a scripted draw sequence and checks the answer and the
 * number of draws it took.
 */
static void expect_roll(const char *die_name, const uint32_t *draws, int draw_count,
                        const char *answer, const char *modifier, int draws_consumed) {
    const die_t *die = find_die(die_name);
    if (die == NULL) {
        fprintf(stderr, "FAIL: no die named %s\n", die_name);
        failures++;
        return;
    }

    memcpy(script, draws, (size_t)draw_count * sizeof(uint32_t));
    script_length = draw_count;
    script_position = 0;
    script_exhausted = false;

    const roll_t roll = roll_die(die);
    const bool same_modifier = (roll.modifier == NULL && modifier == NULL) ||
                               (roll.modifier != NULL && modifier != NULL &&
                                strcmp(roll.modifier, modifier) == 0);

    if (script_exhausted) {
        fprintf(stderr, "FAIL: %s drew more than the %d scripted values\n", die_name, draw_count);
        failures++;
        return;
    }

    if (strcmp(roll.answer, answer) != 0 || !same_modifier) {
        fprintf(stderr, "FAIL: %s gave %s%s%s, expected %s%s%s\n", die_name, roll.answer,
                roll.modifier ? " " : "", roll.modifier ? roll.modifier : "", answer,
                modifier ? " " : "", modifier ? modifier : "");
        failures++;
        return;
    }

    if (script_position != draws_consumed) {
        fprintf(stderr, "FAIL: %s consumed %d draws, expected %d\n", die_name, script_position,
                draws_consumed);
        failures++;
    }

    // The number rolled travels beside its text, so nothing downstream parses.
    const unsigned expected_value = roll.kind == DIE_ORACLE ? 0 : (unsigned)atoi(roll.answer);
    if (roll.value != expected_value) {
        fprintf(stderr, "FAIL: %s carries value %u beside answer %s\n", die_name,
                (unsigned)roll.value, roll.answer);
        failures++;
    }
}

/**
 * The table reads as a d6 oracle table does, worst to best: 1 is "NO, and",
 * 6 is "YES, and", and a "but" softens whichever answer it follows.
 */
static void check_outcome_table(void) {
    static const struct {
        const char *answer;
        const char *modifier;
    } EXPECTED[] = {
        {"NO", "and"}, {"NO", NULL}, {"NO", "but"}, {"YES", "but"}, {"YES", NULL}, {"YES", "and"},
    };

    for (uint8_t index = 0; index < ORACLE_OUTCOME_COUNT && index < 6; index++) {
        const roll_t outcome = oracle_outcome(index);
        const bool same_answer = strcmp(outcome.answer, EXPECTED[index].answer) == 0;
        const bool same_modifier =
            (outcome.modifier == NULL && EXPECTED[index].modifier == NULL) ||
            (outcome.modifier != NULL && EXPECTED[index].modifier != NULL &&
             strcmp(outcome.modifier, EXPECTED[index].modifier) == 0);
        if (!same_answer || !same_modifier) {
            fprintf(stderr, "FAIL: outcome %u is %s%s%s, expected %s%s%s\n", index, outcome.answer,
                    outcome.modifier ? " " : "", outcome.modifier ? outcome.modifier : "",
                    EXPECTED[index].answer, EXPECTED[index].modifier ? " " : "",
                    EXPECTED[index].modifier ? EXPECTED[index].modifier : "");
            failures++;
        }
    }

    if (ORACLE_OUTCOME_COUNT != 6) {
        fprintf(stderr, "FAIL: the oracle has %u outcomes, a d6 table needs 6\n",
                (unsigned)ORACLE_OUTCOME_COUNT);
        failures++;
    }
}

// A die is found by its name, and a name not in the table falls back to the oracle.
static void check_die_index_of(void) {
    const uint8_t d20 = die_index_of("D20");
    if (d20 >= DIE_COUNT || strcmp(DICE[d20].name, "D20") != 0) {
        fprintf(stderr, "FAIL: die_index_of(D20) gave %u\n", (unsigned)d20);
        failures++;
    }

    const uint8_t unknown = die_index_of("D7");
    if (unknown >= DIE_COUNT || DICE[unknown].kind != DIE_ORACLE) {
        fprintf(stderr, "FAIL: an unknown die name gave %u rather than the oracle\n", (unsigned)unknown);
        failures++;
    }
}

int main(void) {
    check_die_index_of();
    // 2^32 mod 6 is 4, so the window is the top four values.
    expect_roll("D6", (uint32_t[]){0}, 1, "1", NULL, 1);
    expect_roll("D6", (uint32_t[]){5}, 1, "6", NULL, 1);
    expect_roll("D6", (uint32_t[]){6}, 1, "1", NULL, 1);
    expect_roll("D6", (uint32_t[]){4294967291u}, 1, "6", NULL, 1);
    expect_roll("D6", (uint32_t[]){4294967292u, 2}, 2, "3", NULL, 2);
    expect_roll("D6", (uint32_t[]){4294967295u, 4294967294u, 0}, 3, "1", NULL, 3);

    // 2^32 mod 100 is 96.
    expect_roll("D100", (uint32_t[]){99}, 1, "100", NULL, 1);
    expect_roll("D100", (uint32_t[]){4294967199u}, 1, "100", NULL, 1);
    expect_roll("D100", (uint32_t[]){4294967200u, 0}, 2, "1", NULL, 2);

    // D66 reads two d6 as tens and units, in that order.
    expect_roll("D66", (uint32_t[]){0, 0}, 2, "11", NULL, 2);
    expect_roll("D66", (uint32_t[]){0, 5}, 2, "16", NULL, 2);
    expect_roll("D66", (uint32_t[]){5, 0}, 2, "61", NULL, 2);
    expect_roll("D66", (uint32_t[]){4294967293u, 5, 5}, 3, "66", NULL, 3);

    /*
     * The oracle rolls one d6 against its table: draw k lands on outcome k,
     * and a draw in the window is taken again.
     */
    for (uint8_t index = 0; index < ORACLE_OUTCOME_COUNT; index++) {
        const roll_t outcome = oracle_outcome(index);
        expect_roll("ORACLE", (uint32_t[]){index}, 1, outcome.answer, outcome.modifier, 1);
    }
    {
        const roll_t last = oracle_outcome((uint8_t)(ORACLE_OUTCOME_COUNT - 1));
        expect_roll("ORACLE", (uint32_t[]){4294967294u, (uint32_t)(ORACLE_OUTCOME_COUNT - 1)}, 2,
                    last.answer, last.modifier, 2);
    }

    check_outcome_table();

    if (failures > 0) {
        fprintf(stderr, "oracle: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("oracle: ok");
    return 0;
}
