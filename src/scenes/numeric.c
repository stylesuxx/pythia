#include "scenes/numeric.h"

#include "render/canvas.h"
#include "render/theme.h"

/**
 * The digits stand 92 rows tall on this baseline, sharing one optical centre
 * with the oracle's answer, and the band is the rows either can reach.
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

static roll_t current_roll;
static uint32_t started_ms = 0;
static const effect_t *effect = NULL;
static effect_subject_t subject;

void numeric_begin(const roll_t *roll, const effect_t *chosen, uint32_t now) {
    const theme_t *theme = theme_active();

    current_roll = *roll;
    started_ms = now;
    effect = chosen;

    subject.font = theme->number_font;
    subject.text = current_roll.answer;
    subject.width = font_text_width(subject.font, subject.text);
    subject.left = (CANVAS_WIDTH - subject.width) / 2;
    subject.baseline = NUMBER_BASELINE;
    subject.color = theme->numbers.text;
    subject.background = theme->colors.background;
    subject.stage = numeric_stage();

    // The seed differs from roll to roll and carries nothing of the result.
    effect->begin(&subject, now);
}

void numeric_tick(uint32_t now) {
    effect->tick(now - started_ms);
}

void numeric_draw(uint32_t now, uint8_t alpha) {
    effect->draw(&subject, now - started_ms, alpha);
}

frame_rect_t numeric_stage(void) {
    return (frame_rect_t){STAGE_TOP, STAGE_HEIGHT, 0, CANVAS_WIDTH};
}

bool numeric_is_animating(uint32_t now) {
    return (now - started_ms) < effect->duration_ms + EFFECT_SETTLE_MS;
}
