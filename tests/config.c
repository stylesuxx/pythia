/*
 * The user's theme file, parsed on the host. A valid palette lands on the
 * active theme, a partial one keeps the built-in values it does not name, and
 * every kind of mistake is refused with the key named and nothing applied.
 * The files port is scripted here, so the whole path from bytes on the drive
 * to the palette the canvas draws with runs without a device.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "files.h"
#include "render/canvas.h"
#include "render/theme.h"
#include "theme_file.h"
#include "user_files.h"

static int failures = 0;

#define EXPECT(condition, ...)                                                                    \
    do {                                                                                          \
        if (!(condition)) {                                                                       \
            fputs("FAIL: ", stderr);                                                              \
            fprintf(stderr, __VA_ARGS__);                                                         \
            fputc('\n', stderr);                                                                  \
            failures++;                                                                           \
        }                                                                                         \
    } while (0)

// The drive's contents, scripted: one file or none.
static const char *served_name = NULL;
static const char *served_text = NULL;

bool files_read(const char *name, char **text, size_t *length) {
    if (served_name == NULL || strcmp(name, served_name) != 0) {
        return false;
    }

    *length = strlen(served_text);
    *text = malloc(*length + 1);
    memcpy(*text, served_text, *length + 1);
    return true;
}

// What the firmware wrote back to the drive, if anything.
static char written_name[32] = "";
static char written_text[1024] = "";

bool files_write(const char *name, const char *text) {
    snprintf(written_name, sizeof(written_name), "%s", name);
    snprintf(written_text, sizeof(written_text), "%s", text);
    return true;
}

static void serve(const char *text) {
    served_name = text != NULL ? "theme.json" : NULL;
    served_text = text;
}

static bool parse(const char *text, config_theme_t *theme, char *error) {
    return config_parse_theme(text, strlen(text), theme, error, CONFIG_ERROR_CAPACITY);
}

static const char FULL[] =
    "{\n"
    "  \"name\": \"parchment\",\n"
    "  \"colors\": {\n"
    "    \"background\": \"#F2E6C9\", \"answer\": \"#2B1D0E\", \"modifier\": \"#5A3E1B\",\n"
    "    \"label\": \"#7A5B2E\", \"ring\": \"#C9B58A\", \"ring_active\": \"#2b1d0e\"\n"
    "  }\n"
    "}\n";

static void check_full_palette_parses(void) {
    config_theme_t theme;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(FULL, &theme, error), "the full palette was refused: %s", error);
    EXPECT(strcmp(theme.name, "parchment") == 0, "name parsed as %s", theme.name);
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        EXPECT(theme.has_color[which], "colour %d was not set", which);
    }

    EXPECT(theme.color[CONFIG_COLOR_BACKGROUND] == CANVAS_RGB(0xF2, 0xE6, 0xC9),
           "background quantised to %04x", theme.color[CONFIG_COLOR_BACKGROUND]);
    EXPECT(theme.color[CONFIG_COLOR_RING_ACTIVE] == CANVAS_RGB(0x2B, 0x1D, 0x0E),
           "lower-case hex was not read");
}

static void check_partial_palette_keeps_the_rest(void) {
    config_theme_t theme;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse("{\"colors\": {\"answer\": \"#FFFFFF\"}}", &theme, error),
           "a one-colour palette was refused: %s", error);
    EXPECT(theme.has_color[CONFIG_COLOR_ANSWER], "the named colour was not set");
    EXPECT(!theme.has_color[CONFIG_COLOR_BACKGROUND], "an unnamed colour was set");
    EXPECT(theme.name[0] == '\0', "a missing name was filled in as %s", theme.name);

    theme_select(0);
    const uint16_t built_in_background = theme_active()->background;
    theme_apply_file(&theme);
    EXPECT(theme_active()->answer == CANVAS_RGB(0xFF, 0xFF, 0xFF), "the named colour was not applied");
    EXPECT(theme_active()->background == built_in_background, "an unnamed colour was changed");
    theme_reset();
}

static void expect_refused(const char *text, const char *expected_error) {
    config_theme_t theme;
    char error[CONFIG_ERROR_CAPACITY] = "";
    const bool accepted = parse(text, &theme, error);
    EXPECT(!accepted, "accepted: %s", text);
    EXPECT(accepted || strcmp(error, expected_error) == 0, "refused with \"%s\", expected \"%s\"",
           error, expected_error);
}

static void check_mistakes_are_named(void) {
    expect_refused("{\"colors\": {\"ring_actve\": \"#000000\"}}", "colors.ring_actve: unknown key");
    expect_refused("{\"colours\": {}}", "colours: unknown key");
    expect_refused("{\"colors\": {\"answer\": \"red\"}}", "colors.answer: expected \"#RRGGBB\"");
    expect_refused("{\"colors\": {\"answer\": \"#12345G\"}}", "colors.answer: expected \"#RRGGBB\"");
    expect_refused("{\"colors\": {\"answer\": 255}}", "colors.answer: expected \"#RRGGBB\"");
    expect_refused("{\"colors\": []}", "colors: expected an object");
    expect_refused("{\"name\": 7}", "name: expected a string");
    expect_refused("{\"name\": \"a name that runs on far too long\"}", "name: longer than 23 characters");
    expect_refused("[1, 2]", "theme: expected an object at the top level");
    expect_refused("{\"name\": \"x\"", "theme: not valid JSON");
    expect_refused("", "theme: expected an object at the top level");
    expect_refused("{\"colors\": {\"answer\": \"#000000\"}, \"name\": \"ok\", \"extra\": {\"deep\": [1, {\"a\": 2}]}}",
                   "extra: unknown key");
}

static void check_nested_values_are_skipped_whole(void) {
    // An unknown key after a nested value must be found, which needs the
    // nested value skipped as one unit.
    expect_refused("{\"colors\": {\"answer\": \"#000000\", \"ring\": \"#111111\"}, \"after\": 1}",
                   "after: unknown key");
}

// The built-in file is the one source of the palette, so it must name
// every colour and parse in full; a device never has to fall back from it.
static void check_built_in_file_is_complete(void) {
    config_theme_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(theme_builtin_text(), &parsed, error), "data/theme.json was refused: %s", error);
    EXPECT(parsed.name[0] != '\0', "data/theme.json has no name");
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        EXPECT(parsed.has_color[which], "data/theme.json leaves colour %d unset", which);
    }

    theme_select(0);
    EXPECT(theme_active()->background == parsed.color[CONFIG_COLOR_BACKGROUND],
           "the selected theme does not carry the built-in file's palette");
    EXPECT(strcmp(theme_active()->name, parsed.name) == 0, "the selected theme is named %s",
           theme_active()->name);
}

static void check_user_files_apply(void) {
    char error[CONFIG_ERROR_CAPACITY] = "";
    theme_select(0);

    const theme_t *built_in = theme_active();
    const uint16_t built_in_background = built_in->background;

    serve(NULL);
    written_name[0] = '\0';
    EXPECT(user_files_apply(error, sizeof(error)), "no file was treated as an error: %s", error);
    EXPECT(theme_active() == built_in, "no file changed the active theme");
    EXPECT(strcmp(written_name, "theme.json") == 0, "no theme.json was written back, got \"%s\"",
           written_name);
    EXPECT(strcmp(written_text, theme_builtin_text()) == 0,
           "the written-back file is not data/theme.json byte for byte");

    serve(FULL);
    EXPECT(user_files_apply(error, sizeof(error)), "the full palette was refused: %s", error);
    EXPECT(theme_active()->background == CANVAS_RGB(0xF2, 0xE6, 0xC9), "the palette was not applied");
    EXPECT(theme_active()->answer_font == THEMES[0].answer_font, "the fonts did not carry over");
    EXPECT(strcmp(theme_active()->name, "parchment") == 0, "the file's name was not taken");

    serve("{\"colors\": {\"answer\": \"#FFFFFF\"}}");
    EXPECT(user_files_apply(error, sizeof(error)), "the partial palette was refused: %s", error);
    EXPECT(theme_active()->background == built_in_background,
           "a colour the new file omits kept the previous file's value");

    serve("{\"colors\": {\"answer\": \"white\"}}");
    EXPECT(!user_files_apply(error, sizeof(error)), "a bad file was applied");
    EXPECT(strcmp(error, "theme.json: colors.answer: expected \"#RRGGBB\"") == 0,
           "the error did not name the file and key: %s", error);
    EXPECT(theme_active() == built_in, "a refused file left a palette applied");
}

int main(void) {
    check_full_palette_parses();
    check_partial_palette_keeps_the_rest();
    check_mistakes_are_named();
    check_nested_values_are_skipped_whole();
    check_built_in_file_is_complete();
    check_user_files_apply();

    if (failures > 0) {
        fprintf(stderr, "config: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("config: ok");
    return 0;
}
