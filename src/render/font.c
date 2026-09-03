#include "render/font.h"

#include <stddef.h>

const glyph_t *font_find_glyph(const font_t *font, uint32_t codepoint) {
    uint16_t low = 0;
    uint16_t high = font->glyph_count;
    while (low < high) {
        uint16_t middle = (uint16_t)(low + (high - low) / 2);
        if (codepoint > font->glyphs[middle].codepoint) {
            low = (uint16_t)(middle + 1);
        } else {
            high = middle;
        }
    }

    if (low < font->glyph_count && codepoint == font->glyphs[low].codepoint) {
        return &font->glyphs[low];
    }

    return NULL;
}

uint8_t font_coverage_at(const font_t *font, const glyph_t *glyph, int x, int y) {
    const uint32_t row_bytes = (glyph->width + 1u) / 2u;
    const uint8_t packed = font->coverage[glyph->coverage_offset + (uint32_t)y * row_bytes + (uint32_t)x / 2u];
    const uint8_t nibble = (x & 1) ? (uint8_t)(packed & 0x0Fu) : (uint8_t)(packed >> 4);

    return (uint8_t)(nibble * 17u);
}

int font_text_width(const font_t *font, const char *text) {
    int width = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const glyph_t *glyph = font_find_glyph(font, (uint8_t)*cursor);
        if (glyph != NULL) {
            width += glyph->advance;
        }
    }

    return width;
}
