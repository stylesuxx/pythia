#include "scenes/caption.h"

#include "render/canvas.h"
#include "render/theme.h"

#define CAPTION_TRACKING 5.0f

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
