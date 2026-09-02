#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "oracle.h"

#ifdef __cplusplus
extern "C" {
#endif

// Rows the reveal draws into, so a frame can repaint just this band. The band
// stops well above the rim caption, which is why a roll never disturbs it.
#define REVEAL_STAGE_TOP 124
#define REVEAL_STAGE_HEIGHT 110

void reveal_begin(const roll_t *roll, uint32_t now);

// Die name curved along the bottom rim. This is persistent chrome: it is drawn
// only on whole-screen frames and a roll's band never reaches it, so it holds
// steady while results come and go.
void reveal_draw_caption(const char *text, uint8_t alpha);

// Drives the haptic beats. Call every loop, not once per rendered frame, so the
// thumps stay on time even when a frame is skipped.
void reveal_tick(uint32_t now);

void reveal_draw(uint32_t now, uint8_t alpha);

bool reveal_is_animating(uint32_t now);

#ifdef __cplusplus
}
#endif
