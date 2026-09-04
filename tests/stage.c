/*
 * The stage's promises, proved on the host. A roll's kind decides what draws:
 * the oracle goes to the reveal, a coin to the coin, and every other result,
 * a two-sided numeric die among them, to an effect, all sharing the band the
 * caption stays clear of. Every effect in the table is held to the same promises through
 * the stage: it stays inside the band from any start instant, it lands on the
 * same rest as every other effect, the last frame drawn before frames stop is
 * that rest, and it plays exactly one cue. The exit status is the verdict.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "ports/haptics.h"
#include "oracle.h"
#include "render/canvas.h"
#include "render/theme.h"
#include "scenes/coin.h"
#include "scenes/effects/effect.h"
#include "scenes/effects/slide.h"
#include "scenes/reveal.h"
#include "stage.h"

/*
 * A roll arrives through its die's effect; here the effect is chosen per
 * check and laid on a copy of the roll as it goes on stage.
 */
static uint8_t chosen_effect = 0;

static void begin_on_stage(const roll_t *roll, uint32_t now) {
    roll_t copy = *roll;
    copy.effect = chosen_effect;
    stage_begin(&copy, now);
}

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

// Numeric results at each digit count, and the D66 that shares the face.
static const roll_t NUMERIC_ROLLS[] = {
    {.kind = DIE_NUMERIC, .answer = "1", .modifier = NULL},
    {.kind = DIE_NUMERIC, .answer = "17", .modifier = NULL},
    {.kind = DIE_NUMERIC, .answer = "100", .modifier = NULL},
    {.kind = DIE_D66, .answer = "66", .modifier = NULL},
};
#define NUMERIC_ROLL_COUNT (sizeof(NUMERIC_ROLLS) / sizeof(NUMERIC_ROLLS[0]))

static const roll_t COIN_ROLL = {.kind = DIE_COIN, .answer = "2", .value = 2, .modifier = NULL};

// A numeric die with two sides is a number like any other, whatever it is called.
static const roll_t TWO_SIDED_ROLL = {.kind = DIE_NUMERIC, .answer = "2", .value = 2, .modifier = NULL};

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

static bool is_same_rect(frame_rect_t a, frame_rect_t b) {
    return a.top == b.top && a.height == b.height && a.left == b.left && a.width == b.width;
}

static bool is_background_only(const theme_t *theme) {
    const uint16_t *pixels = canvas_pixels();
    for (size_t index = 0; index < (size_t)CANVAS_WIDTH * CANVAS_HEIGHT; index++) {
        if (pixels[index] != theme->colors.background) {
            return false;
        }
    }

    return true;
}

static void render_rest(const theme_t *theme, const roll_t *roll, uint32_t start) {
    begin_on_stage(roll, start);
    canvas_fill(theme->colors.background);
    stage_draw(start + LONG_AFTER_MS, FRAME_ALPHA);
}

// The kind of the roll decides what is on stage.
static void check_the_kind_picks_the_stage(void) {
    const roll_t oracle = oracle_outcome(0);

    begin_on_stage(&oracle, 0);
    if (!is_same_rect(stage_get_rect(), stage_band())) {
        fail("an oracle roll is not on the band");
    }

    begin_on_stage(&NUMERIC_ROLLS[1], 0);
    if (!is_same_rect(stage_get_rect(), stage_band())) {
        fail("a numeric roll is not on the band");
    }

    begin_on_stage(&COIN_ROLL, 0);
    if (!is_same_rect(stage_get_rect(), coin_stage())) {
        fail("a coin is not on the coin's stage");
    }

    if (!stage_is_rerolled_in_place()) {
        fail("the coin is not rerolled where it lies");
    }

    chosen_effect = 0;
    begin_on_stage(&TWO_SIDED_ROLL, 0);
    if (!is_same_rect(stage_get_rect(), stage_band())) {
        fail("a two-sided numeric die is not printed on the band");
    }

    if (stage_is_rerolled_in_place()) {
        fail("a printed D2 is rerolled in place");
    }
}

// The rightmost column with ink on it, or -1 on an empty frame.
static int rightmost_ink(const theme_t *theme) {
    const uint16_t *pixels = canvas_pixels();
    for (int column = CANVAS_WIDTH - 1; column >= 0; column--) {
        for (int row = 0; row < CANVAS_HEIGHT; row++) {
            if (pixels[(size_t)row * CANVAS_WIDTH + column] != theme->colors.background) {
                return column;
            }
        }
    }

    return -1;
}

/**
 * The oracle's answer and a number under the slide effect enter by one
 * motion: at every millisecond of the slide the ink of each stands where
 * slide_position() puts it, measured from where it stands at rest, and both
 * are felt at the same instant.
 */
static void check_the_answer_enters_as_the_slide(const theme_t *theme) {
    const roll_t oracle = oracle_outcome(1);
    const roll_t *number = &NUMERIC_ROLLS[2];
    const int answer_width = font_text_width(theme->answer_font, oracle.answer);
    const int answer_rest = (CANVAS_WIDTH - answer_width) / 2;
    const int number_width = font_text_width(theme->number_font, number->answer);
    const int number_rest = (CANVAS_WIDTH - number_width) / 2;

    chosen_effect = effect_index_of("slide");

    reveal_begin(&oracle, 0);
    canvas_fill(theme->colors.background);
    reveal_draw(SLIDE_MS, FRAME_ALPHA);
    const int answer_ink_rest = rightmost_ink(theme);

    begin_on_stage(number, 0);
    canvas_fill(theme->colors.background);
    stage_draw(SLIDE_MS, FRAME_ALPHA);
    const int number_ink_rest = rightmost_ink(theme);

    for (uint32_t elapsed = 0; elapsed < SLIDE_MS; elapsed++) {
        const int answer_offset =
            (int)lroundf(slide_position(answer_width, answer_rest, elapsed)) - answer_rest;
        reveal_begin(&oracle, 0);
        canvas_fill(theme->colors.background);
        reveal_draw(elapsed, FRAME_ALPHA);
        const int answer_ink = rightmost_ink(theme);
        if (answer_ink >= 0 && answer_ink - answer_ink_rest != answer_offset) {
            fail("the answer stands %d from its rest at %u ms; the slide puts it at %d",
                 answer_ink - answer_ink_rest, (unsigned)elapsed, answer_offset);
            return;
        }

        const int number_offset =
            (int)lroundf(slide_position(number_width, number_rest, elapsed)) - number_rest;
        begin_on_stage(number, 0);
        canvas_fill(theme->colors.background);
        stage_draw(elapsed, FRAME_ALPHA);
        const int number_ink = rightmost_ink(theme);
        if (number_ink >= 0 && number_ink - number_ink_rest != number_offset) {
            fail("the number stands %d from its rest at %u ms; the slide puts it at %d",
                 number_ink - number_ink_rest, (unsigned)elapsed, number_offset);
            return;
        }
    }

    cue_log_t answer_log;
    memset(&answer_log, 0, sizeof(answer_log));
    recording = &answer_log;
    reveal_begin(&oracle, 0);
    for (uint32_t now = 0; reveal_is_concealed(now); now += STEP_MS) {
        recording_now = now;
        reveal_tick(now);
    }

    cue_log_t number_log;
    memset(&number_log, 0, sizeof(number_log));
    recording = &number_log;
    begin_on_stage(number, 0);
    for (uint32_t now = 0; stage_is_animating(now); now += STEP_MS) {
        recording_now = now;
        stage_tick(now);
    }
    recording = NULL;

    if (answer_log.count != 1 || number_log.count != 1 ||
        answer_log.cues[0].at_ms != number_log.cues[0].at_ms ||
        answer_log.cues[0].effect != number_log.cues[0].effect) {
        fail("the answer is felt %d times, first at %u ms; the slide %d times, first at %u ms",
             answer_log.count, (unsigned)answer_log.cues[0].at_ms, number_log.count,
             (unsigned)number_log.cues[0].at_ms);
    }
}

// No frame of what is on stage may touch a pixel outside its band.
static void check_stage_band(const theme_t *theme, uint8_t effect, const roll_t *roll,
                             uint32_t start) {
    static uint16_t background_row[CANVAS_WIDTH];
    for (int column = 0; column < CANVAS_WIDTH; column++) {
        background_row[column] = theme->colors.background;
    }

    chosen_effect = effect;
    begin_on_stage(roll, start);
    const frame_rect_t band = stage_get_rect();

    for (uint32_t now = start; stage_is_animating(now); now += STEP_MS) {
        canvas_fill(theme->colors.background);
        stage_draw(now, FRAME_ALPHA);
        const uint16_t *pixels = canvas_pixels();
        for (int row = 0; row < CANVAS_HEIGHT; row++) {
            if (row >= band.top && row < band.top + band.height) {
                continue;
            }

            if (memcmp(&pixels[(size_t)row * CANVAS_WIDTH], background_row,
                       sizeof(background_row)) != 0) {
                fail("%s under %s draws outside its band at %u ms, row %d", roll->answer,
                     EFFECTS[effect]->name, (unsigned)(now - start), row);
                return;
            }
        }
    }
}

// Every effect lands on the same rest, and that rest shows the number.
static void check_effects_share_a_rest(const theme_t *theme) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    uint16_t *reference = malloc(frame_bytes);

    for (size_t index = 0; index < NUMERIC_ROLL_COUNT; index++) {
        const roll_t *roll = &NUMERIC_ROLLS[index];

        chosen_effect = 0;
        render_rest(theme, roll, 0);
        if (is_background_only(theme)) {
            fail("%s rests on an empty frame under %s", roll->answer, EFFECTS[0]->name);
        }
        memcpy(reference, canvas_pixels(), frame_bytes);

        for (uint8_t effect = 1; effect < EFFECT_COUNT; effect++) {
            chosen_effect = effect;
            render_rest(theme, roll, 0);
            if (memcmp(reference, canvas_pixels(), frame_bytes) != 0) {
                fail("%s rests differently under %s than under %s", roll->answer,
                     EFFECTS[effect]->name, EFFECTS[0]->name);
            }
        }
    }

    free(reference);
}

/**
 * Frames are drawn at an interval only while the stage reports itself
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

    chosen_effect = effect;
    render_rest(theme, roll, start);
    memcpy(rest, canvas_pixels(), frame_bytes);

    for (uint32_t interval = every_interval ? 1 : DEVICE_FRAME_MS; interval <= DEVICE_FRAME_MS;
         interval++) {
        begin_on_stage(roll, start);
        int moving_frames = 0;
        for (uint32_t now = start; stage_is_animating(now); now += interval) {
            canvas_fill(theme->colors.background);
            stage_draw(now, FRAME_ALPHA);
            moving_frames += memcmp(rest, canvas_pixels(), frame_bytes) != 0;
        }

        if (memcmp(rest, canvas_pixels(), frame_bytes) != 0) {
            fail("%s under %s stops on a moving frame at a %u ms interval", roll->answer,
                 EFFECTS[effect]->name, (unsigned)interval);
            break;
        }

        if (interval == DEVICE_FRAME_MS && moving_frames == 0) {
            fail("%s under %s draws no frame that differs from its rest", roll->answer,
                 EFFECTS[effect]->name);
        }
    }

    free(rest);
}

// A numeric roll is felt exactly once, while the effect is still running.
static void check_effect_plays_one_cue(uint8_t effect, const roll_t *roll, uint32_t start) {
    cue_log_t log;
    memset(&log, 0, sizeof(log));

    chosen_effect = effect;
    recording = &log;
    begin_on_stage(roll, start);
    for (uint32_t now = start; stage_is_animating(now); now += STEP_MS) {
        recording_now = now - start;
        stage_tick(now);
    }
    recording = NULL;

    if (log.count != 1) {
        fail("%s under %s plays %d cues", roll->answer, EFFECTS[effect]->name, log.count);
        return;
    }

    if (log.cues[0].at_ms > EFFECTS[effect]->duration_ms) {
        fail("%s under %s plays its cue at %u ms, after the effect rests", roll->answer,
             EFFECTS[effect]->name, (unsigned)log.cues[0].at_ms);
    }

    if (effect == effect_index_of("tear") &&
        (log.cues[0].effect != HAPTIC_MODIFIER || log.cues[0].at_ms != 0)) {
        fail("the tear plays haptic %d at %u ms; the strike is %d at 0 ms", log.cues[0].effect,
             (unsigned)log.cues[0].at_ms, HAPTIC_MODIFIER);
    }

    if (effect == effect_index_of("slide") && log.cues[0].effect != HAPTIC_ANSWER) {
        fail("the slide plays haptic %d; landing is %d", log.cues[0].effect, HAPTIC_ANSWER);
    }
}

/**
 * The tear is on screen in full from its first frame; the slide is still off
 * screen. Both are what their names promise.
 */
static void check_effect_opens_as_named(const theme_t *theme) {
    const roll_t *roll = &NUMERIC_ROLLS[1];

    chosen_effect = effect_index_of("tear");
    begin_on_stage(roll, 0);
    canvas_fill(theme->colors.background);
    stage_draw(0, FRAME_ALPHA);
    if (is_background_only(theme)) {
        fail("the tear's first frame is empty");
    }

    chosen_effect = effect_index_of("slide");
    begin_on_stage(roll, 0);
    canvas_fill(theme->colors.background);
    stage_draw(0, FRAME_ALPHA);
    if (!is_background_only(theme)) {
        fail("the slide's first frame already shows the number");
    }
}

// An effect index past the table falls back to the first row.
static void check_an_unknown_effect_falls_back(const theme_t *theme) {
    const size_t frame_bytes = (size_t)CANVAS_WIDTH * CANVAS_HEIGHT * sizeof(uint16_t);
    uint16_t *reference = malloc(frame_bytes);
    const roll_t *roll = &NUMERIC_ROLLS[2];

    chosen_effect = 0;
    begin_on_stage(roll, 0);
    canvas_fill(theme->colors.background);
    stage_draw(EFFECTS[0]->duration_ms / 2, FRAME_ALPHA);
    memcpy(reference, canvas_pixels(), frame_bytes);

    chosen_effect = EFFECT_COUNT;
    begin_on_stage(roll, 0);
    canvas_fill(theme->colors.background);
    stage_draw(EFFECTS[0]->duration_ms / 2, FRAME_ALPHA);
    if (memcmp(reference, canvas_pixels(), frame_bytes) != 0) {
        fail("an effect index past the table does not draw as %s", EFFECTS[0]->name);
    }

    free(reference);
}

int main(void) {
    if (!canvas_begin()) {
        fputs("stage: no framebuffer\n", stderr);
        return 1;
    }

    const theme_t *theme = theme_active();

    check_the_kind_picks_the_stage();
    check_the_answer_enters_as_the_slide(theme);
    check_effects_share_a_rest(theme);
    check_effect_opens_as_named(theme);
    check_an_unknown_effect_falls_back(theme);

    for (uint8_t effect = 0; effect < EFFECT_COUNT; effect++) {
        for (size_t index = 0; index < NUMERIC_ROLL_COUNT; index++) {
            for (size_t start = 0; start < START_TIME_COUNT; start++) {
                check_stage_band(theme, effect, &NUMERIC_ROLLS[index], START_TIMES[start]);
                check_effect_settles_before_frames_stop(theme, effect, &NUMERIC_ROLLS[index],
                                                        START_TIMES[start], start == 0);
                check_effect_plays_one_cue(effect, &NUMERIC_ROLLS[index], START_TIMES[start]);
            }
        }
    }

    if (failures > 0) {
        fprintf(stderr, "stage: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    printf("stage: %u effects through one band\n", (unsigned)EFFECT_COUNT);
    return 0;
}
