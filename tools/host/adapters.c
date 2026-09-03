/*
 * Default host adapters for the hardware the shared sources talk to. Each is
 * weak, so a host program that needs to observe or script one, the way
 * tests/reveal.c records haptics and tests/oracle.c feeds the entropy draws,
 * defines its own and the linker takes that instead.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "effects/effect.h"
#include "esp_random.h"
#include "haptics.h"
#include "settings.h"

__attribute__((weak)) uint32_t esp_random(void) {
    return ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 6) ^ (uint32_t)rand();
}

__attribute__((weak)) void haptics_begin(void) {}

__attribute__((weak)) void haptics_play(uint8_t effect) {
    (void)effect;
}

__attribute__((weak)) void settings_set_die_index(uint8_t index) {
    (void)index;
}

__attribute__((weak)) uint8_t settings_effect_index(void) {
    return effect_index_of("tear");
}
