#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "oracle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The oracle's two-beat reveal: the answer slides in and lands centred, and
 * after a fixed hold the modifier, if there is one, strikes from the right.
 * It is given oracle rolls only; the stage sends every other kind elsewhere.
 */

void reveal_begin(const roll_t *roll, uint32_t now);
void reveal_draw(uint32_t now, uint8_t alpha);
bool reveal_is_animating(uint32_t now);

/**
 * The rows reveal_draw() draws into, so a frame can repaint just that band.
 * It stops above the rim caption, which is why a roll never disturbs it;
 * tests/reveal.c and tests/caption.c hold those promises.
 */
frame_rect_t reveal_stage(void);

/**
 * Drives the haptic beats. Call every loop, not once per rendered frame, so the
 * thumps stay on time even when a frame is skipped.
 */
void reveal_tick(uint32_t now);

/**
 * True while nothing drawn or felt so far could tell whether a modifier is
 * coming. Up to the instant this turns false, every frame and every haptic
 * cue is identical across the outcomes that share an answer.
 */
bool reveal_is_concealed(uint32_t now);

#ifdef __cplusplus
}
#endif
