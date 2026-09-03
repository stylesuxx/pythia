#include "dice.h"

#include <stdio.h>
#include <string.h>

#include "builtin_files.h"
#include "config.h"
#include "render/font.h"
#include "render/theme.h"
#include "scenes/caption.h"

/*
 * The built-in table, data/layout.json laid in once at first use; and the
 * table in use, which is the built-in one or the user's file laid in whole.
 */
static bool initialised = false;
static die_t built_in[DICE_CAPACITY];
static uint8_t built_in_count = 0;
static die_t active[DICE_CAPACITY];
static uint8_t active_count = 0;

// The floor under a build whose embedded layout does not parse.
static const die_t LAST_RESORT = {"ORACLE", DIE_ORACLE, 0, 0};

static void lay_in(die_t *table, uint8_t *count, const config_layout_t *parsed) {
    for (uint8_t index = 0; index < parsed->count; index++) {
        memcpy(table[index].name, parsed->dice[index].name, DIE_NAME_CAPACITY);
        table[index].kind = parsed->dice[index].kind;
        table[index].sides = parsed->dice[index].sides;
        table[index].effect = parsed->dice[index].effect;
    }

    *count = parsed->count;
}

static void initialise(void) {
    if (initialised) {
        return;
    }

    initialised = true;

    /*
     * tests/layout.c holds data/layout.json to parsing in full and the
     * firmware embeds those bytes, so a refusal here is a broken build; the
     * machine still gets one die to stand on.
     */
    const char *text = layout_builtin_text();
    config_layout_t parsed;
    char error[CONFIG_ERROR_CAPACITY];
    if (config_parse_layout(text, strlen(text), &parsed, error, sizeof(error))) {
        lay_in(built_in, &built_in_count, &parsed);
    } else {
        built_in[0] = LAST_RESORT;
        built_in_count = 1;
    }

    memcpy(active, built_in, sizeof(active));
    active_count = built_in_count;
}

const die_t *dice_active(void) {
    initialise();
    return active;
}

uint8_t dice_count(void) {
    initialise();
    return active_count;
}

uint8_t dice_index_of(const char *name) {
    initialise();

    uint8_t oracle = 0;
    bool has_oracle = false;
    for (uint8_t index = 0; index < active_count; index++) {
        if (strcmp(active[index].name, name) == 0) {
            return index;
        }

        if (!has_oracle && active[index].kind == DIE_ORACLE) {
            oracle = index;
            has_oracle = true;
        }
    }

    return oracle;
}

static bool has_every_glyph(const font_t *font, const char *text, char *missing) {
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (font_find_glyph(font, (uint8_t)*cursor) == NULL) {
            *missing = *cursor;
            return false;
        }
    }

    return true;
}

bool dice_check_drawable(const config_layout_t *parsed, const theme_t *theme, char *error,
                         size_t error_capacity) {
    for (uint8_t index = 0; index < parsed->count; index++) {
        char missing;
        if (!has_every_glyph(theme->label_font, parsed->dice[index].name, &missing)) {
            snprintf(error, error_capacity, "dice[%u].name: '%c' is not in the label face",
                     (unsigned)index, missing);
            return false;
        }

        if (!has_every_glyph(theme->caption_font, parsed->dice[index].name, &missing)) {
            snprintf(error, error_capacity, "dice[%u].name: '%c' is not in the caption face",
                     (unsigned)index, missing);
            return false;
        }

        if (!caption_fits(parsed->dice[index].name)) {
            snprintf(error, error_capacity, "dice[%u].name: too wide for the rim", (unsigned)index);
            return false;
        }
    }

    return true;
}

void dice_apply_file(const config_layout_t *parsed) {
    initialise();
    if (parsed == NULL) {
        memcpy(active, built_in, sizeof(active));
        active_count = built_in_count;
        return;
    }

    lay_in(active, &active_count, parsed);
}
