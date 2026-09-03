#include "scenes/reveal.h"

#include <math.h>
#include <stddef.h>

#include "render/canvas.h"
#include "scenes/effects/effect.h"
#include "haptics.h"
#include "render/theme.h"

/**
 * Both faces share one optical centre: YES and NO stand 70 rows tall on their
 * baseline, the digits 92 rows on theirs.
 */
#define ANSWER_BASELINE 212
#define NUMBER_BASELINE 223
#define MODIFIER_GAP 20

// Rows the two faces and the modifier can reach at those baselines.
#define STAGE_TOP 124
#define STAGE_HEIGHT 110

#define ANSWER_SLIDE_MS 380
#define BEAT_HOLD_MS 620
#define MODIFIER_SLIDE_MS 260

/**
 * The second beat always starts at the same moment, whether or not a modifier
 * is coming. Nothing before it may hint at the outcome.
 */
#define BEAT_TWO_MS (ANSWER_SLIDE_MS + BEAT_HOLD_MS)
#define REVEAL_TOTAL_MS (BEAT_TWO_MS + MODIFIER_SLIDE_MS)

// Offset so the haptic lands with the glyph rather than after it.
#define ANSWER_THUMP_MS (ANSWER_SLIDE_MS - 60)
#define MODIFIER_THUMP_MS (BEAT_TWO_MS + 90)

/**
 * Frames keep coming this long after a numeric effect reaches its rest. The
 * frame drawn at or after the rest is the one that stays on the panel, and
 * frames are paced at an interval, so stopping exactly at the rest could leave
 * the last moving frame standing.
 */
#define EFFECT_SETTLE_MS 32

static roll_t current_roll;
static uint32_t started_ms = 0;

/**
 * A numeric result is handed to the chosen effect. The oracle keeps its own
 * choreography below, so the setting can never touch it.
 */
extern const effect_t EFFECT_SLIDE;
static const effect_t *effect = &EFFECT_SLIDE;
static uint8_t effect_index = 0;
static effect_subject_t subject;

static const font_t *answer_font = NULL;
static float answer_entry_x = 0.0f;
static float answer_centre_x = 0.0f;
static float answer_final_x = 0.0f;
static float modifier_entry_x = 0.0f;
static float modifier_final_x = 0.0f;

static bool answer_thumped = false;
static bool modifier_thumped = false;

static float clamp_unit(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }

    if (value > 1.0f) {
        return 1.0f;
    }

    return value;
}

static float mix(float from, float to, float amount) {
    return from + (to - from) * amount;
}

// Hard deceleration, so the answer arrives with weight instead of drifting in.
static float ease_out_quint(float t) {
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse * inverse * inverse;
}

static float ease_out_cubic(float t) {
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

/**
 * Overshoots the target and settles back. This is what gives the modifier its
 * snap on arrival.
 */
static float ease_out_back(float t) {
    const float tension = 1.9f;
    const float inverse = t - 1.0f;
    return 1.0f + (tension + 1.0f) * inverse * inverse * inverse +
           tension * inverse * inverse;
}

static bool is_oracle(void) {
    return current_roll.kind == DIE_ORACLE;
}

static void begin_effect(const theme_t *theme, uint32_t seed) {
    subject.font = theme->number_font;
    subject.text = current_roll.answer;
    subject.width = font_text_width(subject.font, subject.text);
    subject.left = (CANVAS_WIDTH - subject.width) / 2;
    subject.baseline = NUMBER_BASELINE;
    subject.stage = reveal_stage();

    effect = EFFECTS[effect_index];
    effect->begin(&subject, seed);
}

void reveal_select_effect(uint8_t index) {
    effect_index = index < EFFECT_COUNT ? index : 0;
}

void reveal_begin(const roll_t *roll, uint32_t now) {
    const theme_t *theme = theme_active();

    current_roll = *roll;
    started_ms = now;
    answer_thumped = false;
    modifier_thumped = false;

    if (!is_oracle()) {
        begin_effect(theme, now);
        return;
    }

    answer_font = theme->answer_font;

    const int answer_width = font_text_width(answer_font, current_roll.answer);
    const int modifier_width =
        current_roll.modifier != NULL ? font_text_width(theme->label_font, current_roll.modifier) : 0;

    answer_centre_x = (float)((CANVAS_WIDTH - answer_width) / 2);
    answer_entry_x = -(float)(answer_width + 40);

    if (current_roll.modifier != NULL) {
        const int combined = answer_width + MODIFIER_GAP + modifier_width;
        answer_final_x = (float)((CANVAS_WIDTH - combined) / 2);
        modifier_final_x = answer_final_x + (float)(answer_width + MODIFIER_GAP);
    } else {
        answer_final_x = answer_centre_x;
        modifier_final_x = answer_centre_x;
    }
    modifier_entry_x = (float)CANVAS_WIDTH + 30.0f;
}

void reveal_tick(uint32_t now) {
    const uint32_t elapsed = now - started_ms;

    if (!is_oracle()) {
        effect->tick(elapsed);
        return;
    }

    if (!answer_thumped && elapsed >= ANSWER_THUMP_MS) {
        answer_thumped = true;
        haptics_play(HAPTIC_ANSWER);
    }

    if (!modifier_thumped && elapsed >= MODIFIER_THUMP_MS) {
        modifier_thumped = true;
        if (current_roll.modifier != NULL) {
            haptics_play(HAPTIC_MODIFIER);
        }
    }
}

void reveal_draw(uint32_t now, uint8_t alpha) {
    const theme_t *theme = theme_active();
    const uint32_t elapsed = now - started_ms;

    if (!is_oracle()) {
        effect->draw(&subject, elapsed, alpha);
        return;
    }

    float answer_x;
    if (elapsed < ANSWER_SLIDE_MS) {
        const float progress = ease_out_quint((float)elapsed / (float)ANSWER_SLIDE_MS);
        answer_x = mix(answer_entry_x, answer_centre_x, progress);
    } else if (elapsed < BEAT_TWO_MS) {
        answer_x = answer_centre_x;
    } else {
        const float progress =
            ease_out_cubic(clamp_unit((float)(elapsed - BEAT_TWO_MS) / (float)MODIFIER_SLIDE_MS));
        answer_x = mix(answer_centre_x, answer_final_x, progress);
    }
    canvas_text(answer_font, current_roll.answer, (int)lroundf(answer_x), ANSWER_BASELINE,
                theme->answer, alpha);

    if (current_roll.modifier != NULL && elapsed >= BEAT_TWO_MS) {
        const float progress =
            ease_out_back(clamp_unit((float)(elapsed - BEAT_TWO_MS) / (float)MODIFIER_SLIDE_MS));
        const float modifier_x = mix(modifier_entry_x, modifier_final_x, progress);
        canvas_text(theme->label_font, current_roll.modifier, (int)lroundf(modifier_x),
                    ANSWER_BASELINE, theme->modifier, alpha);
    }
}

frame_rect_t reveal_stage(void) {
    return (frame_rect_t){STAGE_TOP, STAGE_HEIGHT, 0, CANVAS_WIDTH};
}

bool reveal_is_animating(uint32_t now) {
    const uint32_t elapsed = now - started_ms;
    if (!is_oracle()) {
        return elapsed < effect->duration_ms + EFFECT_SETTLE_MS;
    }

    return elapsed < REVEAL_TOTAL_MS;
}

bool reveal_is_concealed(uint32_t now) {
    return is_oracle() && (now - started_ms) < BEAT_TWO_MS;
}
