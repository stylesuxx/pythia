#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/**
 * Streams RGB565 frames to a looping GIF89a file.
 *
 * Frames arrive already quantised, so each image's palette is exactly the set
 * of colours it contains and nothing is dithered. Pixels that match the frame
 * before are written transparent and only the changed bounding box is stored,
 * and a frame identical to the last one extends its delay instead of being
 * written again.
 */
typedef struct {
    FILE *file;
    int width;
    int height;

    // the frame the file currently shows
    uint16_t *written;

    // held back so a repeat can extend its delay
    uint16_t *pending;

    int pending_delay;
    bool has_written;
    bool has_pending;
} gif_writer_t;

bool gif_begin(gif_writer_t *writer, const char *path, int width, int height);
bool gif_frame(gif_writer_t *writer, const uint16_t *pixels, int centiseconds);
bool gif_end(gif_writer_t *writer);
