/*
 * The coin, driven through its angle. A flip has to arrive square on the face
 * that was rolled: a spin that stopped wherever it liked and had its face read
 * off the screen would bias the die towards whichever face the easing favours.
 */

#include <math.h>
#include <stdio.h>

#include "canvas.h"
#include "coin.h"
#include "theme.h"

#define STEP_MS 4
#define FLIP_LIMIT_MS 4000

static int failures = 0;

#define EXPECT(condition, ...)                                                                    \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            fputs("FAIL: ", stderr);                                                              \
            fprintf(stderr, __VA_ARGS__);                                                         \
            fputc('\n', stderr);                                                                  \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

// Runs a flip out and returns the instant it stopped moving.
static uint32_t flip_to_rest(uint8_t face, uint32_t start) {
    coin_flip(face, start);
    for (uint32_t now = start; now <= start + FLIP_LIMIT_MS; now += STEP_MS) {
        if (!coin_is_flipping(now)) {
            return now;
        }
    }

    return 0;
}

static void check_it_lands_on_the_face_it_was_given(void) {
    for (uint8_t face = 1; face <= 2; face++) {
        const uint32_t rest = flip_to_rest(face, 500);
        EXPECT(rest > 0, "face %u never stopped flipping", (unsigned)face);

        // It rests tilted, not square on, so the thickness stays in view.
        const float facing = coin_facing(rest + 200);
        const float expected = cosf(COIN_REST_TILT);
        EXPECT(fabsf(fabsf(facing) - expected) < 0.01f,
               "face %u came to rest at facing %.3f, expected %.3f", (unsigned)face, facing,
               expected);
        EXPECT(expected < 0.98f, "the resting tilt is too flat to show any thickness");

        // +1 is face one square on, -1 is face two.
        const float wanted = face == 1 ? 1.0f : -1.0f;
        EXPECT(facing * wanted > 0.0f, "face %u rested showing the other side (facing %.3f)",
               (unsigned)face, facing);
    }
}

static void check_the_flip_really_spins(void) {
    const uint32_t start = 500;
    coin_flip(1, start);

    /*
     * Count edge-on crossings: each is a half turn, so three whole turns is at
     * least six of them.
     */
    int crossings = 0;
    float previous = coin_facing(start);
    for (uint32_t now = start; now <= start + FLIP_LIMIT_MS; now += STEP_MS) {
        const float facing = coin_facing(now);
        if ((previous > 0.0f) != (facing > 0.0f)) {
            crossings++;
        }

        previous = facing;
        if (!coin_is_flipping(now)) {
            break;
        }
    }

    EXPECT(crossings >= 6, "the flip only turned through %d half turns", crossings);
}

static void check_the_flip_is_brisk(void) {
    const uint32_t rest = flip_to_rest(2, 500);
    const uint32_t took = rest - 500;

    EXPECT(took <= 1000, "the flip took %u ms, which is too long to feel like a flick",
           (unsigned)took);
}

static void check_a_flip_continues_from_where_it_was(void) {
    coin_flip(1, 0);

    // Interrupt a flip part way, where the coin is not square on.
    const uint32_t mid = 300;
    const float before = coin_facing(mid);
    coin_flip(1, mid);
    const float after = coin_facing(mid);

    EXPECT(fabsf(before - after) < 0.01f, "the flip jumped from %.3f to %.3f", before, after);
}

/**
 * The silhouette must curve. A flat top or bottom means rows of the far rim
 * were never visited, which is what a signed sweep bound did once the coin
 * turned past square on.
 */
static void check_the_outline_curves(void) {
    const uint16_t background = theme_active()->background;

    for (int step = 0; step < 8; step++) {
        // Spread the samples over a turn, avoiding exactly edge on.
        const uint32_t now = 120 + (uint32_t)step * 90;
        coin_flip(1, 0);
        canvas_fill(background);
        coin_draw(now, 255);

        if (fabsf(coin_facing(now)) < 0.25f) {
            continue; // too near edge on for an outline to mean much
        }

        const uint16_t *pixels = canvas_pixels();
        int top_at_centre = -1;
        int top_near_edge = -1;

        for (int y = 0; y < CANVAS_HEIGHT; y++) {
            if (top_at_centre < 0 && pixels[y * CANVAS_WIDTH + CANVAS_WIDTH / 2] != background) {
                top_at_centre = y;
            }

            if (top_near_edge < 0 &&
                pixels[y * CANVAS_WIDTH + CANVAS_WIDTH / 2 + 74] != background) {
                top_near_edge = y;
            }
        }

        EXPECT(top_at_centre >= 0 && top_near_edge >= 0,
               "nothing drawn at step %d", step);
        EXPECT(top_at_centre < top_near_edge,
               "the outline is flat at step %d: centre starts at %d, edge at %d", step,
               top_at_centre, top_near_edge);
    }
}

/**
 * A roll pushes only coin_stage(), so anything drawn outside it reaches the
 * canvas and never reaches the panel.
 */
static void check_it_stays_inside_its_stage(void) {
    const uint16_t background = theme_active()->background;
    const frame_rect_t stage = coin_stage();

    for (int step = 0; step < 12; step++) {
        const uint32_t now = 60 + (uint32_t)step * 60;
        coin_flip(1, 0);
        canvas_fill(background);
        coin_draw(now, 255);

        const uint16_t *pixels = canvas_pixels();
        for (int y = 0; y < CANVAS_HEIGHT; y++) {
            for (int x = 0; x < CANVAS_WIDTH; x++) {
                if (pixels[y * CANVAS_WIDTH + x] == background) {
                    continue;
                }

                EXPECT(y >= stage.top && y < stage.top + stage.height && x >= stage.left &&
                           x < stage.left + stage.width,
                       "step %d drew at %d,%d, outside the stage %d..%d by %d..%d", step, x, y,
                       stage.top, stage.top + stage.height, stage.left,
                       stage.left + stage.width);
                if (failures > 0) {
                    return;
                }
            }
        }
    }
}

int main(void) {
    if (!canvas_begin()) {
        fputs("coin: no framebuffer\n", stderr);
        return 1;
    }

    check_it_lands_on_the_face_it_was_given();
    check_the_flip_really_spins();
    check_the_flip_is_brisk();
    check_a_flip_continues_from_where_it_was();
    check_the_outline_curves();
    check_it_stays_inside_its_stage();

    if (failures > 0) {
        fprintf(stderr, "coin: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("coin: ok");
    return 0;
}
