/*
 * The canvas primitives must be symmetric where their geometry is. A ring
 * drawn about the panel centre reads the same in every mirror, and a line
 * drawn one way is the transpose of the same line drawn the other. The rim
 * ring once carried a notch at 12 and 6 o'clock that only a row-by-row
 * mirror would have caught.
 */

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "canvas.h"

#define CENTRE 180.0f
#define BACKGROUND 0x0000
#define INK 0xFFFF

static int failures = 0;

static void fail(const char *what, int x, int y, int mirror_x, int mirror_y) {
    fprintf(stderr, "FAIL: %s: pixel (%d, %d) is 0x%04x but (%d, %d) is 0x%04x\n", what, x, y,
            canvas_pixels()[y * CANVAS_WIDTH + x], mirror_x, mirror_y,
            canvas_pixels()[mirror_y * CANVAS_WIDTH + mirror_x]);
    failures++;
}

/**
 * Pixel centres sit on integer coordinates and the drawing centre on 180.0,
 * so x mirrors to 360 - x. Column 0 has no partner and is skipped.
 */
static void expect_mirror_symmetry(const char *what) {
    const uint16_t *pixels = canvas_pixels();
    for (int y = 1; y < CANVAS_HEIGHT; y++) {
        for (int x = 1; x < CANVAS_WIDTH; x++) {
            const uint16_t pixel = pixels[y * CANVAS_WIDTH + x];
            if (pixel != pixels[y * CANVAS_WIDTH + (CANVAS_WIDTH - x)]) {
                fail(what, x, y, CANVAS_WIDTH - x, y);
                return;
            }
            if (pixel != pixels[(CANVAS_HEIGHT - y) * CANVAS_WIDTH + x]) {
                fail(what, x, y, x, CANVAS_HEIGHT - y);
                return;
            }
            if (pixel != pixels[x * CANVAS_WIDTH + y]) {
                fail(what, x, y, y, x);
                return;
            }
        }
    }
}

static void expect_rows_blank(const char *what, int first, int last) {
    const uint16_t *pixels = canvas_pixels();
    for (int y = first; y <= last; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            if (pixels[y * CANVAS_WIDTH + x] != BACKGROUND) {
                fprintf(stderr, "FAIL: %s: pixel (%d, %d) is lit outside the arc\n", what, x, y);
                failures++;
                return;
            }
        }
    }
}

static int lit_pixels(void) {
    const uint16_t *pixels = canvas_pixels();
    int count = 0;
    for (int index = 0; index < CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        count += pixels[index] != BACKGROUND;
    }
    return count;
}

static void check_full_ring(void) {
    canvas_fill(BACKGROUND);
    canvas_arc(CENTRE, CENTRE, 172.0f, 0.0f, 2.0f * (float)M_PI, 3.0f, INK, 255);
    expect_mirror_symmetry("full ring");
    if (lit_pixels() == 0) {
        fputs("FAIL: full ring drew nothing\n", stderr);
        failures++;
    }
}

/**
 * The bottom half, swept from 3 o'clock through 6 to 9. Nothing above the
 * centre row may light, and the half must mirror left to right.
 */
static void check_half_ring(void) {
    canvas_fill(BACKGROUND);
    canvas_arc(CENTRE, CENTRE, 172.0f, 0.0f, (float)M_PI, 3.0f, INK, 255);
    expect_rows_blank("half ring", 0, 177);

    const uint16_t *pixels = canvas_pixels();
    for (int y = 178; y < CANVAS_HEIGHT; y++) {
        for (int x = 1; x < CANVAS_WIDTH; x++) {
            if (pixels[y * CANVAS_WIDTH + x] != pixels[y * CANVAS_WIDTH + (CANVAS_WIDTH - x)]) {
                fail("half ring", x, y, CANVAS_WIDTH - x, y);
                return;
            }
        }
    }
}

/**
 * A gradient arc's alpha runs between its two ends, and blending is monotone
 * in alpha, so its footprint sits between the footprints of flat arcs drawn at
 * those two alphas.
 */
static void check_gradient_footprint(void) {
    static bool dim[CANVAS_WIDTH * CANVAS_HEIGHT];
    static bool bright[CANVAS_WIDTH * CANVAS_HEIGHT];

    canvas_fill(BACKGROUND);
    canvas_arc(CENTRE, CENTRE, 172.0f, 1.0f, 2.5f, 3.0f, INK, 40);
    for (int index = 0; index < CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        dim[index] = canvas_pixels()[index] != BACKGROUND;
    }
    canvas_fill(BACKGROUND);
    canvas_arc(CENTRE, CENTRE, 172.0f, 1.0f, 2.5f, 3.0f, INK, 255);
    for (int index = 0; index < CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        bright[index] = canvas_pixels()[index] != BACKGROUND;
    }

    canvas_fill(BACKGROUND);
    canvas_arc_gradient(CENTRE, CENTRE, 172.0f, 1.0f, 2.5f, 3.0f, INK, 40, 255);
    const uint16_t *pixels = canvas_pixels();
    for (int index = 0; index < CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        const bool lit = pixels[index] != BACKGROUND;
        if ((dim[index] && !lit) || (lit && !bright[index])) {
            fprintf(stderr, "FAIL: gradient arc footprint at (%d, %d) is outside the flat arcs' range\n",
                    index % CANVAS_WIDTH, index / CANVAS_WIDTH);
            failures++;
            return;
        }
    }
}

// A horizontal line and its vertical twin are transposes of each other.
static void check_line_transpose(void) {
    static uint16_t horizontal[CANVAS_WIDTH * CANVAS_HEIGHT];
    canvas_fill(BACKGROUND);
    canvas_line(100.0f, CENTRE, 260.0f, CENTRE, 3.0f, INK, 255);
    for (int index = 0; index < CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        horizontal[index] = canvas_pixels()[index];
    }

    canvas_fill(BACKGROUND);
    canvas_line(CENTRE, 100.0f, CENTRE, 260.0f, 3.0f, INK, 255);
    const uint16_t *pixels = canvas_pixels();
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            if (pixels[y * CANVAS_WIDTH + x] != horizontal[x * CANVAS_WIDTH + y]) {
                fprintf(stderr, "FAIL: vertical line at (%d, %d) is not the horizontal line's transpose\n",
                        x, y);
                failures++;
                return;
            }
        }
    }
}

int main(void) {
    if (!canvas_begin()) {
        fputs("canvas: no framebuffer\n", stderr);
        return 1;
    }

    check_full_ring();
    check_half_ring();
    check_gradient_footprint();
    check_line_transpose();

    if (failures > 0) {
        fprintf(stderr, "canvas: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }
    puts("canvas: ok");
    return 0;
}
