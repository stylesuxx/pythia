#include "stage.h"

#include <stddef.h>

#include "render/canvas.h"
#include "render/theme.h"
#include "scenes/coin.h"
#include "scenes/effects/effect.h"
#include "scenes/reveal.h"

/**
 * The digits stand 92 rows tall on this baseline, sharing one optical centre
 * with the oracle's answer, and the band is the rows either can reach, the
 * modifier included.
 */
#define NUMBER_BASELINE 223
#define STAGE_TOP 124
#define STAGE_HEIGHT 110

/**
 * Frames keep coming this long after the effect reaches its rest. The frame
 * drawn at or after the rest is the one that stays on the panel, and frames
 * are paced at an interval, so stopping exactly at the rest could leave the
 * last moving frame standing.
 */
#define EFFECT_SETTLE_MS 32

typedef struct {
    void (*begin)(const roll_t *roll, uint32_t now);
    void (*tick)(uint32_t now);
    void (*draw)(uint32_t now, uint8_t alpha);
    frame_rect_t (*rect)(void);
    bool (*is_animating)(uint32_t now);
    bool rerolled_in_place;
} stage_adapter_t;

/*
 * A numeric result on its way to rest: the subject is built here, and the way
 * there is the effect's.
 */
static roll_t numeric_roll;
static uint32_t numeric_started_ms = 0;
static const effect_t *effect = NULL;
static effect_subject_t subject;

// The roll carries its die's effect; one past the table is the first.
static void numeric_begin(const roll_t *roll, uint32_t now) {
    const theme_t *theme = theme_active();

    numeric_roll = *roll;
    numeric_started_ms = now;
    effect = EFFECTS[roll->effect < EFFECT_COUNT ? roll->effect : 0];

    subject.font = theme->number_font;
    subject.text = numeric_roll.answer;
    subject.width = font_text_width(subject.font, subject.text);
    subject.left = (CANVAS_WIDTH - subject.width) / 2;
    subject.baseline = NUMBER_BASELINE;
    subject.color = theme->numbers.text;
    subject.background = theme->colors.background;
    subject.stage = stage_band();

    // The seed differs from roll to roll and carries nothing of the result.
    effect->begin(&subject, now);
}

static void numeric_tick(uint32_t now) {
    effect->tick(now - numeric_started_ms);
}

static void numeric_draw(uint32_t now, uint8_t alpha) {
    effect->draw(&subject, now - numeric_started_ms, alpha);
}

static bool numeric_is_animating(uint32_t now) {
    return (now - numeric_started_ms) < effect->duration_ms + EFFECT_SETTLE_MS;
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
    .rect = stage_band,
    .is_animating = reveal_is_animating,
    .rerolled_in_place = false,
};

static const stage_adapter_t NUMERIC_ADAPTER = {
    .begin = numeric_begin,
    .tick = numeric_tick,
    .draw = numeric_draw,
    .rect = stage_band,
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

void stage_begin(const roll_t *roll, uint32_t now) {
    switch (roll->kind) {
        case DIE_ORACLE: {
            current = &REVEAL_ADAPTER;
        } break;

        case DIE_COIN: {
            current = &COIN_ADAPTER;
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

frame_rect_t stage_band(void) {
    return (frame_rect_t){STAGE_TOP, STAGE_HEIGHT, 0, CANVAS_WIDTH};
}
