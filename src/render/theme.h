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

    // One palette per screen, each key a colour of data/theme.json.
    struct {
        uint16_t wordmark; // PYTHIA//, and the scanline that drifts over it
        uint16_t scramble; // the glyphs a position cycles through
        uint16_t caption;  // DELPHI SYSTEMS
        uint16_t ring;
        uint16_t ring_active; // the comet, and the rule under the wordmark
    } boot;

    struct {
        uint16_t name;
        uint16_t ring;
        uint16_t ring_active;
    } list;

    struct {
        uint16_t text; // the die name along the rim
    } caption;

    struct {
        uint16_t text; // every numeric result, whichever effect brings it
    } numbers;

    struct {
        uint16_t answer;
        uint16_t modifier;
    } oracle;

    struct {
        uint16_t face; // the rim, grooves and highlight are shades of it
    } coin;
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
