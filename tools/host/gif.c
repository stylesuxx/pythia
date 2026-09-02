#include "gif.h"

#include <stdlib.h>
#include <string.h>

#define MAX_COLOURS 255 // one slot stays free for the transparent index
#define LZW_MAX_CODE 4096

// Colour depths tried in turn when a frame holds more distinct colours than a
// palette can carry. Each entry drops one bit from one channel of RGB565.
typedef struct {
    uint8_t red_bits;
    uint8_t green_bits;
    uint8_t blue_bits;
} depth_t;

static const depth_t DEPTHS[] = {
    {5, 6, 5}, {5, 5, 5}, {4, 5, 4}, {4, 4, 4}, {3, 4, 3},
    {3, 3, 3}, {2, 3, 2}, {2, 2, 2}, {1, 2, 1}, {1, 1, 1},
};

static uint16_t depth_mask(depth_t depth) {
    const uint16_t red = (uint16_t)((0x1Fu & ~((1u << (5 - depth.red_bits)) - 1)) << 11);
    const uint16_t green = (uint16_t)((0x3Fu & ~((1u << (6 - depth.green_bits)) - 1)) << 5);
    const uint16_t blue = (uint16_t)(0x1Fu & ~((1u << (5 - depth.blue_bits)) - 1));

    return red | green | blue;
}

static void write_u16(FILE *file, unsigned value) {
    fputc(value & 0xFF, file);
    fputc((value >> 8) & 0xFF, file);
}

// LZW output, packed least significant bit first into 255-byte sub-blocks.
typedef struct {
    FILE *file;
    uint8_t block[255];
    int block_length;
    uint32_t bits;
    int bit_count;
} bit_writer_t;

static void bits_flush_block(bit_writer_t *writer) {
    if (writer->block_length == 0) {
        return;
    }

    fputc(writer->block_length, writer->file);
    fwrite(writer->block, 1, (size_t)writer->block_length, writer->file);
    writer->block_length = 0;
}

static void bits_put(bit_writer_t *writer, uint32_t code, int width) {
    writer->bits |= code << writer->bit_count;
    writer->bit_count += width;
    while (writer->bit_count >= 8) {
        writer->block[writer->block_length++] = (uint8_t)(writer->bits & 0xFF);
        writer->bits >>= 8;
        writer->bit_count -= 8;
        if (writer->block_length == 255) {
            bits_flush_block(writer);
        }
    }
}

static void bits_end(bit_writer_t *writer) {
    if (writer->bit_count > 0) {
        writer->block[writer->block_length++] = (uint8_t)(writer->bits & 0xFF);
    }

    bits_flush_block(writer);
    fputc(0, writer->file);
}

typedef struct {
    bit_writer_t bits;

    // Child code for each (prefix code, next byte) pair; 0 marks none, since
    // code 0 is a literal and can never be a child.
    uint16_t *children;
    int minimum_code_size;
    int code_size;
    int clear_code;
    int end_code;
    int next_code;
} lzw_t;

static void lzw_clear(lzw_t *lzw) {
    memset(lzw->children, 0, (size_t)LZW_MAX_CODE * 256 * sizeof(uint16_t));
    lzw->code_size = lzw->minimum_code_size + 1;
    lzw->next_code = lzw->clear_code + 2;
}

static bool lzw_encode(FILE *file, const uint8_t *indices, size_t count, int minimum_code_size) {
    lzw_t lzw;
    memset(&lzw, 0, sizeof(lzw));
    lzw.bits.file = file;
    lzw.children = malloc((size_t)LZW_MAX_CODE * 256 * sizeof(uint16_t));
    if (lzw.children == NULL) {
        return false;
    }

    lzw.minimum_code_size = minimum_code_size;
    lzw.clear_code = 1 << minimum_code_size;
    lzw.end_code = lzw.clear_code + 1;

    fputc(minimum_code_size, file);
    lzw_clear(&lzw);
    bits_put(&lzw.bits, (uint32_t)lzw.clear_code, lzw.code_size);

    int prefix = indices[0];
    for (size_t index = 1; index < count; index++) {
        const uint8_t next = indices[index];
        const uint16_t child = lzw.children[prefix * 256 + next];
        if (child != 0) {
            prefix = child;
            continue;
        }

        bits_put(&lzw.bits, (uint32_t)prefix, lzw.code_size);

        // A decoder adds the same entry one code later than this, so the code
        // width grows when the entry just added reaches the current limit.
        const int added = lzw.next_code++;
        lzw.children[prefix * 256 + next] = (uint16_t)added;
        if (added == (1 << lzw.code_size) && lzw.code_size < 12) {
            lzw.code_size++;
        }

        if (added == LZW_MAX_CODE - 1) {
            bits_put(&lzw.bits, (uint32_t)lzw.clear_code, lzw.code_size);
            lzw_clear(&lzw);
        }

        prefix = next;
    }

    bits_put(&lzw.bits, (uint32_t)prefix, lzw.code_size);
    bits_put(&lzw.bits, (uint32_t)lzw.end_code, lzw.code_size);
    bits_end(&lzw.bits);

    free(lzw.children);
    return true;
}

static void expand_rgb565(uint16_t pixel, uint8_t *rgb) {
    rgb[0] = (uint8_t)(((pixel >> 11) & 0x1Fu) * 255u / 31u);
    rgb[1] = (uint8_t)(((pixel >> 5) & 0x3Fu) * 255u / 63u);
    rgb[2] = (uint8_t)((pixel & 0x1Fu) * 255u / 31u);
}

// Writes the pending frame as one image: a graphic control block, the changed
// bounding box with a local palette, and the LZW-coded indices.
static bool flush_pending(gif_writer_t *writer) {
    const int width = writer->width;
    const int height = writer->height;
    const uint16_t *frame = writer->pending;
    const uint16_t *previous = writer->has_written ? writer->written : NULL;

    int left = width, top = height, right = -1, bottom = -1;
    if (previous == NULL) {
        left = 0;
        top = 0;
        right = width - 1;
        bottom = height - 1;
    } else {
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                if (frame[y * width + x] != previous[y * width + x]) {
                    if (x < left) {
                      left = x;
                    }

                    if (x > right) {
                      right = x;
                    }

                    if (y < top) {
                      top = y;
                    }

                    if (y > bottom) {
                      bottom = y;
                    }
                }
            }
        }

        // A frame equal to the shown one still needs an image to carry its
        // delay; a single transparent pixel does that.
        if (right < 0) {
            left = 0;
            top = 0;
            right = 0;
            bottom = 0;
        }
    }

    const int box_width = right - left + 1;
    const int box_height = bottom - top + 1;
    const size_t box_pixels = (size_t)box_width * (size_t)box_height;

    uint8_t *indices = malloc(box_pixels);
    int16_t *index_of_colour = malloc(65536 * sizeof(int16_t));
    if (indices == NULL || index_of_colour == NULL) {
        free(indices);
        free(index_of_colour);

        return false;
    }

    uint16_t palette[MAX_COLOURS];
    int colour_count = 0;
    bool has_transparent = false;
    for (size_t depth = 0; depth < sizeof(DEPTHS) / sizeof(DEPTHS[0]); depth++) {
        const uint16_t mask = depth_mask(DEPTHS[depth]);
        memset(index_of_colour, 0xFF, 65536 * sizeof(int16_t));
        colour_count = 0;
        has_transparent = false;
        bool overflow = false;
        for (int y = top; y <= bottom && !overflow; y++) {
            for (int x = left; x <= right; x++) {
                const size_t at = (size_t)y * (size_t)width + (size_t)x;
                const size_t box_at = (size_t)(y - top) * (size_t)box_width + (size_t)(x - left);
                if (previous != NULL && frame[at] == previous[at]) {
                    indices[box_at] = MAX_COLOURS;
                    has_transparent = true;
                    continue;
                }

                const uint16_t colour = frame[at] & mask;
                if (index_of_colour[colour] < 0) {
                    if (colour_count == MAX_COLOURS) {
                        overflow = true;
                        break;
                    }

                    index_of_colour[colour] = (int16_t)colour_count;
                    palette[colour_count++] = colour;
                }
                indices[box_at] = (uint8_t)index_of_colour[colour];
            }
        }

        if (!overflow) {
            break;
        }
    }
    free(index_of_colour);

    // The transparent index sits just past the colours, so the table holds
    // colour_count + 1 entries when it is in use.
    const int transparent_index = colour_count;
    if (has_transparent) {
        for (size_t at = 0; at < box_pixels; at++) {
            if (indices[at] == MAX_COLOURS) {
                indices[at] = (uint8_t)transparent_index;
            }
        }
    }

    const int entries = colour_count + (has_transparent ? 1 : 0);
    int table_bits = 1;
    while ((1 << table_bits) < entries) {
        table_bits++;
    }
    const int minimum_code_size = table_bits < 2 ? 2 : table_bits;

    FILE *file = writer->file;
    // Graphic control extension: leave the previous image in place, so the
    // transparent pixels show through to it.
    fputc(0x21, file);
    fputc(0xF9, file);
    fputc(0x04, file);
    fputc((1 << 2) | (has_transparent ? 1 : 0), file);
    write_u16(file, (unsigned)writer->pending_delay);
    fputc(has_transparent ? transparent_index : 0, file);
    fputc(0x00, file);

    // Image descriptor with a local colour table.
    fputc(0x2C, file);
    write_u16(file, (unsigned)left);
    write_u16(file, (unsigned)top);
    write_u16(file, (unsigned)box_width);
    write_u16(file, (unsigned)box_height);
    fputc(0x80 | (table_bits - 1), file);
    for (int entry = 0; entry < (1 << table_bits); entry++) {
        uint8_t rgb[3] = {0, 0, 0};
        if (entry < colour_count) {
            expand_rgb565(palette[entry], rgb);
        }

        fwrite(rgb, 1, 3, file);
    }

    const bool encoded = lzw_encode(file, indices, box_pixels, minimum_code_size);
    free(indices);

    memcpy(writer->written, frame, (size_t)width * (size_t)height * sizeof(uint16_t));
    writer->has_written = true;
    writer->has_pending = false;

    return encoded && !ferror(file);
}

bool gif_begin(gif_writer_t *writer, const char *path, int width, int height) {
    memset(writer, 0, sizeof(*writer));
    writer->width = width;
    writer->height = height;
    const size_t frame_bytes = (size_t)width * (size_t)height * sizeof(uint16_t);
    writer->written = malloc(frame_bytes);
    writer->pending = malloc(frame_bytes);
    writer->file = fopen(path, "wb");
    if (writer->written == NULL || writer->pending == NULL || writer->file == NULL) {
        gif_end(writer);
        return false;
    }

    FILE *file = writer->file;
    fwrite("GIF89a", 1, 6, file);
    write_u16(file, (unsigned)width);
    write_u16(file, (unsigned)height);

    // A two-entry global colour table satisfies decoders that expect one;
    // every image carries its own table.
    fputc(0x80, file);
    fputc(0x00, file);
    fputc(0x00, file);
    static const uint8_t global_table[6] = {0, 0, 0, 0, 0, 0};
    fwrite(global_table, 1, sizeof(global_table), file);

    // Netscape looping extension, loop count 0 for forever.
    static const uint8_t loop[19] = {0x21, 0xFF, 0x0B, 'N', 'E', 'T', 'S', 'C', 'A', 'P',
                                     'E',  '2',  '.',  '0', 0x03, 0x01, 0x00, 0x00, 0x00};
    fwrite(loop, 1, sizeof(loop), file);
    return !ferror(file);
}

bool gif_frame(gif_writer_t *writer, const uint16_t *pixels, int centiseconds) {
    const size_t frame_bytes = (size_t)writer->width * (size_t)writer->height * sizeof(uint16_t);
    if (writer->has_pending) {
        if (memcmp(writer->pending, pixels, frame_bytes) == 0) {
            writer->pending_delay += centiseconds;
            return true;
        }

        if (!flush_pending(writer)) {
            return false;
        }
    }

    memcpy(writer->pending, pixels, frame_bytes);
    writer->pending_delay = centiseconds;
    writer->has_pending = true;

    return true;
}

bool gif_end(gif_writer_t *writer) {
    bool ok = true;
    if (writer->file != NULL) {
        if (writer->has_pending) {
            ok = flush_pending(writer);
        }

        fputc(0x3B, writer->file);
        ok = ok && !ferror(writer->file);
        if (fclose(writer->file) != 0) {
            ok = false;
        }

        writer->file = NULL;
    }

    free(writer->written);
    free(writer->pending);
    writer->written = NULL;
    writer->pending = NULL;

    return ok;
}
