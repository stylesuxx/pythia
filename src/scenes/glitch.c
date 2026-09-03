#include "scenes/glitch.h"

#include "render/canvas.h"

#define SLICE_MIN_ROWS 3
#define SLICE_ROW_RANGE 12

uint32_t glitch_hash(uint32_t position, uint32_t step) {
    uint32_t value = position * 2654435761u ^ (step + 1) * 40503u;
    value ^= value >> 13;
    value *= 0x5bd1e995u;
    value ^= value >> 15;

    return value;
}

void glitch_tear(const glitch_tear_t *tear, uint32_t seed, uint32_t step, uint16_t fill) {
    if (tear->span <= 0) {
        return;
    }

    const uint32_t shift_range = 2u * tear->max_shift + 1u;
    for (uint32_t slice = 0; slice < tear->slices; slice++) {
        const uint32_t draw = glitch_hash(seed + 100 + slice, step);
        const int slice_top = tear->top + (int)(draw % (uint32_t)tear->span);
        int slice_height = SLICE_MIN_ROWS + (int)((draw >> 8) % SLICE_ROW_RANGE);
        if (slice_top + slice_height > tear->limit) {
            slice_height = tear->limit - slice_top;
        }

        const int delta = (int)((draw >> 16) % shift_range) - tear->max_shift;
        canvas_shift_rows(slice_top, slice_height, delta, fill);
    }
}
