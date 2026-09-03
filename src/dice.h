#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The dice the knob offers, in the order the encoder browses them. The
 * built-in table is data/layout.json, embedded into the firmware and laid in
 * at first use; a user's layout.json on the drive replaces it whole.
 */

typedef enum {
    DIE_NUMERIC, // 1 to n sides
    DIE_COIN,    // 1 or 2, shown as a coin rather than a numeral
    DIE_D66,     // two d6 read as tens and units, 11 to 66
    DIE_ORACLE,  // yes or no, sometimes carrying a modifier
} die_kind_t;

// Room for a name; how wide one may be is the rim caption's to say.
#define DIE_NAME_CAPACITY 12

// One tick per die around the rim reads well up to this many.
#define DICE_CAPACITY 16

typedef struct {
    char name[DIE_NAME_CAPACITY];
    die_kind_t kind;
    uint16_t sides;
    uint8_t effect; // how a numeric result arrives; an index into EFFECTS
} die_t;

struct config_layout;
struct theme;

const die_t *dice_active(void);
uint8_t dice_count(void);

/**
 * The first die of that name; when there is none, the first oracle, and when
 * there is no oracle either, the first die.
 */
uint8_t dice_index_of(const char *name);

/**
 * Whether every name in a parsed layout can be drawn: every glyph in the
 * theme's label and caption faces, and the whole name within the rim
 * caption's rows. On refusal returns false with error naming the entry and
 * the glyph, or the width.
 */
bool dice_check_drawable(const struct config_layout *parsed, const struct theme *theme,
                         char *error, size_t error_capacity);

// Lays a parsed user file in as the table; NULL restores the built-in layout.
void dice_apply_file(const struct config_layout *parsed);

#ifdef __cplusplus
}
#endif
