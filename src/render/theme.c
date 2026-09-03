#include "render/theme.h"

#include <stdbool.h>
#include <string.h>

#include "config.h"
#include "render/generated/fonts.h"
#include "theme_file.h"

// The typefaces of each built-in theme; the palette comes from data/theme.json.
const theme_t THEMES[] = {
    {
        .name = "midnight",
        .answer_font = &font_midnight_answer,
        .number_font = &font_midnight_number,
        .label_font = &font_midnight_label,
        .caption_font = &font_midnight_caption,
        .coin_faces = {'1', '2'},
    },
};

const uint8_t THEME_COUNT = (uint8_t)(sizeof(THEMES) / sizeof(THEMES[0]));

/*
 * The selected theme with the built-in palette laid on, and the user's file
 * laid over that when there is one. Each keeps its own copy of the name it
 * was given, since theme_t only points at one.
 */
static bool initialised = false;
static theme_t base;
static char base_name[CONFIG_NAME_CAPACITY];
static theme_t overlay;
static char overlay_name[CONFIG_NAME_CAPACITY];
static bool has_overlay = false;

static void lay_over(theme_t *theme, char *name, const config_theme_t *parsed) {
    uint16_t *const slots[CONFIG_COLOR_COUNT] = {
        &theme->background, &theme->answer, &theme->modifier,
        &theme->label,      &theme->ring,   &theme->ring_active,
    };

    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        if (parsed->has_color[which]) {
            *slots[which] = parsed->color[which];
        }
    }

    if (parsed->name[0] != '\0') {
        memcpy(name, parsed->name, CONFIG_NAME_CAPACITY);
        theme->name = name;
    }
}

void theme_select(uint8_t index) {
    if (index >= THEME_COUNT) {
        return;
    }

    base = THEMES[index];
    has_overlay = false;
    initialised = true;

    const char *text = theme_builtin_text();
    config_theme_t built_in;
    char error[CONFIG_ERROR_CAPACITY];
    if (config_parse_theme(text, strlen(text), &built_in, error, sizeof(error))) {
        lay_over(&base, base_name, &built_in);
    }
}

const theme_t *theme_active(void) {
    if (!initialised) {
        theme_select(0);
    }

    return has_overlay ? &overlay : &base;
}

void theme_apply_file(const config_theme_t *parsed) {
    theme_active();
    overlay = base;
    lay_over(&overlay, overlay_name, parsed);
    has_overlay = true;
}

void theme_reset(void) {
    has_overlay = false;
}
