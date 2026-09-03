#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"
#include "oracle.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * What a roll puts on screen: the reveal for the oracle and the numeric dice,
 * the coin for D2 (if enabled).
 *
 * The stage picks once, when the roll happens, and everything after goes to
 * what it picked, so only what was begun is ever ticked, drawn or asked for
 * its rows.
 */

void stage_configure(bool coin_enabled);
void stage_begin(const roll_t *roll, uint32_t now);
void stage_draw(uint32_t now, uint8_t alpha);
frame_rect_t stage_get_rect(void);
bool stage_is_animating(uint32_t now);

/**
 * Fires the haptic cues of what is on stage.
 *
 * Call every loop rather than every frame, so a dropped frame does not move a
 * thump.
 */
void stage_tick(uint32_t now);

/**
 * True when the next roll goes straight to what is on stage. The coin is spun
 * again from where it lies instead of fading the stage out first.
 */
bool stage_is_rerolled_in_place(void);

#ifdef __cplusplus
}
#endif
