#pragma once

#include <stdint.h>

// Brings the CST816 out of reset and puts it in normal reporting mode.
// The caller owns the I2C bus and must have called Wire.begin() first.
void touch_init();

// Reports the current contact, if any. Coordinates are panel pixels.
bool is_touch_pressed(uint16_t &x, uint16_t &y);
