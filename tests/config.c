/*
 * The user's theme file, parsed on the host. A full file lands on every
 * screen's palette, a partial one keeps what it does not name, a section key
 * left out follows the file's general role, and every kind of mistake is
 * refused with the key named and the look in use kept. The files port is scripted
 * here, so the whole path from bytes on the drive to the palette the canvas
 * draws with runs without a device. The settings file is held to its table
 * the same way: every key parsed, a missing key at its default, every mistake
 * named.
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
#include "builtin_files.h"
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

// The built-in theme's place on the drive, the one file scripted here.
#define THEME_PATH "themes/neon/theme.json"

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
    // The layout is written back too; only the theme is watched here.
    if (strcmp(name, THEME_PATH) == 0) {
        snprintf(written_name, sizeof(written_name), "%s", name);
        snprintf(written_text, sizeof(written_text), "%s", text);
    }

    return true;
}

static void serve(const char *text) {
    served_name = text != NULL ? THEME_PATH : NULL;
    served_text = text;
}

static bool parse(const char *text, config_theme_t *theme, char *error) {
    return config_parse_theme(text, strlen(text), theme, error, CONFIG_ERROR_CAPACITY);
}

// The message carries a line per file; the theme's is the first.
static bool apply(const char *text, char *error) {
    serve(text);
    const bool applied = user_files_begin() == USER_FILES_APPLIED;
    snprintf(error, USER_FILES_MESSAGE_CAPACITY, "%s", user_files_status());

    return applied;
}

#define WHITE CANVAS_RGB(0xFF, 0xFF, 0xFF)
#define RED CANVAS_RGB(0xFF, 0x00, 0x00)

// True when every colour of one theme matches the other's.
static bool has_same_palette(const theme_t *a, const theme_t *b) {
#define SAME_COLOR(section, stem, key, fallback) &&a->section.key == b->section.key
#define SAME_SECTION(section, LIST) LIST(SAME_COLOR)
    return true CONFIG_SECTIONS(SAME_SECTION);
}

/*
 * The built-in file is the one source of the palette, so it must name every
 * colour of every section and parse in full, and every one of them must land
 * in the field its row names; a device never falls back from it.
 */
static void check_built_in_file_is_complete(void) {
    config_theme_t parsed;
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
    EXPECT(parse(theme_builtin_text(), &parsed, error), "data/themes/neon/theme.json was refused: %s",
           error);
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        EXPECT(parsed.has_color[which], "data/themes/neon/theme.json leaves %s.%s unset",
               CONFIG_COLORS[which].section, CONFIG_COLORS[which].key);
    }

    theme_apply_file(NULL, NULL);
    const theme_t *theme = theme_active();
#define EXPECT_LANDED(section, stem, key, fallback)                                               \
    EXPECT(theme->section.key == parsed.color[CONFIG_##stem],                                     \
           #section "." #key " is not the file's");
#define EXPECT_SECTION_LANDED(section, LIST) LIST(EXPECT_LANDED)
    CONFIG_SECTIONS(EXPECT_SECTION_LANDED)
    EXPECT(strcmp(theme->name, CONFIG_BUILTIN_THEME) == 0, "the built-in theme is named %s",
           theme->name);
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
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
    EXPECT(parse("{\"coin\": {\"face\": \"#FFFFFF\"}, \"oracle\": {\"answer\": \"#ff0000\"}}",
                 &theme, error),
           "section keys were refused: %s", error);
    EXPECT(theme.has_color[CONFIG_COIN_FACE] && theme.color[CONFIG_COIN_FACE] == WHITE,
           "coin.face was not read");
    EXPECT(theme.has_color[CONFIG_ORACLE_ANSWER] && theme.color[CONFIG_ORACLE_ANSWER] == RED,
           "lower-case hex was not read");
    EXPECT(!theme.has_color[CONFIG_NUMBERS_TEXT], "an unnamed key was set");
}

static void check_general_roles_reach_the_screens(void) {
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
    theme_apply_file(NULL, NULL);
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
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
    theme_apply_file(NULL, NULL);
    EXPECT(apply("{\"colors\": {\"primary\": \"#FFFFFF\"}, \"numbers\": {\"text\": \"#FF0000\"}}",
                 error),
           "refused: %s", error);
    EXPECT(theme_active()->numbers.text == RED, "numbers.text lost to primary");
    EXPECT(theme_active()->oracle.answer == WHITE, "oracle.answer did not follow primary");
}

static void check_a_section_key_alone_keeps_the_rest(void) {
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
    theme_apply_file(NULL, NULL);
    const theme_t before = *theme_active();
    EXPECT(apply("{\"numbers\": {\"text\": \"#FF0000\"}}", error), "refused: %s", error);
    const theme_t *theme = theme_active();
    EXPECT(theme->numbers.text == RED, "numbers.text was not applied");
    EXPECT(theme->oracle.answer == before.oracle.answer, "oracle.answer changed");
    EXPECT(theme->coin.face == before.coin.face, "coin.face changed");
    EXPECT(theme->colors.background == before.colors.background, "background changed");
}

static void expect_refused(const char *text, const char *expected_error) {
    config_theme_t theme;
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
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
    // The folder is the theme's name; a name inside the file is one name too many.
    expect_refused("{\"name\": \"neon\"}", "name: unknown key");
    expect_refused("[1, 2]", "theme: expected an object at the top level");
    expect_refused("{\"name\": \"x\"", "theme: not valid JSON");
    expect_refused("", "theme: expected an object at the top level");
    expect_refused("{\"coin\": {\"face\": \"#000000\"}, \"extra\": {\"deep\": [1, {\"a\": 2}]}}",
                   "extra: unknown key");
    // An unknown key after a nested value is only found if the nested value
    // was skipped as one unit.
    expect_refused("{\"list\": {\"name\": \"#000000\", \"ring\": \"#111111\"}, \"after\": 1}",
                   "after: unknown key");
}

static void check_user_files_apply(void) {
    char error[USER_FILES_MESSAGE_CAPACITY] = "";
    theme_apply_file(NULL, NULL);
    const theme_t built_in = *theme_active();

    written_name[0] = '\0';
    EXPECT(apply(NULL, error), "no file was treated as an error: %s", error);
    EXPECT(has_same_palette(theme_active(), &built_in), "no file changed the palette");
    EXPECT(strcmp(written_name, THEME_PATH) == 0, "no theme was written back, got \"%s\"",
           written_name);
    EXPECT(strcmp(written_text, theme_builtin_text()) == 0,
           "the written-back file is not data/themes/neon/theme.json byte for byte");

    EXPECT(apply("{\"colors\": {\"background\": \"#F2E6C9\"}}", error), "refused: %s", error);
    EXPECT(theme_active()->colors.background == CANVAS_RGB(0xF2, 0xE6, 0xC9),
           "the palette was not applied");
    EXPECT(theme_active()->answer_font == built_in.answer_font, "the fonts did not carry over");

    EXPECT(apply("{\"numbers\": {\"text\": \"#FFFFFF\"}}", error), "refused: %s", error);
    EXPECT(theme_active()->colors.background == built_in.colors.background,
           "a colour the new file omits kept the previous file's value");

    // A refused file changes nothing: the look the last good file gave stays.
    EXPECT(!apply("{\"oracle\": {\"answer\": \"white\"}}", error), "a bad file was applied");
    static const char REFUSAL[] = THEME_PATH ": oracle.answer: expected \"#RRGGBB\"\r\n";
    EXPECT(strstr(error, REFUSAL) != NULL, "the error did not name the file and key: %s", error);
    EXPECT(theme_active()->numbers.text == WHITE, "a refused file dropped the look in use");

    EXPECT(apply(NULL, error), "no file was treated as an error: %s", error);
    EXPECT(has_same_palette(theme_active(), &built_in),
           "deleting the file did not restore the built-in look");
}

static bool parse_settings(const char *text, config_settings_t *settings, char *error) {
    return config_parse_settings(text, strlen(text), settings, error, CONFIG_ERROR_CAPACITY);
}

static bool has_same_settings(const config_settings_t *a, const config_settings_t *b) {
    return strcmp(a->theme, b->theme) == 0 && strcmp(a->layout, b->layout) == 0 &&
           a->display_rotated == b->display_rotated && a->haptics == b->haptics &&
           a->reverse_knob == b->reverse_knob && a->sleep_after == b->sleep_after &&
           a->brightness == b->brightness;
}

/*
 * The built-in settings file is the one source of the defaults, so it must
 * name every key and parse to exactly what a file naming nothing means.
 */
static void check_built_in_settings_are_the_defaults(void) {
    config_settings_t parsed;
    config_settings_t defaults;
    char error[CONFIG_ERROR_CAPACITY] = "";
    config_default_settings(&defaults);
    EXPECT(parse_settings(settings_builtin_text(), &parsed, error),
           "data/settings.json was refused: %s", error);
    EXPECT(has_same_settings(&parsed, &defaults), "data/settings.json differs from the defaults");
    EXPECT(strcmp(defaults.theme, CONFIG_BUILTIN_THEME) == 0 &&
               strcmp(defaults.layout, CONFIG_BUILTIN_LAYOUT) == 0,
           "the defaults name theme %s and layout %s", defaults.theme, defaults.layout);

    static const char *const KEYS[] = {"theme",        "layout",      "display_rotated", "haptics",
                                       "reverse_knob", "sleep_after", "brightness"};
    for (size_t index = 0; index < sizeof(KEYS) / sizeof(KEYS[0]); index++) {
        char quoted[32];
        snprintf(quoted, sizeof(quoted), "\"%s\"", KEYS[index]);
        EXPECT(strstr(settings_builtin_text(), quoted) != NULL, "data/settings.json leaves %s out",
               KEYS[index]);
    }
}

static void check_settings_keys_are_parsed(void) {
    config_settings_t settings;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse_settings("{\"theme\": \"dusk-2\", \"layout\": \"solo_game\", \"display_rotated\": false, "
                          "\"haptics\": false, \"reverse_knob\": true, \"sleep_after\": 0, "
                          "\"brightness\": 35}",
                          &settings, error),
           "a full settings file was refused: %s", error);
    EXPECT(strcmp(settings.theme, "dusk-2") == 0, "the theme is %s", settings.theme);
    EXPECT(strcmp(settings.layout, "solo_game") == 0, "the layout is %s", settings.layout);
    EXPECT(!settings.display_rotated && !settings.haptics && settings.reverse_knob,
           "the switches were not read");
    EXPECT(settings.sleep_after == 0 && settings.brightness == 35, "the numbers were not read");

    // A key left out means its default.
    config_settings_t defaults;
    config_default_settings(&defaults);
    EXPECT(parse_settings("{\"brightness\": 50}", &settings, error), "a partial file was refused: %s",
           error);
    EXPECT(settings.brightness == 50, "brightness was not read");
    settings.brightness = defaults.brightness;
    EXPECT(has_same_settings(&settings, &defaults), "a partial file changed a key it did not name");

    EXPECT(parse_settings("{}", &settings, error), "an empty object was refused: %s", error);
    EXPECT(has_same_settings(&settings, &defaults), "an empty file is not the defaults");
}

static void expect_settings_refused(const char *text, const char *expected_error) {
    config_settings_t settings;
    char error[CONFIG_ERROR_CAPACITY] = "";
    const bool accepted = parse_settings(text, &settings, error);
    EXPECT(!accepted, "accepted: %s", text);
    EXPECT(accepted || strcmp(error, expected_error) == 0, "refused with \"%s\", expected \"%s\"",
           error, expected_error);
}

static void check_settings_mistakes_are_named(void) {
    static const char NAME_RULE[] = "lower-case letters, digits, - and _, 1 to 16 characters";
    char expected[CONFIG_ERROR_CAPACITY];

    snprintf(expected, sizeof(expected), "theme: %s", NAME_RULE);
    expect_settings_refused("{\"theme\": \"Dusk\"}", expected);
    expect_settings_refused("{\"theme\": \"\"}", expected);
    expect_settings_refused("{\"theme\": 3}", expected);
    expect_settings_refused("{\"theme\": \"dusk theme\"}", expected);
    snprintf(expected, sizeof(expected), "layout: %s", NAME_RULE);
    expect_settings_refused("{\"layout\": \"seventeen_letters\"}", expected);
    expect_settings_refused("{\"layout\": \"a/b\"}", expected);

    expect_settings_refused("{\"haptics\": 1}", "haptics: expected true or false");
    expect_settings_refused("{\"display_rotated\": \"yes\"}", "display_rotated: expected true or false");
    expect_settings_refused("{\"reverse_knob\": null}", "reverse_knob: expected true or false");
    expect_settings_refused("{\"sleep_after\": -1}", "sleep_after: 0 to 86400");
    expect_settings_refused("{\"sleep_after\": 86401}", "sleep_after: 0 to 86400");
    expect_settings_refused("{\"sleep_after\": \"120\"}", "sleep_after: 0 to 86400");
    expect_settings_refused("{\"sleep_after\": 1.5}", "sleep_after: 0 to 86400");
    expect_settings_refused("{\"brightness\": 0}", "brightness: 1 to 100");
    expect_settings_refused("{\"brightness\": 101}", "brightness: 1 to 100");
    expect_settings_refused("{\"coin_enabled\": true}", "coin_enabled: unknown key");
    expect_settings_refused("{\"brigthness\": 50}", "brigthness: unknown key");
    expect_settings_refused("[]", "settings: expected an object at the top level");
    expect_settings_refused("{\"theme\": \"x\"", "settings: not valid JSON");
    expect_settings_refused("", "settings: expected an object at the top level");
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
    check_built_in_settings_are_the_defaults();
    check_settings_keys_are_parsed();
    check_settings_mistakes_are_named();

    if (failures > 0) {
        fprintf(stderr, "config: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("config: ok");
    return 0;
}
