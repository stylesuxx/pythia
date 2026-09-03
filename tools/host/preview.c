/*
 * Renders the boot sequence, the die list or the reveal animation off-device by
 * building the real canvas, font and animation sources against a host stub,
 * then encodes the frames straight to an animated GIF.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "canvas.h"
#include "coin.h"
#include "effects/effect.h"
#include "mode.h"
#include "oracle.h"
#include "reveal.h"
#include "settings.h"
#include "theme.h"

#include "gif.h"

/**
 * GIF frame delays are whole centiseconds, so frames are sampled 20 ms apart
 * and the file plays back at real speed.
 */
#define FRAME_INTERVAL_MS 20
#define FRAME_CENTISECONDS (FRAME_INTERVAL_MS / 10)

// Time the finished reveal stays on screen before the GIF loops.
#define REVEAL_END_HOLD_MS 3000

/**
 * The list render: a pause on the armed screen, one click per die around the
 * whole list, then long enough for the choice to settle back into the armed
 * screen.
 */
#define MENU_LEAD_MS 400
#define MENU_STEP_MS 460
#define MENU_SETTLE_MS 1700

// Colour shown around the round panel, standing in for the bezel.
#define SURROUND_RGB565 0x18C3

static uint16_t frame_pixels[CANVAS_WIDTH * CANVAS_HEIGHT];

void haptics_begin(void) {}

void haptics_play(uint8_t effect) {
    (void)effect;
}

/**
 * Settings are device state; the preview pins them to the shipped defaults,
 * except the effect, which the reveal render takes from its arguments.
 */
static uint8_t preview_effect = 0;

bool settings_is_display_rotated(void) {
    return true;
}

bool settings_is_haptics_enabled(void) {
    return true;
}

uint8_t settings_effect_index(void) {
    return preview_effect;
}

static bool select_effect(const char *name) {
    for (uint8_t index = 0; index < EFFECT_COUNT; index++) {
        if (strcmp(EFFECTS[index]->name, name) == 0) {
            preview_effect = index;
            return true;
        }
    }

    return false;
}

static bool is_inside_panel(int x, int y) {
    const float radius = CANVAS_WIDTH / 2.0f;
    const float dx = (float)x + 0.5f - radius;
    const float dy = (float)y + 0.5f - radius;

    return dx * dx + dy * dy <= radius * radius;
}

static bool encode_frame(gif_writer_t *gif) {
    const uint16_t *pixels = canvas_pixels();
    for (int y = 0; y < CANVAS_HEIGHT; y++) {
        for (int x = 0; x < CANVAS_WIDTH; x++) {
            const int at = y * CANVAS_WIDTH + x;
            frame_pixels[at] = is_inside_panel(x, y) ? pixels[at] : SURROUND_RGB565;
        }
    }

    return gif_frame(gif, frame_pixels, FRAME_CENTISECONDS);
}

static int render_reveal(const char *answer, const char *modifier, const char *caption,
                         gif_writer_t *gif) {
    roll_t roll;
    memset(&roll, 0, sizeof(roll));
    snprintf(roll.answer, sizeof(roll.answer), "%s", answer);
    roll.modifier = strcmp(modifier, "-") == 0 ? NULL : modifier;
    roll.kind = (strcmp(answer, "YES") == 0 || strcmp(answer, "NO") == 0) ? DIE_ORACLE
                                                                           : DIE_NUMERIC;

    reveal_begin(&roll, 0);
    int frames = 0;
    for (uint32_t now = 0; now <= 1600; now += FRAME_INTERVAL_MS) {
        canvas_fill(theme_active()->background);
        reveal_draw(now, 255);
        reveal_draw_caption(caption, 255);
        if (!encode_frame(gif)) {
            return -1;
        }

        frames++;
    }

    // frame_pixels still holds the last frame; repeating it extends its delay.
    if (!gif_frame(gif, frame_pixels, REVEAL_END_HOLD_MS / 10)) {
        return -1;
    }

    return frames;
}

static int render_boot(gif_writer_t *gif) {
    boot_begin(0);
    int frames = 0;
    for (uint32_t now = 0; boot_is_running(now); now += FRAME_INTERVAL_MS) {
        boot_tick(now);
        canvas_fill(theme_active()->background);
        boot_draw(now);
        if (!encode_frame(gif)) {
            return -1;
        }

        frames++;
    }

    return frames;
}

/**
 * One full turn of the knob, driven through the mode machine so the GIF shows
 * the firmware's own transitions: boot is stepped through unrecorded, then the
 * armed screen, one click per die around the whole list, and the second of
 * stillness that settles the choice back into the armed screen.
 */
static int render_menu(gif_writer_t *gif) {
    const mode_input_t nothing = {0, false};
    uint8_t oracle = 0;
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        if (DICE[index].kind == DIE_ORACLE) {
            oracle = index;
        }
    }

    mode_begin(0, oracle, 120000);
    uint32_t now = 0;
    while (mode_current() == MODE_BOOT) {
        mode_step(now, nothing);
        now += FRAME_INTERVAL_MS;
    }

    uint32_t next_click = now + MENU_LEAD_MS;
    const uint32_t last_click = next_click + (uint32_t)(DIE_COUNT - 1) * MENU_STEP_MS;
    const uint32_t end = last_click + MENU_SETTLE_MS;
    int frames = 0;
    for (; now <= end; now += FRAME_INTERVAL_MS) {
        mode_input_t input = nothing;
        if (now >= next_click && next_click <= last_click) {
            input.detents = 1;
            next_click += MENU_STEP_MS;
        }

        mode_step(now, input);
        if (!encode_frame(gif)) {
            return -1;
        }

        frames++;
    }

    return frames;
}

// One throw: the coin at rest on a face, then tumbling and landing on another.
static int render_coin(gif_writer_t *gif) {
    const uint32_t rest_ms = 400;
    const uint32_t throw_ms = 1800;
    int frames = 0;

    // A flip long enough ago to have settled is how a resting coin is posed.
    coin_flip(1, 0);
    for (uint32_t now = 900; now < 900 + rest_ms; now += FRAME_INTERVAL_MS) {
        canvas_fill(theme_active()->background);
        coin_draw(now, 255);
        reveal_draw_caption("D2", 255);
        if (!encode_frame(gif)) {
            return -1;
        }

        frames++;
    }

    coin_flip(2, 900 + rest_ms);
    for (uint32_t now = 900 + rest_ms; now < 900 + rest_ms + throw_ms; now += FRAME_INTERVAL_MS) {
        canvas_fill(theme_active()->background);
        coin_draw(now, 255);
        reveal_draw_caption("D2", 255);

        if (!encode_frame(gif)) {
            return -1;
        }

        frames++;
    }

    return frames;
}

static void usage(void) {
    fprintf(stderr,
            "usage: preview reveal <theme> <effect> <answer> <modifier|-> <caption> <output.gif>\n");
    fprintf(stderr, "       preview boot <theme> <output.gif>\n");
    fprintf(stderr, "       preview menu <theme> <output.gif>\n");
    fprintf(stderr, "       preview coin <theme> <output.gif>\n");
}

int main(int argument_count, char **arguments) {
    const bool boot = argument_count == 4 && strcmp(arguments[1], "boot") == 0;
    const bool menu = argument_count == 4 && strcmp(arguments[1], "menu") == 0;
    const bool coin = argument_count == 4 && strcmp(arguments[1], "coin") == 0;
    const bool reveal = argument_count == 8 && strcmp(arguments[1], "reveal") == 0;
    if (!boot && !menu && !coin && !reveal) {
        usage();
        return 1;
    }

    for (uint8_t index = 0; index < THEME_COUNT; index++) {
        if (strcmp(THEMES[index].name, arguments[2]) == 0) {
            theme_select(index);
        }
    }

    if (reveal && !select_effect(arguments[3])) {
        fprintf(stderr, "preview: no effect named %s; the table holds", arguments[3]);
        for (uint8_t index = 0; index < EFFECT_COUNT; index++) {
            fprintf(stderr, " %s", EFFECTS[index]->name);
        }

        fputc('\n', stderr);
        return 1;
    }

    if (!canvas_begin()) {
        fprintf(stderr, "preview: canvas allocation failed\n");
        return 1;
    }

    const char *path = arguments[argument_count - 1];
    gif_writer_t gif;
    if (!gif_begin(&gif, path, CANVAS_WIDTH, CANVAS_HEIGHT)) {
        perror(path);
        return 1;
    }

    int frames;
    if (boot) {
        frames = render_boot(&gif);
    } else if (menu) {
        frames = render_menu(&gif);
    } else if (coin) {
        frames = render_coin(&gif);
    } else {
        frames = render_reveal(arguments[4], arguments[5], arguments[6], &gif);
    }

    if (!gif_end(&gif) || frames < 0) {
        fprintf(stderr, "preview: writing %s failed\n", path);
        return 1;
    }

    printf("wrote %s (%d frames)\n", path, frames);
    return 0;
}
