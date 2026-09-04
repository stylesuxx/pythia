#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The entry the oracle's answer and the slide effect share: in from beyond
 * the left edge, decelerating hard onto the rest. The effect is the row in
 * EFFECTS; this is the motion itself, so the answer's first beat and a
 * number's arrival are one implementation.
 */

// Rest is reached at this many milliseconds.
#define SLIDE_MS 380

// Offset so the haptic lands with the glyph rather than after it.
#define SLIDE_THUMP_MS (SLIDE_MS - 60)

/**
 * Where a run of glyphs that wide stands at elapsed milliseconds: beyond the
 * left edge at 0, at rest from SLIDE_MS on.
 */
float slide_position(int width, int rest, uint32_t elapsed);

#ifdef __cplusplus
}
#endif
