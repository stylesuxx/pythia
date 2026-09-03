/*
 * Every mark the firmware draws must be present in the face it is drawn with.
 * canvas_text() skips a missing glyph in silence, so a widened die list, a
 * reworded outcome, a new scramble glyph or a coin face the typeface does not
 * carry would otherwise fail only on the panel.
 */

#include <stdbool.h>
#include <stdio.h>

#include "scenes/boot.h"
#include "render/font.h"
#include "render/generated/fonts.h"
#include "oracle.h"
#include "render/theme.h"

static int failures = 0;

static void expect_glyphs(const font_t *font, const char *face, const char *text) {
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (font_find_glyph(font, (uint8_t)*cursor) == NULL) {
            fprintf(stderr, "FAIL: %s lacks '%c', needed to draw \"%s\"\n", face, *cursor, text);
            failures++;
        }
    }
}

static void expect_codepoint(const font_t *font, const char *face, uint32_t codepoint) {
    if (font_find_glyph(font, codepoint) == NULL) {
        fprintf(stderr, "FAIL: %s lacks U+%04X\n", face, codepoint);
        failures++;
    }
}

static void check_theme(const theme_t *theme) {
    char face[48];

    snprintf(face, sizeof(face), "%s label face", theme->name);
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        expect_glyphs(theme->label_font, face, DICE[index].name);
    }
    for (uint8_t index = 0; index < ORACLE_OUTCOME_COUNT; index++) {
        const roll_t outcome = oracle_outcome(index);
        if (outcome.modifier != NULL) {
            expect_glyphs(theme->label_font, face, outcome.modifier);
        }
    }

    snprintf(face, sizeof(face), "%s caption face", theme->name);
    for (uint8_t index = 0; index < DIE_COUNT; index++) {
        expect_glyphs(theme->caption_font, face, DICE[index].name);
    }

    snprintf(face, sizeof(face), "%s answer face", theme->name);
    for (uint8_t index = 0; index < ORACLE_OUTCOME_COUNT; index++) {
        expect_glyphs(theme->answer_font, face, oracle_outcome(index).answer);
    }

    snprintf(face, sizeof(face), "%s number face", theme->name);
    expect_glyphs(theme->number_font, face, "0123456789");

    // The two coin faces have to differ, or the D2 result is unreadable.
    snprintf(face, sizeof(face), "%s coin face", theme->name);
    expect_codepoint(theme->number_font, face, theme->coin_faces[0]);
    expect_codepoint(theme->number_font, face, theme->coin_faces[1]);
    if (theme->coin_faces[0] == theme->coin_faces[1]) {
        fprintf(stderr, "FAIL: %s strikes both coin faces with U+%04X\n", theme->name,
                theme->coin_faces[0]);
        failures++;
    }
}

int main(void) {
    check_theme(theme_active());

    expect_glyphs(&font_boot_wordmark, "boot wordmark face", BOOT_WORDMARK);
    expect_glyphs(&font_boot_wordmark, "boot wordmark face", BOOT_SCRAMBLE_CHARACTERS);
    expect_glyphs(&font_boot_caption, "boot caption face", BOOT_MANUFACTURER);

    if (failures > 0) {
        fprintf(stderr, "glyphs: %d missing\n", failures);
        return 1;
    }
    puts("glyphs: ok");
    return 0;
}
