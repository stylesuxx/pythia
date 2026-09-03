#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Tears rows sideways in slices
 *
 * The flicker the boot wordmark suffers once it
 * has settled. Each step picks a fresh set of slices, so the tear flickers
 * rather than slides, and the same seed and step always pick the same ones,
 * so the preview and the device agree.
 */
typedef struct {
    int top;           // first row a slice may start on
    int span;          // rows below top a slice may start within
    int limit;         // first row no slice may reach
    uint8_t slices;
    uint8_t max_shift; // pixels a slice may move, either way
} glitch_tear_t;

uint32_t glitch_hash(uint32_t position, uint32_t step);
void glitch_tear(const glitch_tear_t *tear, uint32_t seed, uint32_t step, uint16_t fill);

#ifdef __cplusplus
}
#endif
