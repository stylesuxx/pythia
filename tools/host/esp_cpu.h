// Host stand-in so the dice code can be built and previewed off-device.
#pragma once

#include <stdint.h>

static inline uint32_t esp_cpu_get_cycle_count(void) {
    return 0;
}
