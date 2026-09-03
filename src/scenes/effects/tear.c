/*
 * The number cuts in at its rest and tears sideways for a moment, the way the
 * boot wordmark does once PYTHIA// has settled, then stands clean. It is
 * there in full from the first frame; the tear is a signal locking on.
 */

#include <stdbool.h>
#include <stddef.h>

#include "render/canvas.h"
#include "scenes/effects/effect.h"
#include "scenes/glitch.h"
#include "haptics.h"
#include "render/theme.h"

/**
 * The boot's own tear at its own pace, with the shift scaled from the
 * wordmark's 54 px face to the 120 px digits so a slice moves the same
 * fraction of a glyph.
 */
#define TEAR_MS 220
#define TEAR_STEP_MS 40
#define TEAR_SLICES 4
#define TEAR_SHIFT 40

static glitch_tear_t tear;
static uint32_t tear_seed = 0;
static bool thumped = false;

/**
 * Rows the text's ink covers: from the tallest glyph's top down to the lowest
 * glyph's bottom. Slices start within these rows, so the tear cuts the digits
 * rather than the blank stage around them, and none reaches past the stage.
 */
static void find_ink_rows(const effect_subject_t *subject, int *top, int *bottom) {
    int ascent = 0;
    int descent = 0;
    for (const char *character = subject->text; *character != '\0'; character++) {
        const glyph_t *glyph = font_find_glyph(subject->font, (uint32_t)(unsigned char)*character);
        if (glyph == NULL) {
            continue;
        }

        if (glyph->top > ascent) {
            ascent = glyph->top;
        }

        if (glyph->height - glyph->top > descent) {
            descent = glyph->height - glyph->top;
        }
    }

    *top = subject->baseline - ascent;
    *bottom = subject->baseline + descent;
}

static void tear_begin(const effect_subject_t *subject, uint32_t seed) {
    int ink_top;
    int ink_bottom;
    find_ink_rows(subject, &ink_top, &ink_bottom);

    const int stage_end = subject->stage.top + subject->stage.height;
    if (ink_top < subject->stage.top) {
        ink_top = subject->stage.top;
    }

    if (ink_bottom > stage_end) {
        ink_bottom = stage_end;
    }

    tear.top = ink_top;
    tear.span = ink_bottom - ink_top;
    tear.limit = stage_end;
    tear.slices = TEAR_SLICES;
    tear.max_shift = TEAR_SHIFT;
    tear_seed = seed;
    thumped = false;
}

static void tear_tick(uint32_t elapsed) {
    (void)elapsed;
    if (!thumped) {
        thumped = true;
        haptics_play(HAPTIC_MODIFIER);
    }
}

static void tear_draw(const effect_subject_t *subject, uint32_t elapsed, uint8_t alpha) {
    const theme_t *theme = theme_active();
    canvas_text(subject->font, subject->text, subject->left, subject->baseline, theme->answer,
                alpha);

    if (elapsed < TEAR_MS) {
        glitch_tear(&tear, tear_seed, elapsed / TEAR_STEP_MS, theme->background);
    }
}

const effect_t EFFECT_TEAR = {
    .name = "tear",
    .duration_ms = TEAR_MS,
    .begin = tear_begin,
    .tick = tear_tick,
    .draw = tear_draw,
};
