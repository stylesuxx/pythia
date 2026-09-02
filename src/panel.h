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

// Pushes only the given rectangle. The ST77916 addresses pixels in pairs, so
// every edge is rounded outwards to an even value. Sending a narrow rectangle
// is the only defence against tearing on this board: the panel's TE signal
// reaches the LCD connector but is left unconnected, so the write cannot be put
// in step with the scan and can only be made short enough to cross it quickly.
void panel_present_rect(const uint16_t *pixels, int top, int height, int left, int width);

// Sets the light level, 0 to 255. The panel's output follows it: enabled
// before the first lit level and disabled once the level reaches 0, so a dark
// screen draws nothing. Display memory survives either way, and the last
// frame is still there when the light comes back. An unchanged level costs
// nothing.
void panel_set_backlight(uint8_t level);

#ifdef __cplusplus
}
#endif
