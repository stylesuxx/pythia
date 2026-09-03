#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * The die list: the current name centred, and a tick ring around the rim
 * marking where it sits in the list. Drawn over an already filled background.
 */
void menu_draw(uint8_t selected, uint8_t alpha);

#ifdef __cplusplus
}
#endif
