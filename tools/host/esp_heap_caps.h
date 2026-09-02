// Host stand-in so the rendering code can be built and previewed off-device.
#pragma once

#include <stdlib.h>

#define MALLOC_CAP_SPIRAM 0
#define MALLOC_CAP_DMA 0

static inline void *heap_caps_malloc(size_t size, unsigned capabilities) {
    (void)capabilities;
    return malloc(size);
}
