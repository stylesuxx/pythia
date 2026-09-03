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
typedef struct {
    const char *name;
    const font_t *answer_font; // YES and NO
    const font_t *number_font; // die results
    const font_t *label_font;
    const font_t *caption_font;
    uint32_t coin_faces[2];    // struck on face one and face two of the D2
                               // coin, drawn from number_font
    uint16_t background;
    uint16_t answer;
    uint16_t modifier;
    uint16_t label;
    uint16_t ring;
    uint16_t ring_active;
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
