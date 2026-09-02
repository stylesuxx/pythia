#include "frame.h"

#include <stddef.h>

#include "canvas.h"

#define FRAME_INTERVAL_MS 16

// The marked extent, as half-open ranges. Empty when top >= bottom.
static int dirty_top = 0;
static int dirty_bottom = 0;
static int dirty_left = 0;
static int dirty_right = 0;
static uint32_t last_frame_ms = 0;

static bool is_marked(void) {
    return dirty_top < dirty_bottom;
}

void frame_begin(uint32_t now) {
    dirty_top = 0;
    dirty_bottom = 0;
    dirty_left = 0;
    dirty_right = 0;
    last_frame_ms = now - FRAME_INTERVAL_MS;
}

void frame_mark(frame_rect_t rect) {
    int top = rect.top < 0 ? 0 : rect.top;
    int bottom = rect.top + rect.height;
    if (bottom > CANVAS_HEIGHT) {
        bottom = CANVAS_HEIGHT;
    }

    int left = rect.left < 0 ? 0 : rect.left;
    int right = rect.left + rect.width;
    if (right > CANVAS_WIDTH) {
        right = CANVAS_WIDTH;
    }

    if (top >= bottom || left >= right) {
        return;
    }

    if (!is_marked()) {
        dirty_top = top;
        dirty_bottom = bottom;
        dirty_left = left;
        dirty_right = right;

        return;
    }

    if (top < dirty_top) {
        dirty_top = top;
    }

    if (bottom > dirty_bottom) {
        dirty_bottom = bottom;
    }

    if (left < dirty_left) {
        dirty_left = left;
    }

    if (right > dirty_right) {
        dirty_right = right;
    }
}

void frame_mark_whole(void) {
    frame_mark((frame_rect_t){0, CANVAS_HEIGHT, 0, CANVAS_WIDTH});
}

frame_rect_t frame_render(uint32_t now, uint16_t background,
                          void (*draw)(void *context, frame_rect_t rows), void *context) {
    const frame_rect_t nothing = {0, 0, 0, 0};
    if (!is_marked() || (now - last_frame_ms) < FRAME_INTERVAL_MS) {
        return nothing;
    }

    const frame_rect_t rows = {
        dirty_top,
        dirty_bottom - dirty_top,
        dirty_left,
        dirty_right - dirty_left
    };
    dirty_top = 0;
    dirty_bottom = 0;
    dirty_left = 0;
    dirty_right = 0;
    last_frame_ms = now;

    canvas_fill_rect(rows.top, rows.height, rows.left, rows.width, background);
    if (draw != NULL) {
        draw(context, rows);
    }

    return rows;
}
