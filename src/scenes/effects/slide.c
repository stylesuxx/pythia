/*
 * The number slides in from beyond the left edge and decelerates hard onto its
 * rest, the same entry the oracle's answer makes.
 */

#include <math.h>
#include <stdbool.h>

#include "render/canvas.h"
#include "scenes/effects/effect.h"
#include "haptics.h"
#include "render/theme.h"

#define SLIDE_MS 380
#define ENTRY_GAP 40

// Offset so the haptic lands with the glyph rather than after it.
#define THUMP_MS (SLIDE_MS - 60)

static float entry_x = 0.0f;
static bool thumped = false;

// Hard deceleration, so the number arrives with weight instead of drifting in.
static float ease_out_quint(float t) {
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

static void slide_begin(const effect_subject_t *subject, uint32_t seed) {
    (void)seed;
    entry_x = -(float)(subject->width + ENTRY_GAP);
    thumped = false;
}

static void slide_tick(uint32_t elapsed) {
    if (!thumped && elapsed >= THUMP_MS) {
        thumped = true;
        haptics_play(HAPTIC_ANSWER);
    }
}

static void slide_draw(const effect_subject_t *subject, uint32_t elapsed, uint8_t alpha) {
    float x = (float)subject->left;
    if (elapsed < SLIDE_MS) {
        const float progress = ease_out_quint((float)elapsed / (float)SLIDE_MS);
        x = entry_x + ((float)subject->left - entry_x) * progress;
    }

    canvas_text(subject->font, subject->text, (int)lroundf(x), subject->baseline,
                theme_active()->answer, alpha);
}

const effect_t EFFECT_SLIDE = {
    .name = "slide",
    .duration_ms = SLIDE_MS,
    .begin = slide_begin,
    .tick = slide_tick,
    .draw = slide_draw,
};
