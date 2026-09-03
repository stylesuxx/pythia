#include "stage.h"

#include <stddef.h>

#include "scenes/coin.h"
#include "scenes/effects/effect.h"
#include "scenes/numeric.h"
#include "scenes/reveal.h"

typedef struct {
    void (*begin)(const roll_t *roll, uint32_t now);
    void (*tick)(uint32_t now);
    void (*draw)(uint32_t now, uint8_t alpha);
    frame_rect_t (*rect)(void);
    bool (*is_animating)(uint32_t now);
    bool rerolled_in_place;
} stage_adapter_t;

static uint8_t effect_index = 0;

static void numeric_begin_with_effect(const roll_t *roll, uint32_t now) {
    numeric_begin(roll, EFFECTS[effect_index], now);
}

static void coin_begin(const roll_t *roll, uint32_t now) {
    coin_flip(roll->value, now);
}

static void coin_tick(uint32_t now) {
    (void)now;
}

static const stage_adapter_t REVEAL_ADAPTER = {
    .begin = reveal_begin,
    .tick = reveal_tick,
    .draw = reveal_draw,
    .rect = reveal_stage,
    .is_animating = reveal_is_animating,
    .rerolled_in_place = false,
};

static const stage_adapter_t NUMERIC_ADAPTER = {
    .begin = numeric_begin_with_effect,
    .tick = numeric_tick,
    .draw = numeric_draw,
    .rect = numeric_stage,
    .is_animating = numeric_is_animating,
    .rerolled_in_place = false,
};

/**
 * The coin is already on screen and already the result, so asking again just
 * sets it spinning.
 */
static const stage_adapter_t COIN_ADAPTER = {
    .begin = coin_begin,
    .tick = coin_tick,
    .draw = coin_draw,
    .rect = coin_stage,
    .is_animating = coin_is_animating,
    .rerolled_in_place = true,
};

static const stage_adapter_t *current = &REVEAL_ADAPTER;
static bool coin_enabled = true;

void stage_configure(bool enabled, uint8_t index) {
    coin_enabled = enabled;
    effect_index = index < EFFECT_COUNT ? index : 0;
}

void stage_begin(const roll_t *roll, uint32_t now) {
    switch (roll->kind) {
        case DIE_ORACLE: {
            current = &REVEAL_ADAPTER;
        } break;

        case DIE_COIN: {
            current = coin_enabled ? &COIN_ADAPTER : &NUMERIC_ADAPTER;
        } break;

        case DIE_NUMERIC:
        case DIE_D66: {
            current = &NUMERIC_ADAPTER;
        } break;
    }

    current->begin(roll, now);
}

void stage_tick(uint32_t now) {
    current->tick(now);
}

void stage_draw(uint32_t now, uint8_t alpha) {
    current->draw(now, alpha);
}

frame_rect_t stage_get_rect(void) {
    return current->rect();
}

bool stage_is_animating(uint32_t now) {
    return current->is_animating(now);
}

bool stage_is_rerolled_in_place(void) {
    return current->rerolled_in_place;
}
