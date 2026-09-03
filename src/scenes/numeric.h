#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "oracle.h"
#include "scenes/effects/effect.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A numeric result on its way to rest: the digits in the theme's number face,
 * centred on the band the oracle's answer shares, brought there by whichever
 * effect the stage hands over. This is where the subject is built and how
 * long the frames keep coming past the rest; the way there is the effect's.
 */

void numeric_begin(const roll_t *roll, const effect_t *effect, uint32_t now);
void numeric_draw(uint32_t now, uint8_t alpha);
bool numeric_is_animating(uint32_t now);

// The rows numeric_draw() draws into, so a frame can repaint just that band.
frame_rect_t numeric_stage(void);

/**
 * Drives the effect's haptic cues. Call every loop, not once per rendered
 * frame, so a cue stays on time even when a frame is skipped.
 */
void numeric_tick(uint32_t now);

#ifdef __cplusplus
}
#endif
