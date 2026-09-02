#include "frame.h"

#include <stddef.h>

#include "canvas.h"

#define FRAME_INTERVAL_MS 16

// The marked extent, as a half-open row range. Empty when top >= bottom.
static int dirty_top = 0;
static int dirty_bottom = 0;
static uint32_t last_frame_ms = 0;

static bool is_marked(void) {
    return dirty_top < dirty_bottom;
}

void frame_begin(uint32_t now) {
    dirty_top = 0;
    dirty_bottom = 0;
    last_frame_ms = now - FRAME_INTERVAL_MS;
}

void frame_mark(frame_rows_t rows) {
    int top = rows.top < 0 ? 0 : rows.top;
    int bottom = rows.top + rows.height;
    if (bottom > CANVAS_HEIGHT) {
        bottom = CANVAS_HEIGHT;
    }

    if (top >= bottom) {
        return;
    }

    if (!is_marked()) {
        dirty_top = top;
        dirty_bottom = bottom;
        return;
    }

    if (top < dirty_top) {
        dirty_top = top;
    }

    if (bottom > dirty_bottom) {
        dirty_bottom = bottom;
    }
}

void frame_mark_whole(void) {
    frame_mark((frame_rows_t){0, CANVAS_HEIGHT});
}

frame_rows_t frame_render(uint32_t now, uint16_t background,
                          void (*draw)(void *context, frame_rows_t rows), void *context) {
    const frame_rows_t nothing = {0, 0};
    if (!is_marked() || (now - last_frame_ms) < FRAME_INTERVAL_MS) {
        return nothing;
    }

    const frame_rows_t rows = {dirty_top, dirty_bottom - dirty_top};
    dirty_top = 0;
    dirty_bottom = 0;
    last_frame_ms = now;

    canvas_fill_rows(rows.top, rows.height, background);
    if (draw != NULL) {
        draw(context, rows);
    }

    return rows;
}
