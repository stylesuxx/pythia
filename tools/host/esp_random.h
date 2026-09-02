// Host stand-in so the dice code can be built and previewed off-device. The
// definition lives in adapters.c and is weak, so a test can script the draws.
#pragma once
#include <stdint.h>
uint32_t esp_random(void);
