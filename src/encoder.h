#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void encoder_begin(void);

// Clicks turned since the last call, positive clockwise.
int32_t encoder_take_detents(void);

#ifdef __cplusplus
}
#endif
