// Renders the boot sequence, the die list or the reveal animation off-device by
// building the real canvas, font and animation sources against a host stub,
// then encodes the frames straight to an animated GIF.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "boot.h"
#include "canvas.h"
#include "menu.h"
#include "oracle.h"
#include "reveal.h"
#include "settings.h"
#include "theme.h"

#include "gif.h"

// GIF frame delays are whole centiseconds, so frames are sampled 20 ms apart
// and the file plays back at real speed.
#define FRAME_INTERVAL_MS 20
#define FRAME_CENTISECONDS (FRAME_INTERVAL_MS / 10)

// Time the finished reveal stays on screen before the GIF loops.
#define REVEAL_END_HOLD_MS 3000

// How long each die is held while stepping through the list, and the fade the
// firmware runs when the list first appears out of the dark.
#define MENU_STEP_MS 460
#define MENU_FADE_IN_MS 180

// Colour shown around the round panel, standing in for the bezel.
#define SURROUND_RGB565 0x18C3

static uint16_t frame_pixels[CANVAS_WIDTH * CANVAS_HEIGHT];

void haptics_begin(void) {}

void haptics_play(uint8_t effect) {
    (void)effect;
}

// Settings are device state; the preview pins them to the shipped defaults.
bool settings_is_display_rotated(void) {
    return true;
}

bool settings_is_haptics_enabled(void) {
    return true;
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

// One full turn of the knob: every die in the list, once, so the ring highlight
// travels the whole way round. Only the first entry fades in; the firmware
// swaps instantly between entries once the list is already lit.
static int render_menu(gif_writer_t *gif) {
    int frames = 0;

    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        for (uint32_t elapsed = 0; elapsed < MENU_STEP_MS; elapsed += FRAME_INTERVAL_MS) {
            uint8_t alpha = 255;
            if (index == 0 && elapsed < MENU_FADE_IN_MS) {
                alpha = (uint8_t)(255u * elapsed / MENU_FADE_IN_MS);
            }

            canvas_fill(theme_active()->background);
            menu_draw(index, alpha);
            if (!encode_frame(gif)) {
                return -1;
            }
            frames++;
        }
    }

    return frames;
}

static void usage(void) {
    fprintf(stderr, "usage: preview reveal <theme> <answer> <modifier|-> <caption> <output.gif>\n");
    fprintf(stderr, "       preview boot <theme> <output.gif>\n");
    fprintf(stderr, "       preview menu <theme> <output.gif>\n");
}

int main(int argument_count, char **arguments) {
    const bool boot = argument_count == 4 && strcmp(arguments[1], "boot") == 0;
    const bool menu = argument_count == 4 && strcmp(arguments[1], "menu") == 0;
    const bool reveal = argument_count == 7 && strcmp(arguments[1], "reveal") == 0;
    if (!boot && !menu && !reveal) {
        usage();
        return 1;
    }

    for (uint8_t index = 0; index < THEME_COUNT; index++) {
        if (strcmp(THEMES[index].name, arguments[2]) == 0) {
            theme_select(index);
        }
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
    } else {
        frames = render_reveal(arguments[3], arguments[4], arguments[5], &gif);
    }

    if (!gif_end(&gif) || frames < 0) {
        fprintf(stderr, "preview: writing %s failed\n", path);
        return 1;
    }

    printf("wrote %s (%d frames)\n", path, frames);
    return 0;
}
