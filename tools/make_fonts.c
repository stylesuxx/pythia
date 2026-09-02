// Rasterizes every font the themes need into C source.
//
// Each glyph is stored as 4-bit alpha coverage, packed two pixels per byte with
// the high nibble first and each row padded to a whole byte. The device expands
// a nibble back to 0-255 by multiplying by 17.
//
// Adding a theme means adding rows to FONTS and running `make fonts`.

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#define FONT_DIRECTORY "/usr/share/fonts/truetype/dejavu"

// NOSYE spells the two oracle answers; die results use the larger number face.
#define ANSWER_CHARACTERS "NOSYE"
#define NUMBER_CHARACTERS "0123456789"
// Die names (D2 through D100, ORACLE) and the oracle modifiers (and, but).
#define LABEL_CHARACTERS "0123456789ACDELORabdnut"

// The boot wordmark, plus the glyphs a position cycles through before it
// settles. src/boot.c owns the scramble set and must stay within this one.
#define BOOT_WORDMARK_CHARACTERS "PYTHIA/0123456789ABCDEFXZ#%&"
#define BOOT_CAPTION_CHARACTERS "DELPHI SYSTEMS"

typedef struct {
    const char *name;
    const char *typeface;
    int size; // em size in pixels
    const char *characters;
} font_spec_t;

static const font_spec_t FONTS[] = {
    {"font_boot_wordmark", "DejaVuSansMono-Bold.ttf", 54, BOOT_WORDMARK_CHARACTERS},
    {"font_boot_caption", "DejaVuSansMono-Bold.ttf", 22, BOOT_CAPTION_CHARACTERS},
    {"font_midnight_answer", "DejaVuSans-Bold.ttf", 96, ANSWER_CHARACTERS},
    {"font_midnight_number", "DejaVuSans-Bold.ttf", 120, NUMBER_CHARACTERS},
    {"font_midnight_label", "DejaVuSans-Bold.ttf", 44, LABEL_CHARACTERS},
    {"font_midnight_caption", "DejaVuSans-Bold.ttf", 28, LABEL_CHARACTERS},
    {"font_parchment_answer", "DejaVuSerif-Bold.ttf", 96, ANSWER_CHARACTERS},
    {"font_parchment_number", "DejaVuSerif-Bold.ttf", 120, NUMBER_CHARACTERS},
    {"font_parchment_label", "DejaVuSerif-Bold.ttf", 44, LABEL_CHARACTERS},
    {"font_parchment_caption", "DejaVuSerif-Bold.ttf", 28, LABEL_CHARACTERS},
};

#define FONT_COUNT (sizeof(FONTS) / sizeof(FONTS[0]))
#define BYTES_PER_LINE 20

typedef struct {
    uint8_t *data;
    size_t size;
    size_t capacity;
} byte_buffer_t;

static void buffer_append(byte_buffer_t *buffer, uint8_t byte) {
    if (buffer->size == buffer->capacity) {
        buffer->capacity = buffer->capacity ? buffer->capacity * 2 : 4096;
        buffer->data = realloc(buffer->data, buffer->capacity);
        if (buffer->data == NULL) {
            fprintf(stderr, "make_fonts: out of memory\n");
            exit(1);
        }
    }

    buffer->data[buffer->size++] = byte;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        perror(path);
        return NULL;
    }

    fseek(file, 0, SEEK_END);
    const long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    uint8_t *data = malloc((size_t)length);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        perror(path);
        free(data);
        fclose(file);
        return NULL;
    }

    fclose(file);
    *size = (size_t)length;

    return data;
}

// Pack 8-bit coverage into 4-bit pairs, one row at a time.
static void pack_coverage(byte_buffer_t *packed, const uint8_t *pixels, int width, int height) {
    for (int row = 0; row < height; row++) {
        const uint8_t *line = pixels + row * width;
        for (int column = 0; column < width; column += 2) {
            const uint8_t high = line[column] >> 4;
            const uint8_t low = column + 1 < width ? line[column + 1] >> 4 : 0;
            buffer_append(packed, (uint8_t)((high << 4) | low));
        }
    }
}

static void write_bytes(FILE *out, const byte_buffer_t *bytes) {
    for (size_t start = 0; start < bytes->size; start += BYTES_PER_LINE) {
        fputs("   ", out);

        const size_t end = start + BYTES_PER_LINE < bytes->size ? start + BYTES_PER_LINE : bytes->size;
        for (size_t index = start; index < end; index++) {
            fprintf(out, " 0x%02x,", bytes->data[index]);
        }

        fputc('\n', out);
    }
}

static bool generate_font(const font_spec_t *spec, const char *output_directory) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", FONT_DIRECTORY, spec->typeface);
    size_t file_size;
    uint8_t *file = read_file(path, &file_size);
    if (file == NULL) {
        return false;
    }

    stbtt_fontinfo info;
    if (!stbtt_InitFont(&info, file, stbtt_GetFontOffsetForIndex(file, 0))) {
        fprintf(stderr, "%s: not a TrueType font\n", path);
        free(file);

        return false;
    }

    // The em maps to the requested pixel size, as a font size in pixels does
    // in FreeType. The ascent and descent round outwards to whole pixels.
    const float scale = stbtt_ScaleForMappingEmToPixels(&info, (float)spec->size);
    int ascent_units, descent_units, line_gap_units;
    stbtt_GetFontVMetrics(&info, &ascent_units, &descent_units, &line_gap_units);
    const int ascent = (int)ceilf((float)ascent_units * scale);
    const int descent = (int)ceilf((float)-descent_units * scale);

    snprintf(path, sizeof(path), "%s/%s.c", output_directory, spec->name);
    FILE *out = fopen(path, "w");
    if (out == NULL) {
        perror(path);
        free(file);

        return false;
    }

    byte_buffer_t coverage = {0};
    byte_buffer_t glyph_rows = {0};
    int glyph_count = 0;

    // Walking ASCII in order yields the glyph table sorted by codepoint with
    // duplicates in the character set collapsed.
    for (int codepoint = 0; codepoint < 128; codepoint++) {
        if (codepoint == 0 || strchr(spec->characters, codepoint) == NULL) {
            continue;
        }

        int width, height, left, top_offset;
        uint8_t *bitmap = stbtt_GetCodepointBitmap(&info, scale, scale, codepoint,
                                                   &width, &height, &left, &top_offset);
        int advance_units, left_bearing_units;
        stbtt_GetCodepointHMetrics(&info, codepoint, &advance_units, &left_bearing_units);

        char row[128];
        snprintf(row, sizeof(row), "    {0x%02x, %d, %d, %d, %d, %d, %zu},  // '%c'\n",
                 codepoint, width, height, left, -top_offset,
                 (int)lroundf((float)advance_units * scale), coverage.size, codepoint);
        for (const char *character = row; *character; character++) {
            buffer_append(&glyph_rows, (uint8_t)*character);
        }

        if (bitmap != NULL) {
            pack_coverage(&coverage, bitmap, width, height);
            stbtt_FreeBitmap(bitmap, NULL);
        }

        glyph_count++;
    }

    fputs("#include \"font.h\"\n\n", out);
    fprintf(out, "static const uint8_t %s_coverage[] = {\n", spec->name);
    write_bytes(out, &coverage);
    fputs("};\n\n", out);
    fprintf(out, "static const glyph_t %s_glyphs[] = {\n", spec->name);
    fwrite(glyph_rows.data, 1, glyph_rows.size, out);
    fputs("};\n\n", out);
    fprintf(out,
            "const font_t %s = {\n"
            "    .line_height = %d,\n"
            "    .ascent = %d,\n"
            "    .glyph_count = %d,\n"
            "    .glyphs = %s_glyphs,\n"
            "    .coverage = %s_coverage,\n"
            "};\n",
            spec->name, ascent + descent, ascent, glyph_count, spec->name, spec->name);
    fclose(out);

    printf("%s.c: %d glyphs, %zu bytes, ascent %d\n", spec->name, glyph_count, coverage.size,
           ascent);

    free(coverage.data);
    free(glyph_rows.data);
    free(file);

    return true;
}

static bool write_header(const char *output_directory) {
    char path[512];
    snprintf(path, sizeof(path), "%s/fonts.h", output_directory);
    FILE *out = fopen(path, "w");
    if (out == NULL) {
        perror(path);
        return false;
    }

    fputs("#pragma once\n\n#include \"font.h\"\n\n#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n", out);
    for (size_t index = 0; index < FONT_COUNT; index++) {
        fprintf(out, "extern const font_t %s;\n", FONTS[index].name);
    }

    fputs("\n#ifdef __cplusplus\n}\n#endif\n", out);
    fclose(out);
    printf("fonts.h: %zu declarations\n", FONT_COUNT);

    return true;
}

int main(int argument_count, char **arguments) {
    if (argument_count != 2) {
        fprintf(stderr, "usage: make_fonts <output-directory>\n");
        return 1;
    }

    for (size_t index = 0; index < FONT_COUNT; index++) {
        if (!generate_font(&FONTS[index], arguments[1])) {
            return 1;
        }
    }

    return write_header(arguments[1]) ? 0 : 1;
}
