#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dice.h"

#ifdef __cplusplus
extern "C" {
#endif


typedef struct {
    die_kind_t kind;
    char answer[8];
    uint8_t value;        // the number rolled; 0 for the oracle
    uint8_t effect;       // the die's, an index into EFFECTS
    const char *modifier; // NULL when the answer stands alone
} roll_t;

void oracle_begin(void);
roll_t roll_die(const die_t *die);

/**
 * The oracle's outcomes, ordered as its d6 table reads from worst to best, as
 * the rolls roll_die() would return for them.
 */
extern const uint8_t ORACLE_OUTCOME_COUNT;
roll_t oracle_outcome(uint8_t index);

#ifdef __cplusplus
}
#endif
