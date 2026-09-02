#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// What reaches the panel. Callers mark the rows they are about to change;
// one call then fills those rows, hands the canvas to the caller's draw, and
// reports the rows to push. Frames are paced here, so marks made between two
// frames accumulate and are drawn together when the next one is due.

// A run of canvas rows. A height of zero is no rows at all.
typedef struct {
    int top;
    int height;
} frame_rows_t;

// Forgets any marks and lets the next frame run at once.
void frame_begin(uint32_t now);

// Adds rows to the extent the next frame will redraw. Rows outside the canvas
// are dropped.
void frame_mark(frame_rows_t rows);
void frame_mark_whole(void);

// Runs a frame if anything is marked and the frame interval has passed since
// the last one: fills the marked rows with background, calls draw with those
// rows, clears the marks and returns them. Otherwise returns no rows and keeps
// the marks for the next call.
frame_rows_t frame_render(uint32_t now, uint16_t background,
                          void (*draw)(void *context, frame_rows_t rows), void *context);

#ifdef __cplusplus
}
#endif
