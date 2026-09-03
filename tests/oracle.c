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
}

/**
 * The table reads worst to best: every NO before any YES, each answer once
 * with each of its three modifiers, no outcome repeated.
 */
static void check_outcome_table(void) {
    bool seen_yes = false;
    for (uint8_t index = 0; index < ORACLE_OUTCOME_COUNT; index++) {
        const roll_t outcome = oracle_outcome(index);
        if (strcmp(outcome.answer, "YES") == 0) {
            seen_yes = true;
        } else if (strcmp(outcome.answer, "NO") != 0) {
            fprintf(stderr, "FAIL: outcome %u answers \"%s\"\n", index, outcome.answer);
            failures++;
        } else if (seen_yes) {
            fprintf(stderr, "FAIL: outcome %u is a NO after a YES\n", index);
            failures++;
        }
        for (uint8_t other = 0; other < index; other++) {
            const roll_t earlier = oracle_outcome(other);
            const bool same_modifier =
                (earlier.modifier == NULL && outcome.modifier == NULL) ||
                (earlier.modifier != NULL && outcome.modifier != NULL &&
                 strcmp(earlier.modifier, outcome.modifier) == 0);
            if (strcmp(earlier.answer, outcome.answer) == 0 && same_modifier) {
                fprintf(stderr, "FAIL: outcomes %u and %u are the same\n", other, index);
                failures++;
            }
        }
    }
    if (ORACLE_OUTCOME_COUNT != 6) {
        fprintf(stderr, "FAIL: the oracle has %u outcomes, a d6 table needs 6\n",
                (unsigned)ORACLE_OUTCOME_COUNT);
        failures++;
    }
}

int main(void) {
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
