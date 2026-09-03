#include "scenes/effects/effect.h"

#include <string.h>

extern const effect_t EFFECT_SLIDE;
extern const effect_t EFFECT_TEAR;

const effect_t *const EFFECTS[] = {
    &EFFECT_SLIDE,
    &EFFECT_TEAR,
};

const uint8_t EFFECT_COUNT = (uint8_t)(sizeof(EFFECTS) / sizeof(EFFECTS[0]));

uint8_t effect_index_of(const char *name) {
    for (uint8_t index = 0; index < EFFECT_COUNT; index++) {
        if (strcmp(EFFECTS[index]->name, name) == 0) {
            return index;
        }
    }

    return 0;
}
