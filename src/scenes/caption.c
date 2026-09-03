#include "scenes/caption.h"

#include <math.h>

#include "render/canvas.h"
#include "render/theme.h"

#define CAPTION_TRACKING 5.0f

// Rows a glyph's corner and the bilinear sampling may spill past its ascent.
#define CAPTION_FIT_MARGIN 3.0f

/**
 * The rows the caption's glyphs reach in the widest name of every theme, with
 * a margin; tests/caption.c measures every name against it.
 */
#define CAPTION_TOP 304
#define CAPTION_HEIGHT 48

void caption_draw(const char *text, uint8_t alpha) {
    const theme_t *theme = theme_active();
    canvas_text_arc(theme->caption_font, text, CANVAS_WIDTH / 2.0f, CANVAS_HEIGHT / 2.0f,
                    CAPTION_RADIUS, CAPTION_ANGLE, CAPTION_TRACKING, theme->caption.text, alpha);
}

frame_rect_t caption_get_rect(void) {
    return (frame_rect_t){CAPTION_TOP, CAPTION_HEIGHT, 0, CANVAS_WIDTH};
}

/*
 * Where a glyph's top corners land when it is laid on the rim: the glyph is
 * rotated about the point where its pen sits on the baseline circle, so a
 * corner is that point moved inward by the glyph's top and along the tangent
 * by its half width. The screen row of the corner nearer the top of the
 * panel is what has to stay inside the caption's rows.
 */
static float corner_row(const glyph_t *glyph, float angle_from_bottom) {
    const float inward = CAPTION_RADIUS - (float)glyph->top;
    const float along = (float)glyph->width / 2.0f;
    return CANVAS_HEIGHT / 2.0f + inward * cosf(angle_from_bottom) -
           along * sinf(angle_from_bottom);
}

/*
 * The run is centred at the bottom of the rim, so its highest ink is a top
 * corner of its first or last glyph. tests/caption.c draws the widest run
 * this accepts and holds it inside the rect.
 */
bool caption_fits(const char *text) {
    const font_t *font = theme_active()->caption_font;
    const glyph_t *first = NULL;
    const glyph_t *last = NULL;
    int glyphs = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        const glyph_t *glyph = font_find_glyph(font, (uint8_t)*cursor);
        if (glyph == NULL) {
            continue;
        }

        if (first == NULL) {
            first = glyph;
        }

        last = glyph;
        glyphs++;
    }

    if (glyphs == 0) {
        return true;
    }

    const float arc = (float)font_text_width(font, text) + CAPTION_TRACKING * (float)(glyphs - 1);
    const float half_angle = arc / (2.0f * CAPTION_RADIUS);
    const float first_centre = half_angle - (float)first->advance / (2.0f * CAPTION_RADIUS);
    const float last_centre = half_angle - (float)last->advance / (2.0f * CAPTION_RADIUS);
    const float highest = fminf(corner_row(first, first_centre), corner_row(last, last_centre));
    return highest - CAPTION_FIT_MARGIN >= (float)CAPTION_TOP;
}
