#pragma once

#include <stdint.h>

#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

// A theme owns both the typefaces and the palette, so a new look is one entry
// here plus its rows in tools/make_fonts.c.
typedef struct {
    const char *name;
    const font_t *answer_font; // YES and NO
    const font_t *number_font; // die results
    const font_t *label_font;
    const font_t *caption_font;
    uint16_t background;
    uint16_t answer;
    uint16_t modifier;
    uint16_t label;
    uint16_t ring;
    uint16_t ring_active;
} theme_t;

extern const theme_t THEMES[];
extern const uint8_t THEME_COUNT;

const theme_t *theme_active(void);
void theme_select(uint8_t index);

#ifdef __cplusplus
}
#endif
