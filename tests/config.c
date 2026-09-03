/*
 * The user's theme file, parsed on the host. A full file lands on every
 * screen's palette, a partial one keeps what it does not name, a section key
 * left out follows the file's general role, and every kind of mistake is
 * refused with the key named and nothing applied. The files port is scripted
 * here, so the whole path from bytes on the drive to the palette the canvas
 * draws with runs without a device.
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

static bool apply(const char *text, char *error) {
    serve(text);
    return user_files_apply(error, CONFIG_ERROR_CAPACITY);
}

#define WHITE CANVAS_RGB(0xFF, 0xFF, 0xFF)
#define RED CANVAS_RGB(0xFF, 0x00, 0x00)

// The built-in file is the one source of the palette, so it must name every
// colour of every section and parse in full; a device never falls back from it.
static void check_built_in_file_is_complete(void) {
    config_theme_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(theme_builtin_text(), &parsed, error), "data/theme.json was refused: %s", error);
    EXPECT(parsed.name[0] != '\0', "data/theme.json has no name");
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        EXPECT(parsed.has_color[which], "data/theme.json leaves %s.%s unset",
               CONFIG_COLORS[which].section, CONFIG_COLORS[which].key);
    }

    theme_select(0);
    const theme_t *theme = theme_active();
    EXPECT(theme->background == parsed.color[CONFIG_BACKGROUND], "background is not the file's");
    EXPECT(theme->boot.wordmark == parsed.color[CONFIG_BOOT_WORDMARK], "boot.wordmark is not the file's");
    EXPECT(theme->list.ring == parsed.color[CONFIG_LIST_RING], "list.ring is not the file's");
    EXPECT(theme->caption.text == parsed.color[CONFIG_CAPTION_TEXT], "caption.text is not the file's");
    EXPECT(theme->numbers.text == parsed.color[CONFIG_NUMBERS_TEXT], "numbers.text is not the file's");
    EXPECT(theme->oracle.modifier == parsed.color[CONFIG_ORACLE_MODIFIER],
           "oracle.modifier is not the file's");
    EXPECT(theme->coin.face == parsed.color[CONFIG_COIN_FACE], "coin.face is not the file's");
    EXPECT(strcmp(theme->name, parsed.name) == 0, "the selected theme is named %s", theme->name);
}

// Every general role is its own fallback and every section key falls back to
// a general role, so a file of general roles alone reaches every screen.
static void check_fallbacks_point_at_general_roles(void) {
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        const config_color_spec_t *spec = &CONFIG_COLORS[which];
        const bool general = strcmp(spec->section, "colors") == 0;
        EXPECT(general == ((int)spec->fallback == which), "%s.%s has the wrong kind of fallback",
               spec->section, spec->key);
        EXPECT(strcmp(CONFIG_COLORS[spec->fallback].section, "colors") == 0,
               "%s.%s falls back to something that is not a general role", spec->section, spec->key);
    }
}

static void check_a_section_key_is_parsed(void) {
    config_theme_t theme;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse("{\"coin\": {\"face\": \"#FFFFFF\"}, \"oracle\": {\"answer\": \"#ff0000\"}}",
                 &theme, error),
           "section keys were refused: %s", error);
    EXPECT(theme.has_color[CONFIG_COIN_FACE] && theme.color[CONFIG_COIN_FACE] == WHITE,
           "coin.face was not read");
    EXPECT(theme.has_color[CONFIG_ORACLE_ANSWER] && theme.color[CONFIG_ORACLE_ANSWER] == RED,
           "lower-case hex was not read");
    EXPECT(!theme.has_color[CONFIG_NUMBERS_TEXT], "an unnamed key was set");
    EXPECT(theme.name[0] == '\0', "a missing name was filled in as %s", theme.name);
}

static void check_general_roles_reach_the_screens(void) {
    char error[CONFIG_ERROR_CAPACITY] = "";
    theme_select(0);
    const uint16_t built_in_modifier = theme_active()->oracle.modifier;
    const uint16_t built_in_ring = theme_active()->list.ring;

    EXPECT(apply("{\"colors\": {\"primary\": \"#FFFFFF\"}}", error), "refused: %s", error);
    const theme_t *theme = theme_active();
    EXPECT(theme->numbers.text == WHITE, "numbers.text did not follow primary");
    EXPECT(theme->oracle.answer == WHITE, "oracle.answer did not follow primary");
    EXPECT(theme->coin.face == WHITE, "coin.face did not follow primary");
    EXPECT(theme->boot.wordmark == WHITE, "boot.wordmark did not follow primary");
    EXPECT(theme->oracle.modifier == built_in_modifier, "oracle.modifier moved with primary");
    EXPECT(theme->list.ring == built_in_ring, "list.ring moved with primary");
}

static void check_a_section_key_wins_over_its_role(void) {
    char error[CONFIG_ERROR_CAPACITY] = "";
    theme_select(0);
    EXPECT(apply("{\"colors\": {\"primary\": \"#FFFFFF\"}, \"numbers\": {\"text\": \"#FF0000\"}}",
                 error),
           "refused: %s", error);
    EXPECT(theme_active()->numbers.text == RED, "numbers.text lost to primary");
    EXPECT(theme_active()->oracle.answer == WHITE, "oracle.answer did not follow primary");
}

static void check_a_section_key_alone_keeps_the_rest(void) {
    char error[CONFIG_ERROR_CAPACITY] = "";
    theme_select(0);
    const theme_t before = *theme_active();
    EXPECT(apply("{\"numbers\": {\"text\": \"#FF0000\"}}", error), "refused: %s", error);
    const theme_t *theme = theme_active();
    EXPECT(theme->numbers.text == RED, "numbers.text was not applied");
    EXPECT(theme->oracle.answer == before.oracle.answer, "oracle.answer changed");
    EXPECT(theme->coin.face == before.coin.face, "coin.face changed");
    EXPECT(theme->background == before.background, "background changed");
    EXPECT(strcmp(theme->name, before.name) == 0, "the name changed to %s", theme->name);
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
    expect_refused("{\"colors\": {\"answer\": \"#000000\"}}", "colors.answer: unknown key");
    expect_refused("{\"boot\": {\"wordmrk\": \"#000000\"}}", "boot.wordmrk: unknown key");
    expect_refused("{\"numbers\": {\"face\": \"#000000\"}}", "numbers.face: unknown key");
    expect_refused("{\"colours\": {}}", "colours: unknown key");
    expect_refused("{\"oracle\": {\"answer\": \"red\"}}", "oracle.answer: expected \"#RRGGBB\"");
    expect_refused("{\"oracle\": {\"answer\": \"#12345G\"}}", "oracle.answer: expected \"#RRGGBB\"");
    expect_refused("{\"oracle\": {\"answer\": 255}}", "oracle.answer: expected \"#RRGGBB\"");
    expect_refused("{\"coin\": []}", "coin: expected an object");
    expect_refused("{\"name\": 7}", "name: expected a string");
    expect_refused("{\"name\": \"a name that runs on far too long\"}", "name: longer than 23 characters");
    expect_refused("[1, 2]", "theme: expected an object at the top level");
    expect_refused("{\"name\": \"x\"", "theme: not valid JSON");
    expect_refused("", "theme: expected an object at the top level");
    expect_refused("{\"coin\": {\"face\": \"#000000\"}, \"name\": \"ok\", \"extra\": {\"deep\": [1, {\"a\": 2}]}}",
                   "extra: unknown key");
    // An unknown key after a nested value is only found if the nested value
    // was skipped as one unit.
    expect_refused("{\"list\": {\"name\": \"#000000\", \"ring\": \"#111111\"}, \"after\": 1}",
                   "after: unknown key");
}

static void check_user_files_apply(void) {
    char error[CONFIG_ERROR_CAPACITY] = "";
    theme_select(0);
    const theme_t *built_in = theme_active();
    const uint16_t built_in_background = built_in->background;

    written_name[0] = '\0';
    EXPECT(apply(NULL, error), "no file was treated as an error: %s", error);
    EXPECT(theme_active() == built_in, "no file changed the active theme");
    EXPECT(strcmp(written_name, "theme.json") == 0, "no theme.json was written back, got \"%s\"",
           written_name);
    EXPECT(strcmp(written_text, theme_builtin_text()) == 0,
           "the written-back file is not data/theme.json byte for byte");

    EXPECT(apply("{\"name\": \"parchment\", \"colors\": {\"background\": \"#F2E6C9\"}}", error),
           "refused: %s", error);
    EXPECT(theme_active()->background == CANVAS_RGB(0xF2, 0xE6, 0xC9), "the palette was not applied");
    EXPECT(theme_active()->answer_font == THEMES[0].answer_font, "the fonts did not carry over");
    EXPECT(strcmp(theme_active()->name, "parchment") == 0, "the file's name was not taken");

    EXPECT(apply("{\"numbers\": {\"text\": \"#FFFFFF\"}}", error), "refused: %s", error);
    EXPECT(theme_active()->background == built_in_background,
           "a colour the new file omits kept the previous file's value");

    EXPECT(!apply("{\"oracle\": {\"answer\": \"white\"}}", error), "a bad file was applied");
    EXPECT(strcmp(error, "theme.json: oracle.answer: expected \"#RRGGBB\"") == 0,
           "the error did not name the file and key: %s", error);
    EXPECT(theme_active() == built_in, "a refused file left a palette applied");
}

int main(void) {
    check_built_in_file_is_complete();
    check_fallbacks_point_at_general_roles();
    check_a_section_key_is_parsed();
    check_general_roles_reach_the_screens();
    check_a_section_key_wins_over_its_role();
    check_a_section_key_alone_keeps_the_rest();
    check_mistakes_are_named();
    check_user_files_apply();

    if (failures > 0) {
        fprintf(stderr, "config: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("config: ok");
    return 0;
}
