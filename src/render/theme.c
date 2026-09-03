#include "render/theme.h"

#include <stdbool.h>
#include <stddef.h>
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

/*
 * Where each colour of the file lands. The general roles have no slot of
 * their own: they reach the screen only as the fallback of a section key.
 */
static uint16_t *slot(theme_t *theme, config_color_t which) {
    uint16_t *target = NULL;

    switch (which) {
        case CONFIG_BACKGROUND: {
            target = &theme->background;
        } break;

        case CONFIG_BOOT_WORDMARK: {
            target = &theme->boot.wordmark;
        } break;

        case CONFIG_BOOT_SCRAMBLE: {
            target = &theme->boot.scramble;
        } break;

        case CONFIG_BOOT_CAPTION: {
            target = &theme->boot.caption;
        } break;

        case CONFIG_BOOT_RING: {
            target = &theme->boot.ring;
        } break;

        case CONFIG_BOOT_RING_ACTIVE: {
            target = &theme->boot.ring_active;
        } break;

        case CONFIG_LIST_NAME: {
            target = &theme->list.name;
        } break;

        case CONFIG_LIST_RING: {
            target = &theme->list.ring;
        } break;

        case CONFIG_LIST_RING_ACTIVE: {
            target = &theme->list.ring_active;
        } break;

        case CONFIG_CAPTION_TEXT: {
            target = &theme->caption.text;
        } break;

        case CONFIG_NUMBERS_TEXT: {
            target = &theme->numbers.text;
        } break;

        case CONFIG_ORACLE_ANSWER: {
            target = &theme->oracle.answer;
        } break;

        case CONFIG_ORACLE_MODIFIER: {
            target = &theme->oracle.modifier;
        } break;

        case CONFIG_COIN_FACE: {
            target = &theme->coin.face;
        } break;

        case CONFIG_PRIMARY:
        case CONFIG_SECONDARY:
        case CONFIG_MUTED:
        case CONFIG_RING:
        case CONFIG_RING_ACTIVE:
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
        if (target == NULL) {
            continue;
        }

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
