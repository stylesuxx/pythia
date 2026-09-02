// Host stand-in so the dice code can be built and previewed off-device.
#pragma once

#include <stdint.h>
#include <stdlib.h>

static inline uint32_t esp_random(void) {
    return ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 6) ^ (uint32_t)rand();
}
