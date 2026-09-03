#include "stage.h"

#include <stddef.h>

#include "scenes/coin.h"
#include "scenes/reveal.h"

typedef struct {
    void (*begin)(const roll_t *roll, uint32_t now);
    void (*tick)(uint32_t now);
    void (*draw)(uint32_t now, uint8_t alpha);
    frame_rect_t (*rect)(void);
    bool (*is_animating)(uint32_t now);
    bool rerolled_in_place;
} stage_adapter_t;

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

void stage_configure(bool enabled) {
    coin_enabled = enabled;
}

void stage_begin(const roll_t *roll, uint32_t now) {
    current = &REVEAL_ADAPTER;
    if (roll->kind == DIE_COIN && coin_enabled) {
        current = &COIN_ADAPTER;
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
