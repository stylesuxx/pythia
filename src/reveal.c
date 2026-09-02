#include "reveal.h"

#include <math.h>
#include <stddef.h>

#include "canvas.h"
#include "haptics.h"
#include "theme.h"

// Both faces share one optical centre: YES and NO stand 70 rows tall on their
// baseline, the digits 92 rows on theirs.
#define ANSWER_BASELINE 212
#define NUMBER_BASELINE 223
#define MODIFIER_GAP 20

// Baseline circle for the rim caption, measured from the panel centre.
#define CAPTION_RADIUS 166.0f
#define CAPTION_ANGLE 1.5707963f // bottom of the panel
#define CAPTION_TRACKING 5.0f

#define ANSWER_SLIDE_MS 380
#define BEAT_HOLD_MS 620
#define MODIFIER_SLIDE_MS 260

// The second beat always starts at the same moment, whether or not a modifier
// is coming. Nothing before it may hint at the outcome.
#define BEAT_TWO_MS (ANSWER_SLIDE_MS + BEAT_HOLD_MS)
#define REVEAL_TOTAL_MS (BEAT_TWO_MS + MODIFIER_SLIDE_MS)

// Offset so the haptic lands with the glyph rather than after it.
#define ANSWER_THUMP_MS (ANSWER_SLIDE_MS - 60)
#define MODIFIER_THUMP_MS (BEAT_TWO_MS + 90)

static roll_t current_roll;
static uint32_t started_ms = 0;

static const font_t *answer_font = NULL;
static int answer_baseline = ANSWER_BASELINE;
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

// Overshoots the target and settles back. This is what gives the modifier its
// snap on arrival.
static float ease_out_back(float t) {
    const float tension = 1.9f;
    const float inverse = t - 1.0f;
    return 1.0f + (tension + 1.0f) * inverse * inverse * inverse +
           tension * inverse * inverse;
}

void reveal_begin(const roll_t *roll, uint32_t now) {
    const theme_t *theme = theme_active();

    current_roll = *roll;
    started_ms = now;
    answer_thumped = false;
    modifier_thumped = false;

    const bool oracle = current_roll.kind == DIE_ORACLE;
    answer_font = oracle ? theme->answer_font : theme->number_font;
    answer_baseline = oracle ? ANSWER_BASELINE : NUMBER_BASELINE;

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
    canvas_text(answer_font, current_roll.answer, (int)lroundf(answer_x), answer_baseline,
                theme->answer, alpha);

    if (current_roll.modifier != NULL && elapsed >= BEAT_TWO_MS) {
        const float progress =
            ease_out_back(clamp_unit((float)(elapsed - BEAT_TWO_MS) / (float)MODIFIER_SLIDE_MS));
        const float modifier_x = mix(modifier_entry_x, modifier_final_x, progress);
        canvas_text(theme->label_font, current_roll.modifier, (int)lroundf(modifier_x),
                    answer_baseline, theme->modifier, alpha);
    }
}

void reveal_draw_caption(const char *text, uint8_t alpha) {
    const theme_t *theme = theme_active();
    canvas_text_arc(theme->caption_font, text, CANVAS_WIDTH / 2.0f, CANVAS_HEIGHT / 2.0f,
                    CAPTION_RADIUS, CAPTION_ANGLE, CAPTION_TRACKING, theme->label, alpha);
}

bool reveal_is_animating(uint32_t now) {
    return (now - started_ms) < REVEAL_TOTAL_MS;
}
