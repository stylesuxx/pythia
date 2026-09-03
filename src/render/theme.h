#pragma once

#include <stdint.h>

#include "config.h"
#include "render/font.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A theme owns both the typefaces and the palette. The typefaces of a built-in
 * theme are a row of THEMES; its palette is data/theme.json, embedded into the
 * firmware and parsed when the theme is selected, and a user's theme.json on
 * the drive lays over that.
 *
 * NOTE: a new look is one entry here plus its rows in tools/make_fonts.c.
 */
#define THEME_FIELD(section, stem, key, fallback) uint16_t key;
#define THEME_SECTION(section, LIST)                                                              \
    struct {                                                                                      \
        LIST(THEME_FIELD)                                                                         \
    } section;

typedef struct {
    const char *name;
    const font_t *answer_font; // YES and NO
    const font_t *number_font; // die results
    const font_t *label_font;
    const font_t *caption_font;
    uint32_t coin_faces[2];    // struck on face one and face two of the D2
                               // coin, drawn from number_font

    /*
     * One palette per section of data/theme.json, each key a field: the
     * general roles under colors, then one struct per screen, so a scene
     * reads only its own section.
     */
    CONFIG_SECTIONS(THEME_SECTION)
} theme_t;

extern const theme_t THEMES[];
extern const uint8_t THEME_COUNT;

// The theme in use; the first built-in one until something is selected.
const theme_t *theme_active(void);

// Picks a built-in theme, with the built-in palette, and drops any file laid over it.
void theme_select(uint8_t index);

/**
 * Lays a parsed user file over the selected theme: the colours and name it
 * sets win, the rest stay built-in. theme_active() answers with the result
 * until theme_reset() or the next select.
 */
void theme_apply_file(const config_theme_t *parsed);
void theme_reset(void);

#ifdef __cplusplus
}
#endif
