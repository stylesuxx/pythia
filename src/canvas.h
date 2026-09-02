#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "font.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CANVAS_WIDTH 360
#define CANVAS_HEIGHT 360

// Pixels are native-endian RGB565. The panel byte order is applied on present.
// A macro so themes can build their palettes at compile time.
#define CANVAS_RGB(red, green, blue) \
    ((uint16_t)((((red) & 0xF8u) << 8) | (((green) & 0xFCu) << 3) | ((blue) >> 3)))

// Allocates the framebuffer in PSRAM. Returns false if PSRAM is unavailable.
bool canvas_begin(void);

uint16_t *canvas_pixels(void);

void canvas_fill(uint16_t color);

void canvas_fill_rows(int top, int height, uint16_t color);

void canvas_blend(int x, int y, uint16_t color, uint8_t alpha);

// Anti-aliased line with round caps, so meeting edges join without a notch.
void canvas_line(float from_x, float from_y, float to_x, float to_y, float width,
                 uint16_t color, uint8_t alpha);

// Anti-aliased arc of a circle, from_angle measured in radians clockwise from
// the positive x axis, sweeping clockwise by sweep. Radial edges are
// anti-aliased; the two angular ends are cut square. A sweep of 2*M_PI or more
// draws the whole ring.
void canvas_arc(float centre_x, float centre_y, float radius, float from_angle, float sweep,
                float width, uint16_t color, uint8_t alpha);

// The same arc with alpha running linearly from alpha_from at from_angle to
// alpha_to at the far end, for tails that dim towards where they have been.
void canvas_arc_gradient(float centre_x, float centre_y, float radius, float from_angle,
                         float sweep, float width, uint16_t color, uint8_t alpha_from,
                         uint8_t alpha_to);

// Slides the given rows sideways by delta_x pixels, filling the vacated edge.
void canvas_shift_rows(int top, int height, int delta_x, uint16_t fill);

// Draws with the pen starting at left_x, sitting on baseline_y.
void canvas_text(const font_t *font, const char *text, int left_x, int baseline_y,
                 uint16_t color, uint8_t alpha);

// Draws along a circle, each glyph rotated so its baseline stays tangent. The
// run is centred on centre_angle, measured in radians clockwise from the
// positive x axis, so M_PI/2 puts the text upright along the bottom rim.
// tracking adds pixels between glyphs, measured along the arc.
void canvas_text_arc(const font_t *font, const char *text, float origin_x, float origin_y,
                     float radius, float centre_angle, float tracking, uint16_t color,
                     uint8_t alpha);

#ifdef __cplusplus
}
#endif
