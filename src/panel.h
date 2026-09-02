#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Brings up the QSPI bus and the ST77916 with the backlight off. The panel
// keeps its RAM through a reset, so the caller presents a frame before
// panel_set_backlight() to keep the previous run's screen from showing.
bool panel_begin(void);

// Pushes a full native-endian RGB565 frame, converting to the panel byte order.
void panel_present(const uint16_t *pixels);

// Pushes only the given rows. The ST77916 addresses pixels in pairs, so top and
// height are rounded outwards to even values.
void panel_present_rows(const uint16_t *pixels, int top, int height);

void panel_set_backlight(uint8_t level);

// Enables or disables frame buffer output (DISPON / DISPOFF). Display memory
// survives either way, so the panel still holds the last frame while off.
void panel_set_display_on(bool on);

#ifdef __cplusplus
}
#endif
