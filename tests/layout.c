/*
 * The user's layout file, parsed on the host. The built-in file is the die
 * table the firmware ships; a user's file replaces it whole, with every entry
 * checked, its effect resolved against EFFECTS or the file's default, and
 * its name checked against the faces that draw it. Every mistake is refused
 * with the entry and key named, and a refused file changes nothing. The
 * files port is scripted here, so the whole path from bytes on the drive to
 * the dice the knob offers runs without a device.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/builtin_files.h"
#include "config.h"
#include "dice.h"
#include "ports/files.h"
#include "render/theme.h"
#include "scenes/effects/effect.h"
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

// The built-in layout's place on the drive, the one file scripted here.
#define LAYOUT_PATH "layouts/default.json"

// The drive's contents, scripted: a layout file or none; never a theme.
static const char *served_layout = NULL;

bool files_read(const char *name, char **text, size_t *length) {
    if (served_layout == NULL || strcmp(name, LAYOUT_PATH) != 0) {
        return false;
    }

    *length = strlen(served_layout);
    *text = malloc(*length + 1);
    memcpy(*text, served_layout, *length + 1);

    return true;
}

// What the firmware wrote back to the drive, by file.
static char written_layout[1024] = "";

bool files_write(const char *name, const char *text) {
    if (strcmp(name, LAYOUT_PATH) == 0) {
        snprintf(written_layout, sizeof(written_layout), "%s", text);
    }

    return true;
}

static bool parse(const char *text, config_layout_t *layout, char *error) {
    return config_parse_layout(text, strlen(text), layout, error, CONFIG_ERROR_CAPACITY);
}

static bool apply(const char *text, char *message) {
    served_layout = text;
    const bool applied = user_files_begin() == USER_FILES_APPLIED;
    snprintf(message, USER_FILES_MESSAGE_CAPACITY, "%s", user_files_status());

    return applied;
}

static const char CUSTOM[] =
    "{\n"
    "  \"default_effect\": \"slide\",\n"
    "  \"dice\": [\n"
    "    {\"name\": \"D20\", \"kind\": \"numeric\", \"sides\": 20},\n"
    "    {\"name\": \"D6\", \"kind\": \"numeric\", \"sides\": 6, \"effect\": \"tear\"},\n"
    "    {\"name\": \"ORACLE\", \"kind\": \"oracle\"},\n"
    "    {\"name\": \"D2\", \"kind\": \"coin\"},\n"
    "    {\"name\": \"D66\", \"kind\": \"d66\"}\n"
    "  ]\n"
    "}\n";

// The built-in file is the die table, so it must parse in full and be what
// a fresh table carries: ten dice, the coin first, the oracle last.
static void check_built_in_file_is_the_table(void) {
    config_layout_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(layout_builtin_text(), &parsed, error), "data/layouts/default.json was refused: %s", error);
    EXPECT(parsed.count == 10, "data/layout.json holds %u dice", (unsigned)parsed.count);

    dice_apply_file(NULL);
    EXPECT(dice_count() == parsed.count, "the table holds %u dice", (unsigned)dice_count());
    for (uint8_t index = 0; index < parsed.count && index < dice_count(); index++) {
        const die_t *die = &dice_active()[index];
        EXPECT(strcmp(die->name, parsed.dice[index].name) == 0, "die %u is %s", (unsigned)index,
               die->name);
        EXPECT(die->kind == parsed.dice[index].kind && die->sides == parsed.dice[index].sides,
               "die %s has the wrong kind or sides", die->name);
        EXPECT(die->effect == effect_index_of("tear"), "die %s arrives through effect %u",
               die->name, (unsigned)die->effect);
    }

    EXPECT(dice_active()[0].kind == DIE_COIN, "the first die is not the coin");
    EXPECT(dice_active()[dice_count() - 1].kind == DIE_ORACLE, "the last die is not the oracle");
    EXPECT(dice_check_drawable(&parsed, theme_active(), error, CONFIG_ERROR_CAPACITY),
           "a built-in name cannot be drawn: %s", error);
}

// The README the drive carries has to explain every file and folder on it.
static void check_the_readme_names_every_file(void) {
    const char *readme = readme_builtin_text();
    EXPECT(strstr(readme, "settings.json") != NULL, "the README does not mention settings.json");
    EXPECT(strstr(readme, "themes/neon/theme.json") != NULL,
           "the README does not mention themes/neon/theme.json");
    EXPECT(strstr(readme, "layouts/default.json") != NULL,
           "the README does not mention layouts/default.json");
    EXPECT(strstr(readme, "STATUS.txt") != NULL, "the README does not mention STATUS.txt");
}

static void check_a_custom_file_parses(void) {
    config_layout_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(CUSTOM, &parsed, error), "the custom layout was refused: %s", error);
    EXPECT(parsed.count == 5, "parsed %u dice", (unsigned)parsed.count);
    EXPECT(parsed.dice[0].kind == DIE_NUMERIC && parsed.dice[0].sides == 20, "D20 is wrong");
    EXPECT(parsed.dice[0].effect == effect_index_of("slide"), "D20 did not take the default effect");
    EXPECT(parsed.dice[1].effect == effect_index_of("tear"), "D6's own effect lost to the default");
    EXPECT(parsed.dice[3].kind == DIE_COIN && parsed.dice[3].sides == 2, "the coin has %u sides",
           (unsigned)parsed.dice[3].sides);
    EXPECT(parsed.dice[4].kind == DIE_D66, "D66 is not the d66 kind");
}

// Without a default the effects fall to the first row of EFFECTS.
static void check_the_default_effect_is_the_first_row(void) {
    config_layout_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse("{\"dice\": [{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 4}]}", &parsed,
                 error),
           "refused: %s", error);
    EXPECT(parsed.dice[0].effect == 0, "the effect defaulted to %u", (unsigned)parsed.dice[0].effect);
}

static void expect_refused(const char *text, const char *expected_error) {
    config_layout_t layout;
    char error[CONFIG_ERROR_CAPACITY] = "";
    const bool accepted = parse(text, &layout, error);
    EXPECT(!accepted, "accepted: %s", text);
    EXPECT(accepted || strcmp(error, expected_error) == 0, "refused with \"%s\", expected \"%s\"",
           error, expected_error);
}

#define D(body) "{\"dice\": [" body "]}"

static void check_mistakes_are_named(void) {
    expect_refused("[]", "layout: expected an object at the top level");
    expect_refused("{\"dice\": [", "layout: not valid JSON");
    expect_refused("{}", "dice: missing");
    expect_refused("{\"dice\": {}}", "dice: expected an array");
    expect_refused("{\"dice\": []}", "dice: at least one die");
    expect_refused("{\"dies\": []}", "dies: unknown key");
    expect_refused("{\"default_effect\": \"wobble\", \"dice\": []}",
                   "default_effect: no effect named wobble; the table holds slide, tear");
    expect_refused(D("7"), "dice[0]: expected an object");
    expect_refused(D("{\"kind\": \"numeric\", \"sides\": 4}"), "dice[0]: needs a name");
    expect_refused(D("{\"name\": \"D4\"}"), "dice[0]: needs a kind");
    expect_refused(D("{\"name\": 4, \"kind\": \"numeric\"}"), "dice[0].name: expected a string");
    expect_refused(D("{\"name\": \"\", \"kind\": \"numeric\"}"), "dice[0].name: 1 to 11 characters");
    expect_refused(D("{\"name\": \"D100000000000\", \"kind\": \"numeric\"}"),
                   "dice[0].name: 1 to 11 characters");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"cube\"}"), "dice[0].kind: numeric, coin, d66 or oracle");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\"}"), "dice[0].sides: a numeric die needs sides");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 1}"), "dice[0].sides: 2 to 100");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 101}"), "dice[0].sides: 2 to 100");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": \"4\"}"), "dice[0].sides: 2 to 100");
    expect_refused(D("{\"name\": \"D66\", \"kind\": \"d66\", \"sides\": 36}"),
                   "dice[0].sides: only a numeric die has sides");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 4, \"effect\": \"wobble\"}"),
                   "dice[0].effect: no effect named wobble; the table holds slide, tear");
    expect_refused(D("{\"name\": \"ORACLE\", \"kind\": \"oracle\", \"effect\": \"tear\"}"),
                   "dice[0].effect: the oracle has no effect");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 4, \"color\": 1}"),
                   "dice[0].color: unknown key");
    expect_refused(D("{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 4}, {\"name\": \"D6\"}"),
                   "dice[1]: needs a kind");

    char many[2048] = "{\"dice\": [";
    for (int index = 0; index < 17; index++) {
        strcat(many, index > 0 ? ", " : "");
        strcat(many, "{\"name\": \"D4\", \"kind\": \"numeric\", \"sides\": 4}");
    }
    strcat(many, "]}");
    expect_refused(many, "dice: more than 16 dice");
}

// A name the faces cannot draw is refused with the glyph named.
static void check_undrawable_names_are_refused(void) {
    config_layout_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(D("{\"name\": \"ATTACK\", \"kind\": \"numeric\", \"sides\": 20}"), &parsed, error),
           "refused: %s", error);
    EXPECT(!dice_check_drawable(&parsed, theme_active(), error, CONFIG_ERROR_CAPACITY),
           "ATTACK was accepted");
    EXPECT(strcmp(error, "dice[0].name: 'T' is not in the label face") == 0, "refused with: %s",
           error);

    EXPECT(parse(D("{\"name\": \"D30\", \"kind\": \"numeric\", \"sides\": 30}"), &parsed, error),
           "refused: %s", error);
    EXPECT(dice_check_drawable(&parsed, theme_active(), error, CONFIG_ERROR_CAPACITY),
           "D30 was refused: %s", error);

    // A name that would climb out of the rim caption's rows is refused too.
    EXPECT(parse(D("{\"name\": \"DDDDDDDDDDD\", \"kind\": \"numeric\", \"sides\": 30}"), &parsed,
                 error),
           "refused: %s", error);
    EXPECT(!dice_check_drawable(&parsed, theme_active(), error, CONFIG_ERROR_CAPACITY),
           "an eleven-glyph name was accepted");
    EXPECT(strcmp(error, "dice[0].name: too wide for the rim") == 0, "refused with: %s", error);
}

// A die is found by name, and a missing name falls to the oracle, or to the
// first die of a layout without one.
static void check_names_resolve(void) {
    dice_apply_file(NULL);
    EXPECT(strcmp(dice_active()[dice_index_of("D20")].name, "D20") == 0, "D20 was not found");
    EXPECT(dice_active()[dice_index_of("D7")].kind == DIE_ORACLE,
           "an unknown name did not fall to the oracle");

    config_layout_t parsed;
    char error[CONFIG_ERROR_CAPACITY] = "";
    EXPECT(parse(D("{\"name\": \"D8\", \"kind\": \"numeric\", \"sides\": 8}, "
                   "{\"name\": \"D12\", \"kind\": \"numeric\", \"sides\": 12}"),
                 &parsed, error),
           "refused: %s", error);
    dice_apply_file(&parsed);
    EXPECT(dice_count() == 2, "the file did not replace the table");
    EXPECT(dice_index_of("D12") == 1, "D12 was not found in the file's table");
    EXPECT(dice_index_of("D20") == 0, "a missing name in a layout without an oracle did not fall to the first die");

    dice_apply_file(NULL);
    EXPECT(dice_count() == 10, "restoring the built-in layout left %u dice", (unsigned)dice_count());
}

static void check_user_files_apply(void) {
    char message[USER_FILES_MESSAGE_CAPACITY] = "";
    dice_apply_file(NULL);

    written_layout[0] = '\0';
    EXPECT(apply(NULL, message), "no file was treated as an error: %s", message);
    EXPECT(dice_count() == 10, "no file changed the table");
    EXPECT(strcmp(written_layout, layout_builtin_text()) == 0,
           "the written-back file is not data/layouts/default.json byte for byte");
    EXPECT(strstr(message, LAYOUT_PATH " written with the built-in dice") != NULL,
           "the message did not say the file was written: %s", message);

    EXPECT(apply(CUSTOM, message), "the custom layout was refused: %s", message);
    EXPECT(dice_count() == 5 && strcmp(dice_active()[0].name, "D20") == 0, "the custom layout was not applied");
    EXPECT(dice_active()[0].effect == effect_index_of("slide") &&
               dice_active()[1].effect == effect_index_of("tear"),
           "the effects did not come through");
    EXPECT(strstr(message, LAYOUT_PATH " applied: 5 dice") != NULL, "the message was: %s", message);

    // A refused file changes nothing: the dice the last good file gave stay.
    EXPECT(!apply(D("{\"name\": \"ATTACK\", \"kind\": \"numeric\", \"sides\": 20}"), message),
           "an undrawable name was applied");
    EXPECT(strstr(message, LAYOUT_PATH ": dice[0].name: 'T' is not in the label face") != NULL,
           "the error did not name the file, entry and glyph: %s", message);
    EXPECT(dice_count() == 5, "a refused file dropped the dice in use");

    EXPECT(apply(NULL, message), "no file was treated as an error: %s", message);
    EXPECT(dice_count() == 10, "deleting the file did not restore the built-in dice");
}

int main(void) {
    check_built_in_file_is_the_table();
    check_the_readme_names_every_file();
    check_a_custom_file_parses();
    check_the_default_effect_is_the_first_row();
    check_mistakes_are_named();
    check_undrawable_names_are_refused();
    check_names_resolve();
    check_user_files_apply();

    if (failures > 0) {
        fprintf(stderr, "layout: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("layout: ok");
    return 0;
}
