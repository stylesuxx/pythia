#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A coin tumbling about a horizontal axis. The whole animation is one angle
 * driving the foreshortening through a cosine. It only moves when it is
 * thrown: like every other die here, the armed screen shows nothing.
 */

/**
 * Where a flip comes to rest, in radians through the tumble. Deliberately
 * short of square on: the coin keeps its thickness in view instead of lying
 * perfectly flat, which is what makes it read as a solid object at rest.
 */
#define COIN_REST_TILT 0.46f

/**
 * Spins up and lands on face 1 or 2.
 *
 * The face is decided before the animation starts and the animation is built
 * to arrive there; reading a face off a spin that stopped where it liked would
 * bias the die.
 */
void coin_flip(uint8_t face, uint32_t now);

/**
 * How the coin is turned right now, as the cosine of its angle: +1 is face one
 * square on, -1 is face two square on, 0 is edge on. It drives the vertical
 * radius, so this one number is the whole animation, and it is what the tests
 * assert against.
 */
float coin_facing(uint32_t now);

frame_rect_t coin_stage(void);
void coin_draw(uint32_t now, uint8_t alpha);
bool coin_is_flipping(uint32_t now);
bool coin_is_animating(uint32_t now);

#ifdef __cplusplus
}
#endif
