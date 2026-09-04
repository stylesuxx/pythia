/*
 * The turn: the span in which the firmware holds the drive. Through a
 * scripted files port this proves that a step with nothing to do touches the
 * drive not at all, that a turn opens before its first read and closes after
 * its last write, that STATUS.txt is written inside the turn with the last
 * word and README.txt with CRLF line endings, that the JSON written back is
 * untouched by that, that a turn whose open fails is quiet, that the boot
 * takes a turn unasked, and that a turn with a refused file still reports
 * itself, since every turn the computer ends is followed by the boot sequence.
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtin_files.h"
#include "dice.h"
#include "files.h"
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

/*
 * The scripted drive: what the computer left on it, whether it can be
 * taken, and a record of every write in the order it was made. A read or a
 * write outside a turn is the promise broken, so the adapter fails the test
 * itself rather than answering.
 */
static const char *served_theme = NULL;
static const char *served_layout = NULL;
static bool change_pending = false;
static bool can_open = true;

static bool is_open = false;
static int opens = 0;
static int closes = 0;
static int reads = 0;

#define WRITE_CAPACITY 8
#define WRITE_TEXT_CAPACITY 4096

typedef struct {
    char name[32];
    char text[WRITE_TEXT_CAPACITY];
} write_t;

static write_t writes[WRITE_CAPACITY];
static int write_count = 0;

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

    const char *served = NULL;
    if (strcmp(name, "theme.json") == 0) {
        served = served_theme;
    } else if (strcmp(name, "layout.json") == 0) {
        served = served_layout;
    }

    if (!is_open || served == NULL) {
        return false;
    }

    *length = strlen(served);
    *text = malloc(*length + 1);
    memcpy(*text, served, *length + 1);

    return true;
}

bool files_write(const char *name, const char *text) {
    EXPECT(is_open, "%s was written outside a turn", name);
    if (!is_open || write_count == WRITE_CAPACITY) {
        return false;
    }

    write_t *record = &writes[write_count++];
    snprintf(record->name, sizeof(record->name), "%s", name);
    snprintf(record->text, sizeof(record->text), "%s", text);

    return true;
}

static const write_t *written(const char *name) {
    for (int index = 0; index < write_count; index++) {
        if (strcmp(writes[index].name, name) == 0) {
            return &writes[index];
        }
    }

    return NULL;
}

static void reset(void) {
    served_theme = NULL;
    served_layout = NULL;
    change_pending = false;
    can_open = true;
    is_open = false;
    opens = 0;
    closes = 0;
    reads = 0;
    write_count = 0;
    theme_apply_file(NULL);
    dice_apply_file(NULL);
}

static void check_a_quiet_step_touches_nothing(void) {
    reset();
    EXPECT(user_files_step() == USER_FILES_QUIET, "a step with no change took a turn");
    EXPECT(opens == 0 && reads == 0 && write_count == 0,
           "a quiet step touched the drive: %d opens, %d reads, %d writes", opens, reads,
           write_count);
}

static void check_a_turn_holds_the_drive(void) {
    reset();
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_APPLIED, "a turn with no files was not applied");
    EXPECT(opens == 1 && closes == 1, "the turn opened %d times and closed %d", opens, closes);
    EXPECT(reads == 2, "the turn read %d files; there are two", reads);
    EXPECT(write_count > 0 && strcmp(writes[write_count - 1].name, "STATUS.txt") == 0,
           "STATUS.txt was not the last thing written");
    EXPECT(written("README.txt") != NULL, "README.txt was not written during the turn");
    EXPECT(written("theme.json") != NULL && written("layout.json") != NULL,
           "the missing files were not written back");

    // The change is consumed by the turn that answered it.
    EXPECT(user_files_step() == USER_FILES_QUIET, "one change was answered by two turns");
    EXPECT(opens == 1, "the second step opened the drive again");
}

static void check_the_status_names_a_refusal(void) {
    reset();
    served_theme = "{\"numbers\": {\"text\": \"#FFFFFF\"}}";
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "a good theme was refused");
    EXPECT(theme_active()->numbers.text == WHITE, "the good theme was not applied");

    write_count = 0;
    served_theme = "{\"oracle\": {\"answer\": \"white\"}}";
    change_pending = true;

    /*
     * A refused turn is still a turn: the shell restarts the machine on it,
     * so the reader sees the boot sequence in the colours that stayed.
     */
    EXPECT(user_files_step() == USER_FILES_REFUSED, "a refused file did not report a refusal");
    EXPECT(theme_active()->numbers.text == WHITE, "a refused file dropped the look in use");

    const char *status = user_files_status();
    EXPECT(strncmp(status, "theme.json: oracle.answer: expected \"#RRGGBB\"\r\n", 47) == 0,
           "the status did not name the file and key: %s", status);
    EXPECT(strstr(status, "layout.json written with the built-in dice\r\n") != NULL,
           "the status did not report the layout's line: %s", status);

    const write_t *file = written("STATUS.txt");
    EXPECT(file != NULL, "STATUS.txt was not written");
    EXPECT(file != NULL && strcmp(file->text, status) == 0,
           "STATUS.txt differs from the status reported");
}

static void check_the_readme_is_written_with_crlf(void) {
    reset();
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the boot's turn was not applied");

    const write_t *file = written("README.txt");
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

    const write_t *theme = written("theme.json");
    const write_t *layout = written("layout.json");
    EXPECT(theme != NULL && strcmp(theme->text, theme_builtin_text()) == 0,
           "theme.json was not written back byte for byte");
    EXPECT(layout != NULL && strcmp(layout->text, layout_builtin_text()) == 0,
           "layout.json was not written back byte for byte");
}

static void check_a_turn_that_cannot_open_is_quiet(void) {
    reset();
    can_open = false;
    change_pending = true;
    EXPECT(user_files_step() == USER_FILES_QUIET, "a drive that could not be taken was reported");
    EXPECT(reads == 0 && write_count == 0 && closes == 0,
           "a failed open was followed by %d reads, %d writes and %d closes", reads,
           write_count, closes);

    EXPECT(user_files_begin() == USER_FILES_QUIET, "the boot's turn ran on a drive it could not take");
}

static void check_the_boot_takes_a_turn(void) {
    reset();
    EXPECT(user_files_begin() == USER_FILES_APPLIED, "the boot did not take a turn");
    EXPECT(opens == 1 && closes == 1, "the boot's turn opened %d times and closed %d", opens,
           closes);
}

int main(void) {
    check_a_quiet_step_touches_nothing();
    check_a_turn_holds_the_drive();
    check_the_status_names_a_refusal();
    check_the_readme_is_written_with_crlf();
    check_the_json_is_written_back_as_is();
    check_a_turn_that_cannot_open_is_quiet();
    check_the_boot_takes_a_turn();

    if (failures > 0) {
        fprintf(stderr, "user_files: %d failure%s\n", failures, failures == 1 ? "" : "s");
        return 1;
    }

    puts("user_files: ok");
    return 0;
}
