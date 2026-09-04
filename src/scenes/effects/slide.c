/*
 * The number slides in from beyond the left edge and decelerates hard onto its
 * rest, the same entry the oracle's answer makes.
 */

#include "scenes/effects/slide.h"

#include <math.h>
#include <stdbool.h>

#include "render/canvas.h"
#include "scenes/effects/effect.h"
#include "haptics.h"

// How far beyond the edge the glyphs start.
#define ENTRY_GAP 40

static bool thumped = false;

// Hard deceleration, so the glyphs arrive with weight instead of drifting in.
static float ease_out_quint(float t) {
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

float slide_position(int width, int rest, uint32_t elapsed) {
    if (elapsed >= SLIDE_MS) {
        return (float)rest;
    }

    const float entry = -(float)(width + ENTRY_GAP);
    const float progress = ease_out_quint((float)elapsed / (float)SLIDE_MS);
    return entry + ((float)rest - entry) * progress;
}

static void slide_begin(const effect_subject_t *subject, uint32_t seed) {
    (void)subject;
    (void)seed;
    thumped = false;
}

static void slide_tick(uint32_t elapsed) {
    if (!thumped && elapsed >= SLIDE_THUMP_MS) {
        thumped = true;
        haptics_play(HAPTIC_ANSWER);
    }
}

static void slide_draw(const effect_subject_t *subject, uint32_t elapsed, uint8_t alpha) {
    const float x = slide_position(subject->width, subject->left, elapsed);
    canvas_text(subject->font, subject->text, (int)lroundf(x), subject->baseline, subject->color,
                alpha);
}

const effect_t EFFECT_SLIDE = {
    .name = "slide",
    .duration_ms = SLIDE_MS,
    .begin = slide_begin,
    .tick = slide_tick,
    .draw = slide_draw,
};
