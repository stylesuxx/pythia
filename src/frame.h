#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What reaches the panel. Callers mark the rows they are about to change;
 * one call then fills those rows, hands the canvas to the caller's draw, and
 * reports the rows to push. Frames are paced here, so marks made between two
 * frames accumulate and are drawn together when the next one is due.
 */

/**
 * A rectangle of canvas. A height or width of zero is nothing at all.
 *
 * Columns matter as much as rows here. The panel scans its own memory out to
 * the glass on its own schedule and this board leaves the ST77916's tearing
 * signal unconnected, so there is no way to write in step with it. What is
 * left is to write as little as possible: a narrow rectangle crosses the scan
 * quickly, where a full-width one travels at nearly the scan's own speed and
 * leaves the seam standing still.
 */
typedef struct {
    int top;
    int height;
    int left;
    int width;
} frame_rect_t;

// Forgets any marks and lets the next frame run at once.
void frame_begin(uint32_t now);

/**
 * Adds a rectangle to the extent the next frame will redraw. Anything outside
 * the canvas is clipped away.
 */
void frame_mark(frame_rect_t rows);

// True when the two rects share at least one pixel; touching edges do not.
bool frame_rect_is_overlapping(frame_rect_t a, frame_rect_t b);
void frame_mark_whole(void);

/**
 * Runs a frame if anything is marked and the frame interval has passed since
 * the last one: fills the marked rows with background, calls draw with those
 * rows, clears the marks and returns them. Otherwise returns no rows and keeps
 * the marks for the next call.
 */
frame_rect_t frame_render(uint32_t now, uint16_t background,
                          void (*draw)(void *context, frame_rect_t rows), void *context);

#ifdef __cplusplus
}
#endif
