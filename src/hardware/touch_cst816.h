#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    bool pressed;
    uint16_t x;
    uint16_t y;
} touch_t;

void touch_begin(void);
touch_t touch_read(void);

#ifdef __cplusplus
}
#endif
