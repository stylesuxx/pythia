/*
 * The turn: the span in which the firmware holds the drive. Through a
 * scripted files port this proves that a step with nothing to do touches the
 * drive not at all, that a turn opens before its first read and closes after
 * its last write, that the folders are made before anything is written into
 * them, that STATUS.txt is written inside the turn with the last word and
 * README.txt with CRLF line endings, that the JSON written back is untouched
 * by that, that a turn whose open fails is quiet, that the boot takes a turn
 * unasked, and that a turn with a refused file still reports itself, since
 * every turn the computer ends is followed by the boot sequence. And the
 * settings: settings.json names the theme and the layout the turn reads, a
 * name with no file behind it leaves what is in use, a refused settings file
 * keeps every setting in use, and the settings a turn applied are reported.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/builtin_files.h"
#include "config.h"
#include "dice.h"
#include "ports/files.h"
#include "render/canvas.h"
#include "render/theme.h"
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

#define WHITE CANVAS_RGB(0xFF, 0xFF, 0xFF)

#define SETTINGS_PATH "settings.json"
#define BUILTIN_THEME_PATH "themes/neon/theme.json"
#define BUILTIN_LAYOUT_PATH "layouts/default.json"

/*
 * The scripted drive: what the computer left on it by path, whether it can
 * be taken, and a record of every write and every folder made in the order
 * they happened. A read or a write outside a turn is the promise broken, so
 * the adapter fails the test itself rather than answering.
 */
#define SERVED_CAPACITY 8

typedef struct {
    const char *path;
    const char *text;
} served_t;

static served_t served[SERVED_CAPACITY];
static int served_count = 0;

static bool change_pending = false;
static bool can_open = true;

static bool is_open = false;
static int opens = 0;
static int closes = 0;
static int reads = 0;

#define RECORD_CAPACITY 12
#define RECORD_TEXT_CAPACITY 4096

typedef struct {
    char name[48];
    char text[RECORD_TEXT_CAPACITY];
    bool folder;
} record_t;

static record_t records[RECORD_CAPACITY];
static int record_count = 0;

static void serve(const char *path, const char *text) {
    for (int index = 0; index < served_count; index++) {
        if (strcmp(served[index].path, path) == 0) {
            served[index].text = text;
            return;
        }
    }

    if (served_count < SERVED_CAPACITY) {
        served[served_count].path = path;
        served[served_count].text = text;
        served_count++;
    }
}

static void unserve(const char *path) {
    serve(path, NULL);
}

bool files_take_change(void) {
    const bool was_pending = change_pending;
    change_pending = false;
    return was_pending;
}

bool files_open(void) {
    EXPECT(!is_open, "the drive was opened twice");
    if (!can_open) {
        return false;
    }

    is_open = true;
    opens++;

    return true;
}

void files_close(void) {
    EXPECT(is_open, "the drive was closed without being open");
    is_open = false;
    closes++;
}

bool files_read(const char *name, char **text, size_t *length) {
    EXPECT(is_open, "%s was read outside a turn", name);
    reads++;

    const char *found = NULL;
    for (int index = 0; index < served_count; index++) {
        if (strcmp(served[index].path, name) == 0) {
            found = served[index].text;
        }
    }

    if (!is_open || found == NULL) {
        return false;
    }

    *length = strlen(found);
    *text = malloc(*length + 1);
    memcpy(*text, found, *length + 1);

    return true;
}

static record_t *record(const char *name, bool folder) {
    if (!is_open || record_count == RECORD_CAPACITY) {
        return NULL;
    }

    record_t *entry = &records[record_count++];
    snprintf(entry->name, sizeof(entry->name), "%s", name);
    entry->text[0] = '\0';
    entry->folder = folder;

    return entry;
}

bool files_mkdir(const char *name) {
    EXPECT(is_open, "folder %s was made outside a turn", name);
    return record(name, true) != NULL;
}

bool files_write(const char *name, const char *text) {
    EXPECT(is_open, "%s was written outside a turn", name);
    record_t *entry = record(name, false);
    if (entry == NULL) {
        return false;
    }

    snprintf(entry->text, sizeof(entry->text), "%s", text);
    return true;
}

// The position of a write or a folder in the record, or -1.
static int position_of(const char *name, bool folder) {
    for (int index = 0; index < record_count; index++) {
        if (records[index].folder == folder && strcmp(records[index].name, name) == 0) {
            return index;
        }
    }

    return -1;
}

static const record_t *written(const char *name) {
    const int position = position_of(name, false);
    return position < 0 ? NULL : &records[position];
}

static void clear_counters(void) {
    is_open = false;
    opens = 0;
    closes = 0;
    reads = 0;
    record_count = 0;
}

// A bare drive that can be taken, and the built-in look, dice and settings in use.
static void reset(void) {
    served_count = 0;
    change_pending = false;
    can_open = true;
    clear_counters();
    user_files_begin();
    clear_counters();
}

static void check_a_quiet_step_touches_nothing(void) {
    reset();
    EXPECT(user_files_step() == USER_FILES_QUIET, "a step with no change took a turn");
    EXPECT(opens == 0 && reads == 0 && record_count == 0,
           "a quiet step touched the drive: %d opens, %d reads, %d writes", opens, reads,
           record_count);
}

static void check_a_turn_holds_the_drive(void) {
    reset();
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_APPLIED, "a turn with no files was not applied");
    EXPECT(opens == 1 && closes == 1, "the turn opened %d times and closed %d", opens, closes);
    EXPECT(reads == 3, "the turn read %d files; there are three", reads);
    EXPECT(record_count > 0 && !records[record_count - 1].folder &&
               strcmp(records[record_count - 1].name, "STATUS.txt") == 0,
           "STATUS.txt was not the last thing written");
    EXPECT(written("README.txt") != NULL, "README.txt was not written during the turn");
    EXPECT(written(SETTINGS_PATH) != NULL && written(BUILTIN_THEME_PATH) != NULL &&
               written(BUILTIN_LAYOUT_PATH) != NULL,
           "the missing files were not written back");

    // The change is consumed by the turn that answered it.
    EXPECT(user_files_step() == USER_FILES_QUIET, "one change was answered by two turns");
    EXPECT(opens == 1, "the second step opened the drive again");
}

// A folder is made before the file that lives in it is written.
static void check_the_folders_come_first(void) {
    reset();
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the boot's turn was not applied");

    static const char *const FOLDERS[] = {"themes", "themes/neon", "layouts"};
    for (size_t index = 0; index < sizeof(FOLDERS) / sizeof(FOLDERS[0]); index++) {
        EXPECT(position_of(FOLDERS[index], true) >= 0, "folder %s was not made", FOLDERS[index]);
    }

    EXPECT(position_of("themes", true) < position_of("themes/neon", true) &&
               position_of("themes/neon", true) < position_of(BUILTIN_THEME_PATH, false),
           "the theme was written before its folders were made");
    EXPECT(position_of("layouts", true) < position_of(BUILTIN_LAYOUT_PATH, false),
           "the layout was written before its folder was made");
}

static void check_the_status_names_a_refusal(void) {
    reset();
    serve(BUILTIN_THEME_PATH, "{\"numbers\": {\"text\": \"#FFFFFF\"}}");
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "a good theme was refused");
    EXPECT(theme_active()->numbers.text == WHITE, "the good theme was not applied");

    record_count = 0;
    serve(BUILTIN_THEME_PATH, "{\"oracle\": {\"answer\": \"white\"}}");
    change_pending = true;

    /*
     * A refused turn is still a turn: the shell restarts the machine on it,
     * so the reader sees the boot sequence in the colours that stayed.
     */
    EXPECT(user_files_step() == USER_FILES_REFUSED, "a refused file did not report a refusal");
    EXPECT(theme_active()->numbers.text == WHITE, "a refused file dropped the look in use");

    const char *status = user_files_status();
    static const char FIRST_LINE[] = SETTINGS_PATH " written with the built-in settings\r\n";
    EXPECT(strncmp(status, FIRST_LINE, strlen(FIRST_LINE)) == 0,
           "the status does not open with the settings line: %s", status);
    EXPECT(strstr(status, BUILTIN_THEME_PATH ": oracle.answer: expected \"#RRGGBB\"\r\n") != NULL,
           "the status did not name the file and key: %s", status);
    EXPECT(strstr(status, BUILTIN_LAYOUT_PATH " written with the built-in dice\r\n") != NULL,
           "the status did not report the layout's line: %s", status);

    const record_t *file = written("STATUS.txt");
    EXPECT(file != NULL, "STATUS.txt was not written");
    EXPECT(file != NULL && strcmp(file->text, status) == 0,
           "STATUS.txt differs from the status reported");
}

static void check_the_readme_is_written_with_crlf(void) {
    reset();
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the boot's turn was not applied");

    const record_t *file = written("README.txt");
    EXPECT(file != NULL, "README.txt was not written");
    if (file == NULL) {
        return;
    }

    const char *built_in = readme_builtin_text();
    const char *cursor = file->text;
    bool same = true;
    for (const char *source = built_in; *source != '\0' && same; source++) {
        if (*source == '\n') {
            same = cursor[0] == '\r' && cursor[1] == '\n';
            cursor += 2;
        } else {
            same = *cursor == *source;
            cursor++;
        }
    }

    EXPECT(same && *cursor == '\0', "README.txt is not data/README.txt with CRLF line endings");
}

static void check_the_json_is_written_back_as_is(void) {
    reset();
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the boot's turn was not applied");

    const record_t *settings = written(SETTINGS_PATH);
    const record_t *theme = written(BUILTIN_THEME_PATH);
    const record_t *layout = written(BUILTIN_LAYOUT_PATH);
    EXPECT(settings != NULL && strcmp(settings->text, settings_builtin_text()) == 0,
           "settings.json was not written back byte for byte");
    EXPECT(theme != NULL && strcmp(theme->text, theme_builtin_text()) == 0,
           "the theme was not written back byte for byte");
    EXPECT(layout != NULL && strcmp(layout->text, layout_builtin_text()) == 0,
           "the layout was not written back byte for byte");
}

static void check_a_turn_that_cannot_open_is_quiet(void) {
    reset();
    can_open = false;
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_QUIET, "a drive that could not be taken was reported");
    EXPECT(reads == 0 && record_count == 0 && closes == 0,
           "a failed open was followed by %d reads, %d writes and %d closes", reads,
           record_count, closes);

    EXPECT(user_files_begin() == USER_FILES_QUIET, "the boot's turn ran on a drive it could not take");
}

static void check_the_boot_takes_a_turn(void) {
    reset();
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the boot did not take a turn");
    EXPECT(opens == 1 && closes == 1, "the boot's turn opened %d times and closed %d", opens,
           closes);
}

static const char DUSK[] = "{\"numbers\": {\"text\": \"#FFFFFF\"}}";
static const char SOLO[] =
    "{\"dice\": [{\"name\": \"D6\", \"kind\": \"numeric\", \"sides\": 6}, "
    "{\"name\": \"ORACLE\", \"kind\": \"oracle\"}]}";

// The theme and the layout a turn reads are the ones settings.json names.
static void check_the_settings_name_the_theme_and_the_layout(void) {
    reset();
    serve(SETTINGS_PATH, "{\"theme\": \"dusk\", \"layout\": \"solo\"}");
    serve("themes/dusk/theme.json", DUSK);
    serve("layouts/solo.json", SOLO);
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "a named theme and layout were refused");
    EXPECT(theme_active()->numbers.text == WHITE, "the named theme was not applied");
    EXPECT(strcmp(theme_active()->name, "dusk") == 0, "the theme in use is named %s",
           theme_active()->name);
    EXPECT(dice_count() == 2, "the named layout was not applied; %u dice", (unsigned)dice_count());
    EXPECT(strcmp(user_files_settings()->theme, "dusk") == 0, "the settings in use name theme %s",
           user_files_settings()->theme);

    const char *status = user_files_status();
    EXPECT(strstr(status, SETTINGS_PATH " applied\r\n") != NULL, "the settings line is missing: %s",
           status);
    EXPECT(strstr(status, "themes/dusk/theme.json applied\r\n") != NULL,
           "the theme line does not name the file: %s", status);
    EXPECT(strstr(status, "layouts/solo.json applied: 2 dice\r\n") != NULL,
           "the layout line does not name the file: %s", status);

    // The built-in files are still put back, so there is always something to copy from.
    EXPECT(written(BUILTIN_THEME_PATH) != NULL && written(BUILTIN_LAYOUT_PATH) != NULL,
           "the built-in files were not written back beside the named ones");
    EXPECT(written("themes/dusk/theme.json") == NULL && written("layouts/solo.json") == NULL,
           "a file the computer wrote was written over");
    EXPECT(written(SETTINGS_PATH) == NULL, "settings.json was written over");
}

// A name with no file behind it leaves what is in use, and the status says so.
static void check_a_missing_name_leaves_what_is_in_use(void) {
    reset();
    serve(SETTINGS_PATH, "{\"theme\": \"dusk\", \"layout\": \"solo\"}");
    serve("themes/dusk/theme.json", DUSK);
    serve("layouts/solo.json", SOLO);
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the named files were refused");

    unserve("themes/dusk/theme.json");
    unserve("layouts/solo.json");
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_REFUSED, "a missing named file was not reported");
    EXPECT(theme_active()->numbers.text == WHITE, "a missing theme dropped the look in use");
    EXPECT(dice_count() == 2, "a missing layout dropped the dice in use");

    const char *status = user_files_status();
    EXPECT(strstr(status, "themes/dusk/theme.json: not on the drive\r\n") != NULL,
           "the status does not say the theme is missing: %s", status);
    EXPECT(strstr(status, "layouts/solo.json: not on the drive\r\n") != NULL,
           "the status does not say the layout is missing: %s", status);

    // Naming the built-in ones again is the way back.
    serve(SETTINGS_PATH, "{\"theme\": \"neon\", \"layout\": \"default\"}");
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_APPLIED, "the built-in names were refused");
    EXPECT(theme_active()->numbers.text != WHITE, "the built-in look did not come back");
    EXPECT(dice_count() == 10, "the built-in dice did not come back");
}

// A refused settings file changes no setting, the names in use among them.
static void check_a_refused_settings_file_keeps_the_settings_in_use(void) {
    reset();
    serve(SETTINGS_PATH, "{\"theme\": \"dusk\", \"brightness\": 35}");
    serve("themes/dusk/theme.json", DUSK);
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the settings were refused");
    EXPECT(user_files_settings()->brightness == 35, "brightness was not applied");

    serve(SETTINGS_PATH, "{\"theme\": \"dusk\", \"brightness\": 0}");
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_REFUSED, "a bad settings file was not refused");
    EXPECT(user_files_settings()->brightness == 35, "a refused file changed brightness to %u",
           (unsigned)user_files_settings()->brightness);
    EXPECT(strcmp(user_files_settings()->theme, "dusk") == 0, "a refused file changed the theme");
    EXPECT(theme_active()->numbers.text == WHITE, "the theme in use was not read again");

    const char *status = user_files_status();
    static const char FIRST_LINE[] = SETTINGS_PATH ": brightness: 1 to 100\r\n";
    EXPECT(strncmp(status, FIRST_LINE, strlen(FIRST_LINE)) == 0,
           "the status does not name the key: %s", status);
    EXPECT(strstr(status, "themes/dusk/theme.json applied\r\n") != NULL,
           "the theme was not applied under the name in use: %s", status);
}

// What a turn applied is what the shell is told.
static void check_the_settings_are_reported(void) {
    reset();
    config_settings_t defaults;
    config_default_settings(&defaults);
    const config_settings_t *in_use = user_files_settings();
    EXPECT(strcmp(in_use->theme, defaults.theme) == 0 && in_use->haptics == defaults.haptics &&
               in_use->sleep_after == defaults.sleep_after &&
               in_use->brightness == defaults.brightness,
           "a bare drive did not leave the defaults in use");

    serve(SETTINGS_PATH, "{\"display_rotated\": false, \"haptics\": false, \"reverse_knob\": true, "
                         "\"sleep_after\": 0, \"brightness\": 35}");
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_APPLIED, "the settings were refused");
    EXPECT(!in_use->display_rotated && !in_use->haptics && in_use->reverse_knob &&
               in_use->sleep_after == 0 && in_use->brightness == 35,
           "the settings applied were not reported");
}

int main(void) {
    check_a_quiet_step_touches_nothing();
    check_a_turn_holds_the_drive();
    check_the_folders_come_first();
    check_the_status_names_a_refusal();
    check_the_readme_is_written_with_crlf();
    check_the_json_is_written_back_as_is();
    check_a_turn_that_cannot_open_is_quiet();
    check_the_boot_takes_a_turn();
    check_the_settings_name_the_theme_and_the_layout();
    check_a_missing_name_leaves_what_is_in_use();
    check_a_refused_settings_file_keeps_the_settings_in_use();
    check_the_settings_are_reported();

    if (failures > 0) {
        fprintf(stderr, "user_files: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("user_files: ok");
    return 0;
}
