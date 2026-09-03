#pragma once

#include <stdint.h>

#include "frame.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The rim caption: the die's name curved along the bottom of the panel. It
 * stays on screen while results come and go, so it reports the rows it
 * occupies and a frame draws it whenever those rows are in the frame; a
 * roll's band lies clear of them and never disturbs it. tests/caption.c
 * holds both promises.
 */

// Baseline circle for text along the rim, measured from the panel centre.
#define CAPTION_RADIUS 166.0f
#define CAPTION_ANGLE 1.5707963f // bottom of the panel

void caption_draw(const char *text, uint8_t alpha);

frame_rect_t caption_get_rect(void);

#ifdef __cplusplus
}
#endif
