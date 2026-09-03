#include "scenes/menu.h"

#include <math.h>
#include <stdbool.h>

#include "render/canvas.h"
#include "oracle.h"
#include "render/theme.h"

#define CHOICE_BASELINE 200
#define RING_RADIUS 166.0f
#define RING_TICK_LENGTH 14.0f
#define RING_TICK_WIDTH 3.0f
#define RING_ACTIVE_LENGTH 28.0f
#define RING_ACTIVE_WIDTH 5.0f

void menu_draw(uint8_t selected, uint8_t alpha) {
    const theme_t *theme = theme_active();
    const die_t *die = &DICE[selected];

    const int width = font_text_width(theme->label_font, die->name);
    canvas_text(theme->label_font, die->name, (CANVAS_WIDTH - width) / 2, CHOICE_BASELINE,
                theme->label, alpha);

    // One tick per die around the rim, the current one long and bright.
    const float centre = CANVAS_WIDTH / 2.0f;
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        const bool active = (index == selected);
        const float angle =
            -(float)M_PI / 2.0f + (float)index * 2.0f * (float)M_PI / (float)DIE_COUNT;
        const float cosine = cosf(angle);
        const float sine = sinf(angle);
        const float length = active ? RING_ACTIVE_LENGTH : RING_TICK_LENGTH;

        canvas_line(
            centre + cosine * (RING_RADIUS - length),
            centre + sine * (RING_RADIUS - length),
            centre + cosine * RING_RADIUS, centre + sine * RING_RADIUS,
            active ? RING_ACTIVE_WIDTH : RING_TICK_WIDTH,
            active ? theme->ring_active : theme->ring,
            alpha
        );
    }
}
