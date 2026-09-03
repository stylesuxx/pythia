#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DIE_NUMERIC, // 1 to sides
    DIE_COIN,    // 1 or 2, shown as a coin rather than a numeral
    DIE_D66,     // two d6 read as tens and units, 11 to 66
    DIE_ORACLE,  // yes or no, sometimes carrying a modifier
} die_kind_t;

typedef struct {
    const char *name;
    die_kind_t kind;
    uint16_t sides;
} die_t;

extern const die_t DICE[];
extern const uint8_t DIE_COUNT;

typedef struct {
    die_kind_t kind;
    char answer[8];
    const char *modifier; // NULL when the answer stands alone
} roll_t;

// Call once at startup, before the first roll.
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
