#pragma once

#include <stdint.h>

#include "render/font.h"
#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * How a numeric result arrives on screen. Every effect brings the same subject
 * to the same rest; what differs is the way there.
 *
 * EFFECTS in effects.c is what the effect setting indexes, so adding one is
 * a file in this directory defining an effect_t and a row in the table.
 *
 * tests/stage.c holds every row to the promises below.
 */

/**
 * A result at rest: the text, the face and colour it is set in, and where it
 * stands. The stage is the band of rows a frame repaints for it, filled with
 * background before the effect draws; an effect must keep every pixel it
 * touches inside.
 */
typedef struct {
    const font_t *font;
    const char *text;
    uint16_t color;
    uint16_t background;
    int left; // pen x, centred on the panel
    int baseline;
    int width;
    frame_rect_t stage;
} effect_subject_t;

typedef struct {
    const char *name;

    /**
     * Rest is reached at this many milliseconds; from then on draw produces
     * the subject plain, whatever the elapsed time.
     */
    uint32_t duration_ms;

    /**
     * Starts a run. The seed differs from roll to roll and carries nothing of
     * the result, so an effect may vary on it freely.
     */
    void (*begin)(const effect_subject_t *subject, uint32_t seed);

    /**
     * Fires the haptic cues. Called every loop rather than every frame, so a
     * dropped frame does not move a thump.
     */
    void (*tick)(uint32_t elapsed);

    void (*draw)(const effect_subject_t *subject, uint32_t elapsed, uint8_t alpha);
} effect_t;

extern const effect_t *const EFFECTS[];
extern const uint8_t EFFECT_COUNT;

/**
 * Index into EFFECTS by name, or the first effect for a name that is not in
 * the table.
 */
uint8_t effect_index_of(const char *name);

#ifdef __cplusplus
}
#endif
