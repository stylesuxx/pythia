/*
 * The rim caption reports the rows it occupies, so a frame can decide by
 * arithmetic whether to draw it. Every pixel of every die name in every theme
 * must fall inside that rect, and the rect must clear both stages, so a roll's
 * band can never erase the caption and a caption draw can never touch a roll.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dice.h"
#include "render/canvas.h"
#include "render/theme.h"
#include "scenes/caption.h"
#include "scenes/coin.h"
#include "scenes/numeric.h"
#include "scenes/reveal.h"

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

static bool is_inside(frame_rect_t rect, int row, int column) {
    return row >= rect.top && row < rect.top + rect.height && column >= rect.left &&
           column < rect.left + rect.width;
}

static bool is_disjoint(frame_rect_t a, frame_rect_t b) {
    const bool rows_apart = a.top + a.height <= b.top || b.top + b.height <= a.top;
    const bool columns_apart = a.left + a.width <= b.left || b.left + b.width <= a.left;
    return rows_apart || columns_apart;
}

static bool name_fits_the_rect(const theme_t *theme, const char *name) {
    const frame_rect_t rect = caption_get_rect();
    const uint16_t *pixels = canvas_pixels();
    canvas_fill(theme->colors.background);
    caption_draw(name, 255);

    for (int row = 0; row < CANVAS_HEIGHT; row++) {
        for (int column = 0; column < CANVAS_WIDTH; column++) {
            if (pixels[row * CANVAS_WIDTH + column] != theme->colors.background &&
                !is_inside(rect, row, column)) {
                EXPECT(false, "%s: %s draws at row %d column %d, outside the caption's rect",
                       theme->name, name, row, column);
                return false;
            }
        }
    }

    return true;
}

// Every inked pixel of every built-in name lies inside the rect the caption reports.
static void check_every_name_fits_the_rect(const theme_t *theme) {
    for (uint8_t index = 0; index < dice_count(); index++) {
        if (!name_fits_the_rect(theme, dice_active()[index].name)) {
            return;
        }
    }
}

/*
 * caption_fits() is what a layout is held to, so the widest run of a wide
 * glyph it accepts must draw inside the rect, and one glyph more must be
 * refused. Every built-in name must pass it.
 */
static void check_the_fit_measure_is_honest(const theme_t *theme) {
    for (uint8_t index = 0; index < dice_count(); index++) {
        EXPECT(caption_fits(dice_active()[index].name), "%s does not fit its own rim",
               dice_active()[index].name);
    }

    char run[DIE_NAME_CAPACITY + 1];
    int accepted = 0;
    for (int length = 1; length <= DIE_NAME_CAPACITY; length++) {
        memset(run, 'D', (size_t)length);
        run[length] = '\0';
        if (!caption_fits(run)) {
            break;
        }

        accepted = length;
    }

    EXPECT(accepted >= 4, "the rim accepts only %d wide glyphs; D100 needs four", accepted);
    EXPECT(accepted < DIE_NAME_CAPACITY, "the measure accepts every length up to the capacity");
    memset(run, 'D', (size_t)accepted);
    run[accepted] = '\0';
    EXPECT(name_fits_the_rect(theme, run), "the widest accepted run, %s, spills out of the rect", run);
    printf("caption: up to %d wide glyphs fit the rim\n", accepted);
}

// The caption's rect and every stage are told apart by numbers, not pixels.
static void check_the_rect_clears_every_stage(void) {
    const frame_rect_t caption = caption_get_rect();
    const frame_rect_t reveal = reveal_stage();
    const frame_rect_t numeric = numeric_stage();
    const frame_rect_t coin = coin_stage();

    EXPECT(is_disjoint(caption, reveal), "the caption's rows %d+%d overlap the reveal's stage %d+%d",
           caption.top, caption.height, reveal.top, reveal.height);
    EXPECT(is_disjoint(caption, numeric),
           "the caption's rows %d+%d overlap the numeric band %d+%d",
           caption.top, caption.height, numeric.top, numeric.height);
    EXPECT(is_disjoint(caption, coin), "the caption's rows %d+%d overlap the coin's stage %d+%d",
           caption.top, caption.height, coin.top, coin.height);
}

int main(void) {
    if (!canvas_begin()) {
        fputs("caption: no framebuffer\n", stderr);
        return 1;
    }

    check_every_name_fits_the_rect(theme_active());
    check_the_fit_measure_is_honest(theme_active());
    check_the_rect_clears_every_stage();

    if (failures > 0) {
        fprintf(stderr, "caption: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    const frame_rect_t rect = caption_get_rect();
    printf("caption: rows %d to %d, clear of every stage\n", rect.top, rect.top + rect.height - 1);
    return 0;
}
