#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Boots that die before the loop runs are counted in the store. After
 * SAFE_MODE_AFTER_ATTEMPTS in a row the next boot is in safe mode, which the
 * shell answers by bringing up the USB port and nothing else, so a firmware
 * that dies before its port is up can still be reflashed over the cable. The
 * count clears once a boot has run for SAFE_MODE_SETTLED_MS, and safe mode
 * clears it too, so the boot after safe mode is a normal one.
 */

#define SAFE_MODE_AFTER_ATTEMPTS 3
#define SAFE_MODE_SETTLED_MS 8000

void safe_mode_begin(uint32_t now);
bool safe_mode_is_active(void);
void safe_mode_step(uint32_t now);

#ifdef __cplusplus
}
#endif
