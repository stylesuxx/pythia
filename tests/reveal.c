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
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "haptics.h"
#include "oracle.h"
#include "render/canvas.h"
#include "render/theme.h"
#include "scenes/reveal.h"

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
        canvas_fill(theme->colors.background);
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
        background_row[column] = theme->colors.background;
    }
    const frame_rect_t stage = reveal_stage();

    reveal_begin(roll, start);
    for (uint32_t now = start; reveal_is_animating(now); now += STEP_MS) {
        canvas_fill(theme->colors.background);
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

int main(void) {
    if (!canvas_begin()) {
        fputs("check: no framebuffer\n", stderr);
        return 1;
    }

    const theme_t *theme = theme_active();
    const uint32_t until = concealed_until();

    check_concealed_frames(theme, until);
    check_concealed_cues(theme, until);

    for (uint8_t outcome = 0; outcome < ORACLE_OUTCOME_COUNT; outcome++) {
        const roll_t roll = oracle_outcome(outcome);
        check_stage_band(theme, &roll, 0);
    }

    printf("%s: concealed for %u ms, %u outcomes\n", theme->name, (unsigned)until,
           (unsigned)ORACLE_OUTCOME_COUNT);

    if (failures > 0) {
        fprintf(stderr, "check: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("check: ok");
    return 0;
}
