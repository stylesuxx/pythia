#pragma once

#include <stdint.h>

#include "config.h"
#include "render/font.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * A theme owns both the typefaces and the palette. The built-in typefaces are
 * compiled in; the palette is data/theme.json, embedded into the firmware and
 * parsed at first use, and a user's theme.json on the drive lays over that.
 *
 * NOTE: the built-in faces are the rows in tools/make_fonts.c.
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

// The theme in use: the built-in look until a file is laid over it.
const theme_t *theme_active(void);

/**
 * Lays a parsed user file over the built-in look: the colours and name it
 * sets win, the rest stay built-in. NULL restores the built-in look. Either
 * way theme_active() answers with the result from then on.
 */
void theme_apply_file(const config_theme_t *parsed);

#ifdef __cplusplus
}
#endif
