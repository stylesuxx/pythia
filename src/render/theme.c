#include "render/theme.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "config.h"
#include "render/generated/fonts.h"
#include "theme_file.h"

// The built-in typefaces; the palette comes from data/theme.json.
static const theme_t BUILT_IN_FACES = {
    .name = "midnight",
    .answer_font = &font_midnight_answer,
    .number_font = &font_midnight_number,
    .label_font = &font_midnight_label,
    .caption_font = &font_midnight_caption,
    .coin_faces = {'1', '2'},
};

/*
 * The built-in look, the faces with data/theme.json laid on, resolved once at
 * first use; and the look in use, which is that with the user's file laid
 * over it. Each keeps its own copy of the name it was given, since theme_t
 * only points at one.
 */
static bool initialised = false;
static theme_t built_in;
static char built_in_name[CONFIG_NAME_CAPACITY];
static theme_t active;
static char active_name[CONFIG_NAME_CAPACITY];

// Where each colour of the file lands: the field its row names.
#define THEME_SLOT_CASE(section, stem, key, fallback)                                             \
    case CONFIG_##stem: {                                                                         \
        target = &theme->section.key;                                                             \
    } break;
#define THEME_SLOT_SECTION(section, LIST) LIST(THEME_SLOT_CASE)

static uint16_t *slot(theme_t *theme, config_color_t which) {
    uint16_t *target = NULL;

    switch (which) {
        CONFIG_SECTIONS(THEME_SLOT_SECTION)

        case CONFIG_COLOR_COUNT: {
            target = NULL;
        } break;
    }

    return target;
}

/*
 * A section key the file sets wins; one it leaves out follows the file's own
 * general role when that is set, so a file naming only "primary" recolours
 * every screen that role feeds; and what the file says nothing about keeps
 * the value theme already had.
 */
static void lay_over(theme_t *theme, char *name, const config_theme_t *parsed) {
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        uint16_t *const target = slot(theme, (config_color_t)which);
        const config_color_t fallback = CONFIG_COLORS[which].fallback;
        if (parsed->has_color[which]) {
            *target = parsed->color[which];
        } else if (parsed->has_color[fallback]) {
            *target = parsed->color[fallback];
        }
    }

    if (parsed->name[0] != '\0') {
        memcpy(name, parsed->name, CONFIG_NAME_CAPACITY);
        theme->name = name;
    }
}

static void initialise(void) {
    if (initialised) {
        return;
    }

    initialised = true;
    built_in = BUILT_IN_FACES;

    /*
     * tests/config.c holds data/theme.json to parsing in full and the firmware
     * embeds those bytes, so a refusal here is a broken build; the faces still
     * stand, over an unset palette.
     */
    const char *text = theme_builtin_text();
    config_theme_t parsed;
    char error[CONFIG_ERROR_CAPACITY];
    if (config_parse_theme(text, strlen(text), &parsed, error, sizeof(error))) {
        lay_over(&built_in, built_in_name, &parsed);
    }

    active = built_in;
}

const theme_t *theme_active(void) {
    initialise();
    return &active;
}

void theme_apply_file(const config_theme_t *parsed) {
    initialise();
    active = built_in;
    if (parsed != NULL) {
        lay_over(&active, active_name, parsed);
    }
}
