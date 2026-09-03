#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * One rasterized glyph. Coverage rows are packed two pixels per byte, high
 * nibble first, each row padded to a whole byte.
 */
typedef struct {
    uint32_t codepoint;
    uint16_t width;
    uint16_t height;
    int16_t left;             // pen to the bitmap's left edge
    int16_t top;              // baseline up to the bitmap's top edge
    int16_t advance;          // pen movement to the next glyph
    uint32_t coverage_offset; // start of this glyph inside font_t::coverage
} glyph_t;

typedef struct {
    uint16_t line_height;
    uint16_t ascent;
    uint16_t glyph_count;
    const glyph_t *glyphs; // ordered by codepoint
    const uint8_t *coverage;
} font_t;

// Returns NULL when the font was built without that character.
const glyph_t *font_find_glyph(const font_t *font, uint32_t codepoint);

// Coverage at a pixel inside the glyph bitmap, expanded to 0-255.
uint8_t font_coverage_at(const font_t *font, const glyph_t *glyph, int x, int y);

// Sum of glyph advances. Characters missing from the font contribute nothing.
int font_text_width(const font_t *font, const char *text);

#ifdef __cplusplus
}
#endif
