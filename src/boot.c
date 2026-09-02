#include "boot.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "canvas.h"
#include "generated/fonts.h"
#include "glitch.h"
#include "haptics.h"
#include "theme.h"

const char BOOT_WORDMARK[] = "PYTHIA//";
#define WORDMARK_LENGTH 8

const char BOOT_MANUFACTURER[] = "DELPHI SYSTEMS";

// Every one of these must be in BOOT_WORDMARK_CHARACTERS in
// tools/make_fonts.c; tests/glyphs.c holds the font to it.
const char BOOT_SCRAMBLE_CHARACTERS[] = "0123456789ABCDEFXZ#%&/";
#define SCRAMBLE_COUNT ((uint32_t)(sizeof(BOOT_SCRAMBLE_CHARACTERS) - 1))

#define WORDMARK_BASELINE 198
#define RULE_Y 216.0f
#define RULE_WIDTH 2.0f

#define RING_RADIUS 172.0f
#define RING_WIDTH 3.0f
#define SPIN_START_ANGLE (-(float)M_PI / 2.0f)
#define SPIN_TAIL_RADIANS 2.2f

#define CAPTION_RADIUS 166.0f
#define CAPTION_ANGLE 1.5707963f // bottom of the panel
#define CAPTION_TRACKING 4.0f

#define SCAN_BAND_ROWS 3
#define SCAN_PERIOD_MS 1900
#define SCAN_ALPHA 22

// Timeline, in milliseconds from boot_begin.
#define SPIN_PERIOD_MS 900 // one revolution of the comet once up to speed
#define SPIN_RAMP_MS 500   // accelerating from rest

#define RING_CLOSE_START_MS 1900
#define RING_CLOSE_MS 350 // the tail grows until the ring is whole
#define RING_SETTLE_MS 700 // the whole ring crossfades to its resting colour

#define TYPE_START_MS 850
#define TYPE_STEP_MS 170

#define SCRAMBLE_LEAD 2 // positions shown scrambling ahead of the one settling
#define SCRAMBLE_STEP_MS 55

#define GLITCH_START_MS 2250
#define GLITCH_MS 220
#define GLITCH_STEP_MS 40
#define GLITCH_SLICES 4
#define GLITCH_SHIFT 18

#define RULE_START_MS 2250
#define RULE_MS 450

#define CAPTION_START_MS 2550
#define CAPTION_MS 700

#define FADE_START_MS 4300
#define FADE_MS 600

#define BOOT_TOTAL_MS (FADE_START_MS + FADE_MS)

// The ring closes as the glitch strikes, so one double click marks both.
#define RING_CLOSED_MS (RING_CLOSE_START_MS + RING_CLOSE_MS)

static uint32_t started_ms = 0;
static int wordmark_left = 0;
static int wordmark_width = 0;
static int wordmark_advance = 0;

static bool glitch_thumped = false;
static uint8_t glyphs_ticked = 0;

static float clamp_unit(float value) {
    if (value < 0.0f) {
        return 0.0f;
    }

    if (value > 1.0f) {
        return 1.0f;
    }

    return value;
}

static float ease_in_out_cubic(float t) {
    if (t < 0.5f) {
        return 4.0f * t * t * t;
    }

    const float inverse = -2.0f * t + 2.0f;
    return 1.0f - inverse * inverse * inverse / 2.0f;
}

static float ease_out_cubic(float t) {
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

static uint8_t scale_alpha(uint8_t alpha, float factor) {
    return (uint8_t)lroundf((float)alpha * clamp_unit(factor));
}

static uint32_t settle_ms(uint8_t position) {
    return TYPE_START_MS + (uint32_t)position * TYPE_STEP_MS;
}

void boot_begin(uint32_t now) {
    started_ms = now;
    glitch_thumped = false;
    glyphs_ticked = 0;

    wordmark_width = font_text_width(&font_boot_wordmark, BOOT_WORDMARK);
    wordmark_left = (CANVAS_WIDTH - wordmark_width) / 2;
    wordmark_advance = wordmark_width / WORDMARK_LENGTH;
}

void boot_tick(uint32_t now) {
    const uint32_t elapsed = now - started_ms;
    while (glyphs_ticked < WORDMARK_LENGTH && elapsed >= settle_ms(glyphs_ticked)) {
        glyphs_ticked++;
        haptics_play(HAPTIC_DETENT);
    }

    if (!glitch_thumped && elapsed >= GLITCH_START_MS) {
        glitch_thumped = true;
        haptics_play(HAPTIC_MODIFIER);
    }
}

// Angle the comet's head has travelled: a smooth start, then constant speed.
static float spin_angle(uint32_t elapsed) {
    const float speed = 2.0f * (float)M_PI / (float)SPIN_PERIOD_MS;
    const float ramp = (float)SPIN_RAMP_MS;
    const float t = (float)elapsed;
    if (t < ramp) {
        return speed * t * t / (2.0f * ramp);
    }

    return speed * (t - ramp / 2.0f);
}

static void draw_ring(const theme_t *theme, uint32_t elapsed, uint8_t alpha) {
    const float centre = CANVAS_WIDTH / 2.0f;
    const float two_pi = 2.0f * (float)M_PI;

    // The comet keeps its gradient from first spark to last: bright at the
    // head, dimming to nothing along the tail. While closing, the tail grows to
    // a full turn and the resting ring fades in beneath it, so the shrinking
    // gap fills with the resting colour and the moment of closure has nothing
    // to snap. Once whole, the comet dissolves out over the resting ring, still
    // turning, and only the colour is left to settle.
    float closing = 0.0f;
    if (elapsed >= RING_CLOSE_START_MS) {
        closing = ease_in_out_cubic(
            clamp_unit((float)(elapsed - RING_CLOSE_START_MS) / (float)RING_CLOSE_MS));
    }

    float comet_alpha = 1.0f;
    if (elapsed >= RING_CLOSED_MS) {
        comet_alpha = 1.0f - clamp_unit((float)(elapsed - RING_CLOSED_MS) / (float)RING_SETTLE_MS);
    }

    if (closing > 0.0f) {
        canvas_arc(centre, centre, RING_RADIUS, 0.0f, two_pi, RING_WIDTH, theme->ring,
                   scale_alpha(alpha, closing));
    }

    if (comet_alpha <= 0.0f) {
        return;
    }

    const float head = SPIN_START_ANGLE + spin_angle(elapsed);
    const float tail = fminf(SPIN_TAIL_RADIANS, spin_angle(elapsed)) +
                       (two_pi - SPIN_TAIL_RADIANS) * closing;
    if (tail <= 0.0f) {
        return;
    }

    canvas_arc_gradient(centre, centre, RING_RADIUS, head - tail, tail, RING_WIDTH,
                        theme->ring_active, 0, scale_alpha(alpha, comet_alpha));
}

static void draw_wordmark(const theme_t *theme, uint32_t elapsed, uint8_t alpha) {
    const uint32_t scramble_step = elapsed / SCRAMBLE_STEP_MS;
    char glyph[2] = {0, 0};

    for (uint8_t position = 0; position < WORDMARK_LENGTH; position++) {
        const int pen_x = wordmark_left + position * wordmark_advance;
        const bool settled = elapsed >= settle_ms(position);
        const bool reached =
            position < SCRAMBLE_LEAD || elapsed >= settle_ms((uint8_t)(position - SCRAMBLE_LEAD));

        if (settled) {
            glyph[0] = BOOT_WORDMARK[position];
            canvas_text(&font_boot_wordmark, glyph, pen_x, WORDMARK_BASELINE, theme->answer,
                        alpha);
        } else if (reached && elapsed >= TYPE_START_MS - TYPE_STEP_MS) {
            glyph[0] = BOOT_SCRAMBLE_CHARACTERS[glitch_hash(position, scramble_step) % SCRAMBLE_COUNT];
            canvas_text(&font_boot_wordmark, glyph, pen_x, WORDMARK_BASELINE, theme->label,
                        alpha);
        }
    }
}

// Tears the wordmark rows sideways in a few slices that change every
// GLITCH_STEP_MS, so the tear flickers rather than sliding.
static void draw_glitch(const theme_t *theme, uint32_t elapsed) {
    if (elapsed < GLITCH_START_MS || elapsed >= GLITCH_START_MS + GLITCH_MS) {
        return;
    }

    const glitch_tear_t tear = {
        .top = WORDMARK_BASELINE - font_boot_wordmark.ascent,
        .span = font_boot_wordmark.line_height,
        .limit = CANVAS_HEIGHT,
        .slices = GLITCH_SLICES,
        .max_shift = GLITCH_SHIFT,
    };
    glitch_tear(&tear, 0, (elapsed - GLITCH_START_MS) / GLITCH_STEP_MS, theme->background);
}

static void draw_rule(const theme_t *theme, uint32_t elapsed, uint8_t alpha) {
    if (elapsed < RULE_START_MS) {
        return;
    }

    const float progress = ease_out_cubic(clamp_unit((float)(elapsed - RULE_START_MS) / (float)RULE_MS));
    const float from_x = (float)wordmark_left;
    const float to_x = from_x + (float)wordmark_width * progress;
    if (to_x - from_x < 1.0f) {
        return;
    }

    canvas_line(from_x, RULE_Y, to_x, RULE_Y, RULE_WIDTH, theme->ring_active, alpha);
}

static void draw_caption(const theme_t *theme, uint32_t elapsed, uint8_t alpha) {
    if (elapsed < CAPTION_START_MS) {
        return;
    }

    const float progress = clamp_unit((float)(elapsed - CAPTION_START_MS) / (float)CAPTION_MS);
    canvas_text_arc(&font_boot_caption, BOOT_MANUFACTURER, CANVAS_WIDTH / 2.0f, CANVAS_HEIGHT / 2.0f,
                    CAPTION_RADIUS, CAPTION_ANGLE, CAPTION_TRACKING, theme->label,
                    scale_alpha(alpha, progress));
}

// A faint band drifting down the whole panel, the one thing that keeps moving
// while the wordmark holds.
static void draw_scanline(const theme_t *theme, uint32_t elapsed, uint8_t alpha) {
    const uint32_t phase = elapsed % SCAN_PERIOD_MS;
    const int travel = CANVAS_HEIGHT + SCAN_BAND_ROWS;
    const int top = (int)((uint32_t)travel * phase / SCAN_PERIOD_MS) - SCAN_BAND_ROWS;
    const uint8_t band_alpha = scale_alpha(alpha, (float)SCAN_ALPHA / 255.0f);

    for (int row = top; row < top + SCAN_BAND_ROWS; row++) {
        for (int column = 0; column < CANVAS_WIDTH; column++) {
            canvas_blend(column, row, theme->answer, band_alpha);
        }
    }
}

void boot_draw(uint32_t now) {
    const theme_t *theme = theme_active();
    const uint32_t elapsed = now - started_ms;

    uint8_t alpha = 255;
    if (elapsed >= FADE_START_MS) {
        alpha = scale_alpha(255, 1.0f - (float)(elapsed - FADE_START_MS) / (float)FADE_MS);
    }

    if (alpha == 0) {
        return;
    }

    draw_ring(theme, elapsed, alpha);
    draw_wordmark(theme, elapsed, alpha);
    draw_glitch(theme, elapsed);
    draw_rule(theme, elapsed, alpha);
    draw_caption(theme, elapsed, alpha);
    draw_scanline(theme, elapsed, alpha);
}

bool boot_is_running(uint32_t now) {
    return (now - started_ms) < BOOT_TOTAL_MS;
}
