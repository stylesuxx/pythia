#include "user_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "builtin_files.h"
#include "config.h"
#include "dice.h"
#include "files.h"
#include "render/theme.h"

#define THEME_FILE "theme.json"
#define LAYOUT_FILE "layout.json"
#define README_FILE "README.txt"
#define STATUS_FILE "STATUS.txt"

// What STATUS.txt said after the last turn.
static char status[USER_FILES_MESSAGE_CAPACITY] = "";

/*
 * A missing file restores the built-in and is written back, so the drive
 * always carries what is in use and there is something to edit from: the
 * built-in file, byte for byte.
 */
static void restore_built_in(const char *name, const char *built_in_text, char *line,
                             size_t capacity) {
    const bool wrote = files_write(name, built_in_text);
    snprintf(line, capacity, "%s %s with the built-in %s", name,
             wrote ? "written" : "missing and could not be written",
             strcmp(name, THEME_FILE) == 0 ? "look" : "dice");
}

static bool apply_theme(char *line, size_t capacity) {
    char *text;
    size_t length;
    if (!files_read(THEME_FILE, &text, &length)) {
        theme_apply_file(NULL);
        restore_built_in(THEME_FILE, theme_builtin_text(), line, capacity);
        return true;
    }

    config_theme_t parsed;
    char reason[CONFIG_ERROR_CAPACITY];
    const bool valid = config_parse_theme(text, length, &parsed, reason, sizeof(reason));
    free(text);
    // A refused file changes nothing: the look in use stays.
    if (!valid) {
        snprintf(line, capacity, "%s: %s", THEME_FILE, reason);
        return false;
    }

    theme_apply_file(&parsed);
    snprintf(line, capacity, "%s applied%s%s", THEME_FILE, parsed.name[0] != '\0' ? ": " : "",
             parsed.name);
    return true;
}

// After the theme, since the names are checked against its faces.
static bool apply_layout(char *line, size_t capacity) {
    char *text;
    size_t length;
    if (!files_read(LAYOUT_FILE, &text, &length)) {
        dice_apply_file(NULL);
        restore_built_in(LAYOUT_FILE, layout_builtin_text(), line, capacity);
        return true;
    }

    config_layout_t parsed;
    char reason[CONFIG_ERROR_CAPACITY];
    bool valid = config_parse_layout(text, length, &parsed, reason, sizeof(reason));
    free(text);
    if (valid) {
        valid = dice_check_drawable(&parsed, theme_active(), reason, sizeof(reason));
    }

    // A refused file changes nothing: the dice in use stay.
    if (!valid) {
        snprintf(line, capacity, "%s: %s", LAYOUT_FILE, reason);
        return false;
    }

    dice_apply_file(&parsed);
    snprintf(line, capacity, "%s applied: %u dice", LAYOUT_FILE, (unsigned)parsed.count);
    return true;
}

/*
 * README.txt takes CRLF line endings on the way, so every editor on every
 * computer reads it. The JSON files are written as they are, because what is
 * written back must be the built-in file byte for byte.
 */
static void write_with_crlf(const char *name, const char *text) {
    size_t newlines = 0;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        newlines += *cursor == '\n';
    }

    char *converted = malloc(strlen(text) + newlines + 1);
    if (converted == NULL) {
        return;
    }

    char *out = converted;
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == '\n') {
            *out++ = '\r';
        }

        *out++ = *cursor;
    }

    *out = '\0';
    files_write(name, converted);
    free(converted);
}

/*
 * One turn: the files, then what the drive says about them, README.txt at
 * every turn so a firmware update never leaves a stale copy and STATUS.txt
 * last, so it reports the whole turn.
 */
static user_files_result_t take_turn(void) {
    if (!files_open()) {
        return USER_FILES_QUIET;
    }

    char theme_line[CONFIG_ERROR_CAPACITY + 16];
    char layout_line[CONFIG_ERROR_CAPACITY + 16];
    const bool theme_ok = apply_theme(theme_line, sizeof(theme_line));
    const bool layout_ok = apply_layout(layout_line, sizeof(layout_line));
    snprintf(status, sizeof(status), "%s\r\n%s\r\n", theme_line, layout_line);

    write_with_crlf(README_FILE, readme_builtin_text());
    files_write(STATUS_FILE, status);
    files_close();

    return theme_ok && layout_ok ? USER_FILES_APPLIED : USER_FILES_REFUSED;
}

user_files_result_t user_files_begin(void) {
    return take_turn();
}

user_files_result_t user_files_step(void) {
    if (!files_take_change()) {
        return USER_FILES_QUIET;
    }

    return take_turn();
}

const char *user_files_status(void) {
    return status;
}
