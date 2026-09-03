/*
 * The reveal's promises, proved on the host. Until beat two, nothing on screen
 * and nothing in the hand tells whether a modifier is coming. Outcomes that
 * share an answer must be indistinguishable until then. Also proves that a
 * reveal frame never touches a pixel outside the stage band, which is what
 * lets a roll repaint only that band without disturbing the rim caption.
 *
 * Every millisecond of every outcome is rendered in every theme, because the
 * device draws at arbitrary instants and a coarser sample could miss a
 * one-frame leak. The exit status is the verdict.
 *
 * Numeric results arrive through whichever effect the setting names, and every
 * effect in the table is held to the same promises: it stays inside the stage
 * from any start instant, it lands on the same rest as every other effect, the
 * last frame drawn before frames stop is that rest, it plays exactly one cue,
 * and it leaves the oracle's frames and cues untouched.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render/canvas.h"
#include "scenes/effects/effect.h"
#include "haptics.h"
#include "oracle.h"
#include "scenes/reveal.h"
#include "settings.h"
#include "render/theme.h"

#define STEP_MS 1
#define FRAME_ALPHA 255
#define MAX_CUES 16

typedef struct {
    uint8_t effect;
    uint32_t at_ms;
} cue_t;

typedef struct {
    cue_t cues[MAX_CUES];
    int count;
} cue_log_t;

// The haptics adapter for this program records instead of buzzing.
static cue_log_t *recording = NULL;
static uint32_t recording_now = 0;

void haptics_begin(void) {}

void haptics_play(uint8_t effect) {
    if (recording == NULL || recording->count == MAX_CUES) {
        return;
    }

    recording->cues[recording->count].effect = effect;
    recording->cues[recording->count].at_ms = recording_now;
    recording->count++;
}

// The effect setting, scripted so every effect in the table gets its turn.

// Numeric results at each digit count, and the D66 that shares the face.
static const roll_t NUMERIC_ROLLS[] = {
    {.kind = DIE_NUMERIC, .answer = "1", .modifier = NULL},
    {.kind = DIE_NUMERIC, .answer = "17", .modifier = NULL},
    {.kind = DIE_NUMERIC, .answer = "100", .modifier = NULL},
    {.kind = DIE_D66, .answer = "66", .modifier = NULL},
};
#define NUMERIC_ROLL_COUNT (sizeof(NUMERIC_ROLLS) / sizeof(NUMERIC_ROLLS[0]))

// Instants a roll may begin at, one of them within reach of the clock's wrap.
static const uint32_t START_TIMES[] = {0, 1, 777, UINT32_MAX - 100};
#define START_TIME_COUNT (sizeof(START_TIMES) / sizeof(START_TIMES[0]))

// Longer than any effect, so a frame this far in is the effect at rest.
#define LONG_AFTER_MS 60000

// The widest frame interval the device paces at.
#define DEVICE_FRAME_MS 16

static int failures = 0;

static void fail(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void fail(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);

    fputs("FAIL: ", stderr);
    vfprintf(stderr, format, arguments);
    fputc('\n', stderr);

    va_end(arguments);
    failures++;
}

// Two buffers, so one message can name two outcomes.
static const char *describe(const roll_t *roll) {
    static char texts[2][24];
    static int next = 0;
    char *text = texts[next];
    next = (next + 1) % 2;
    snprintf(text, sizeof(texts[0]), "%s%s%s", roll->answer, roll->modifier ? " " : "",
             roll->modifier ? roll->modifier : "");

             return text;
}

/**
 * The outcome an outcome is measured against: the first with the same answer.
 * An outcome that is its own reference has nothing to be compared with yet.
 */
static uint8_t reference_outcome(uint8_t outcome) {
    const roll_t roll = oracle_outcome(outcome);
    for (uint8_t candidate = 0; candidate < outcome; candidate++) {
        const roll_t other = oracle_outcome(candidate);
        if (strcmp(other.answer, roll.answer) == 0) {
            return candidate;
        }
    }

    return outcome;
}

// Last instant of the concealed span, found by rendering outcome 0.
static uint32_t concealed_until(void) {
    roll_t roll = oracle_outcome(0);
    reveal_begin(&roll, 0);

    uint32_t now = 0;
    while (reveal_is_concealed(now)) {
        now += STEP_MS;
    }

    return now;
}

/**
 * Renders every outcome at one instant and reports the first pair that share
 * an answer but not a frame. References hold the frame of the first outcome
 * seen for each answer.
 */
static bool frames_agree_at(const theme_t *theme, uint32_t now, uint16_t **references) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);

    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        roll_t roll = oracle_outcome(outcome);
        const uint8_t reference = reference_outcome(outcome);
        reveal_begin(&roll, 0);
        canvas_fill(theme->background);
        reveal_draw(now, FRAME_ALPHA);

        if (reference == outcome) {
            memcpy(references[outcome], canvas_pixels(), frame_bytes);
            continue;
        }

        const uint16_t *expected = references[reference];
        const uint16_t *pixels = canvas_pixels();
        if (memcmp(expected, pixels, frame_bytes) == 0) {
            continue;
        }

        int index = 0;
        while (pixels[index] == expected[index]) {
            index++;
        }

        const roll_t other = oracle_outcome(reference);
        fail("%s: %s and %s differ at %u ms, pixel (%d, %d)", theme->name, describe(&roll),
             describe(&other), (unsigned)now, index % CANVAS_WIDTH, index / CANVAS_WIDTH);

             return false;
    }

    return true;
}

/**
 * Every concealed frame must be identical across the outcomes that share an
 * answer.
 */
static void check_concealed_frames(const theme_t *theme, uint32_t until) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    uint16_t *references[UINT8_MAX] = {NULL};
    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        references[outcome] = malloc(frame_bytes);
    }

    for (uint32_t now = 0; now < until; now += STEP_MS) {
        if (!frames_agree_at(theme, now, references)) {
            break;
        }
    }

    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        free(references[outcome]);
    }
}

// Cues are logged in time order, so the concealed ones are a prefix.
static int concealed_cue_count(const cue_log_t *log, uint32_t until) {
    int count = 0;
    while (count < log->count && log->cues[count].at_ms < until) {
        count++;
    }

    return count;
}

/**
 * Every haptic cue inside the concealed span must match across the outcomes
 * that share an answer, in effect and in instant. Cues after it are the
 * outcome and may differ.
 */
static void check_concealed_cues(const theme_t *theme, uint32_t until) {
    cue_log_t logs[ORACLE_OUTCOME_COUNT];

    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        roll_t roll = oracle_outcome(outcome);
        memset(&logs[outcome], 0, sizeof(logs[outcome]));
        recording = &logs[outcome];
        reveal_begin(&roll, 0);
        for (uint32_t now = 0; reveal_is_animating(now); now += STEP_MS) {
            recording_now = now;
            reveal_tick(now);
        }

        recording = NULL;
    }

    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        const uint8_t reference = reference_outcome(outcome);
        if (reference == outcome) {
            continue;
        }

        const roll_t roll = oracle_outcome(outcome);
        const roll_t other = oracle_outcome(reference);
        const cue_t *expected = logs[reference].cues;
        const cue_t *actual = logs[outcome].cues;
        const int expected_count = concealed_cue_count(&logs[reference], until);
        const int actual_count = concealed_cue_count(&logs[outcome], until);

        if (actual_count < expected_count) {
            fail("%s: %s lacks the haptic %d that %s plays at %u ms while concealed", theme->name,
                 describe(&roll), expected[actual_count].effect, describe(&other),
                 (unsigned)expected[actual_count].at_ms);
            continue;
        }

        if (actual_count > expected_count) {
            fail("%s: %s plays haptic %d at %u ms while concealed; %s does not", theme->name,
                 describe(&roll), actual[expected_count].effect,
                 (unsigned)actual[expected_count].at_ms, describe(&other));
            continue;
        }

        for (int index = 0; index < expected_count; index++) {
            if (expected[index].effect == actual[index].effect &&
                expected[index].at_ms == actual[index].at_ms) {
                continue;
            }

            fail("%s: %s plays haptic %d at %u ms while concealed; %s plays %d at %u ms",
                 theme->name, describe(&roll), actual[index].effect, (unsigned)actual[index].at_ms,
                 describe(&other), expected[index].effect, (unsigned)expected[index].at_ms);
            break;
        }
    }
}

// No reveal frame may touch a pixel outside the stage band.
static void check_stage_band(const theme_t *theme, const roll_t *roll, uint32_t start) {
    static uint16_t background_row[CANVAS_WIDTH];
    for (int column = 0; column < CANVAS_WIDTH; column++) {
        background_row[column] = theme->background;
    }
    const frame_rect_t stage = reveal_stage();

    reveal_begin(roll, start);
    for (uint32_t now = start; reveal_is_animating(now); now += STEP_MS) {
        canvas_fill(theme->background);
        reveal_draw(now, FRAME_ALPHA);
        const uint16_t *pixels = canvas_pixels();
        for (int row = 0; row < CANVAS_HEIGHT; row++) {
            if (row >= stage.top && row < stage.top + stage.height) {
                continue;
            }

            if (memcmp(&pixels[(size_t)row * CANVAS_WIDTH], background_row,
                       sizeof(background_row)) != 0) {
                fail("%s: %s draws outside the stage band at %u ms, row %d", theme->name,
                     describe(roll), (unsigned)(now - start), row);
                return;
            }
        }
    }
}

/**
 * The rim caption must lie entirely outside the stage, so a band repaint can
 * never touch it. Measured from the widest die name rather than assumed.
 */
static void check_caption_clears_stage(const theme_t *theme) {
    const frame_rect_t stage = reveal_stage();
    canvas_fill(theme->background);
    reveal_draw_caption("ORACLE", FRAME_ALPHA);

    const uint16_t *pixels = canvas_pixels();
    for (int row = stage.top; row < stage.top + stage.height; row++) {
        for (int column = 0; column < CANVAS_WIDTH; column++) {
            if (pixels[row * CANVAS_WIDTH + column] != theme->background) {
                fail("%s: the rim caption reaches into the stage at row %d", theme->name, row);
                return;
            }
        }
    }
}

static bool is_background_only(const theme_t *theme) {
    const uint16_t *pixels = canvas_pixels();
    for (size_t index = 0; index < (size_t)CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        if (pixels[index] != theme->background) {
            return false;
        }
    }

    return true;
}

static void render_rest(const theme_t *theme, const roll_t *roll, uint32_t start) {
    reveal_begin(roll, start);
    canvas_fill(theme->background);
    reveal_draw(start + LONG_AFTER_MS, FRAME_ALPHA);
}

// Every effect lands on the same rest, and that rest shows the number.
static void check_effects_share_a_rest(const theme_t *theme) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    uint16_t *reference = malloc(frame_bytes);

    for (size_t index = 0; index < NUMERIC_ROLL_COUNT; index++) {
        const roll_t *roll = &NUMERIC_ROLLS[index];

        reveal_select_effect(0);
        render_rest(theme, roll, 0);
        if (is_background_only(theme)) {
            fail("%s: %s rests on an empty frame under %s", theme->name, describe(roll),
                 EFFECTS[0]->name);
        }
        memcpy(reference, canvas_pixels(), frame_bytes);

        for (uint8_t effect = 1; effect < EFFECT_COUNT; effect++) {
            reveal_select_effect(effect);
            render_rest(theme, roll, 0);
            if (memcmp(reference, canvas_pixels(), frame_bytes) != 0) {
                fail("%s: %s rests differently under %s than under %s", theme->name,
                     describe(roll), EFFECTS[effect]->name, EFFECTS[0]->name);
            }
        }
    }

    free(reference);
}

/**
 * Frames are drawn at an interval only while the reveal reports itself
 * animating, and the last one drawn is what stays on the panel, so it must be
 * the rest. At the device's own interval, at least one earlier frame must
 * differ from the rest, or the effect does nothing. Every interval up to the
 * device's is swept when every_interval is set; otherwise only the device's.
 */
static void check_effect_settles_before_frames_stop(const theme_t *theme, uint8_t effect,
                                                    const roll_t *roll, uint32_t start,
                                                    bool every_interval) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    uint16_t *rest = malloc(frame_bytes);

    reveal_select_effect(effect);
    render_rest(theme, roll, start);
    memcpy(rest, canvas_pixels(), frame_bytes);

    for (uint32_t interval = every_interval ? 1 : DEVICE_FRAME_MS; interval <= DEVICE_FRAME_MS;
         interval++) {
        reveal_begin(roll, start);
        int moving_frames = 0;
        for (uint32_t now = start; reveal_is_animating(now); now += interval) {
            canvas_fill(theme->background);
            reveal_draw(now, FRAME_ALPHA);
            moving_frames += memcmp(rest, canvas_pixels(), frame_bytes) != 0;
        }

        if (memcmp(rest, canvas_pixels(), frame_bytes) != 0) {
            fail("%s: %s under %s stops on a moving frame at a %u ms interval", theme->name,
                 describe(roll), EFFECTS[effect]->name, (unsigned)interval);
            break;
        }

        if (interval == DEVICE_FRAME_MS && moving_frames == 0) {
            fail("%s: %s under %s draws no frame that differs from its rest", theme->name,
                 describe(roll), EFFECTS[effect]->name);
        }
    }

    free(rest);
}

// A numeric roll is felt exactly once, while the effect is still running.
static void check_effect_plays_one_cue(const theme_t *theme, uint8_t effect, const roll_t *roll,
                                       uint32_t start) {
    cue_log_t log;
    memset(&log, 0, sizeof(log));

    reveal_select_effect(effect);
    recording = &log;
    reveal_begin(roll, start);
    for (uint32_t now = start; reveal_is_animating(now); now += STEP_MS) {
        recording_now = now - start;
        reveal_tick(now);
    }
    recording = NULL;

    if (log.count != 1) {
        fail("%s: %s under %s plays %d cues", theme->name, describe(roll), EFFECTS[effect]->name,
             log.count);
        return;
    }

    if (log.cues[0].at_ms > EFFECTS[effect]->duration_ms) {
        fail("%s: %s under %s plays its cue at %u ms, after the effect rests", theme->name,
             describe(roll), EFFECTS[effect]->name, (unsigned)log.cues[0].at_ms);
    }

    if (effect == effect_index_of("tear") &&
        (log.cues[0].effect != HAPTIC_MODIFIER || log.cues[0].at_ms != 0)) {
        fail("%s: the tear plays haptic %d at %u ms; the strike is %d at 0 ms", theme->name,
             log.cues[0].effect, (unsigned)log.cues[0].at_ms, HAPTIC_MODIFIER);
    }

    if (effect == effect_index_of("slide") && log.cues[0].effect != HAPTIC_ANSWER) {
        fail("%s: the slide plays haptic %d; landing is %d", theme->name, log.cues[0].effect,
             HAPTIC_ANSWER);
    }
}

/**
 * The tear is on screen in full from its first frame; the slide is still off
 * screen. Both are what their names promise.
 */
static void check_effect_opens_as_named(const theme_t *theme) {
    const roll_t *roll = &NUMERIC_ROLLS[1];

    reveal_select_effect(effect_index_of("tear"));
    reveal_begin(roll, 0);
    canvas_fill(theme->background);
    reveal_draw(0, FRAME_ALPHA);
    if (is_background_only(theme)) {
        fail("%s: the tear's first frame is empty", theme->name);
    }

    reveal_select_effect(effect_index_of("slide"));
    reveal_begin(roll, 0);
    canvas_fill(theme->background);
    reveal_draw(0, FRAME_ALPHA);
    if (!is_background_only(theme)) {
        fail("%s: the slide's first frame already shows the number", theme->name);
    }
}

/**
 * The setting must have no reach into the oracle: its frames and its cues are
 * the same under every effect.
 */
static void check_oracle_ignores_the_effect(const theme_t *theme) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    uint16_t *reference = malloc(frame_bytes);
    cue_log_t reference_log;

    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        const roll_t roll = oracle_outcome(outcome);

        reveal_select_effect(0);
        memset(&reference_log, 0, sizeof(reference_log));
        recording = &reference_log;
        reveal_begin(&roll, 0);
        for (uint32_t now = 0; reveal_is_animating(now); now += STEP_MS) {
            recording_now = now;
            reveal_tick(now);
        }
        recording = NULL;

        for (uint8_t effect = 1; effect < EFFECT_COUNT; effect++) {
            for (uint32_t now = 0; now < 2000; now += 37) {
                reveal_select_effect(0);
                reveal_begin(&roll, 0);
                canvas_fill(theme->background);
                reveal_draw(now, FRAME_ALPHA);
                memcpy(reference, canvas_pixels(), frame_bytes);

                reveal_select_effect(effect);
                reveal_begin(&roll, 0);
                canvas_fill(theme->background);
                reveal_draw(now, FRAME_ALPHA);
                if (memcmp(reference, canvas_pixels(), frame_bytes) != 0) {
                    fail("%s: %s draws differently at %u ms under %s", theme->name,
                         describe(&roll), (unsigned)now, EFFECTS[effect]->name);
                    break;
                }
            }

            cue_log_t log;
            memset(&log, 0, sizeof(log));
            reveal_select_effect(effect);
            recording = &log;
            reveal_begin(&roll, 0);
            for (uint32_t now = 0; reveal_is_animating(now); now += STEP_MS) {
                recording_now = now;
                reveal_tick(now);
            }
            recording = NULL;

            if (log.count != reference_log.count ||
                memcmp(log.cues, reference_log.cues, sizeof(cue_t) * (size_t)log.count) != 0) {
                fail("%s: %s plays different cues under %s", theme->name, describe(&roll),
                     EFFECTS[effect]->name);
            }
        }
    }

    free(reference);
}

int main(void) {
    if (!canvas_begin()) {
        fputs("check: no framebuffer\n", stderr);
        return 1;
    }

    for (uint8_t index = 0; index < THEME_COUNT; index++) {
        theme_select(index);
        const theme_t *theme = theme_active();
        reveal_select_effect(0);
        const uint32_t until = concealed_until();

        check_concealed_frames(theme, until);
        check_concealed_cues(theme, until);

        for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
            const roll_t roll = oracle_outcome(outcome);
            check_stage_band(theme, &roll, 0);
        }

        check_caption_clears_stage(theme);
        check_oracle_ignores_the_effect(theme);
        check_effects_share_a_rest(theme);
        check_effect_opens_as_named(theme);

        for (uint8_t effect = 0; effect < EFFECT_COUNT; effect++) {
            for (size_t index = 0; index < NUMERIC_ROLL_COUNT; index++) {
                for (size_t start = 0; start < START_TIME_COUNT; start++) {
                    check_stage_band(theme, &NUMERIC_ROLLS[index], START_TIMES[start]);
                    check_effect_settles_before_frames_stop(theme, effect, &NUMERIC_ROLLS[index],
                                                            START_TIMES[start], start == 0);
                    check_effect_plays_one_cue(theme, effect, &NUMERIC_ROLLS[index],
                                               START_TIMES[start]);
                }
            }
        }

        printf("%s: concealed for %u ms, %u outcomes, %u effects\n", theme->name,
               (unsigned)until, (unsigned)ORACLE_OUTCOME_COUNT, (unsigned)EFFECT_COUNT);
    }

    if (failures > 0) {
        fprintf(stderr, "check: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("check: ok");
    return 0;
}
