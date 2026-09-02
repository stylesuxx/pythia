#include "theme.h"

#include "canvas.h"
#include "generated/fonts.h"

const theme_t THEMES[] = {
    {
        .name = "midnight",
        .answer_font = &font_midnight_answer,
        .number_font = &font_midnight_number,
        .label_font = &font_midnight_label,
        .caption_font = &font_midnight_caption,
        .background = CANVAS_RGB(8, 5, 18),
        .answer = CANVAS_RGB(255, 206, 110),
        .modifier = CANVAS_RGB(255, 236, 190),
        .label = CANVAS_RGB(146, 118, 214),
        .ring = CANVAS_RGB(52, 42, 88),
        .ring_active = CANVAS_RGB(255, 206, 110),
    },
    {
        .name = "parchment",
        .answer_font = &font_parchment_answer,
        .number_font = &font_parchment_number,
        .label_font = &font_parchment_label,
        .caption_font = &font_parchment_caption,
        .background = CANVAS_RGB(228, 216, 188),
        .answer = CANVAS_RGB(38, 28, 20),
        .modifier = CANVAS_RGB(120, 44, 32),
        .label = CANVAS_RGB(128, 104, 72),
        .ring = CANVAS_RGB(198, 184, 154),
        .ring_active = CANVAS_RGB(120, 44, 32),
    },
};

const uint8_t THEME_COUNT = (uint8_t)(sizeof(THEMES) / sizeof(THEMES[0]));

static uint8_t active_index = 0;

const theme_t *theme_active(void) {
    return &THEMES[active_index];
}

void theme_select(uint8_t index) {
    if (index < THEME_COUNT) {
        active_index = index;
    }
}
