#include "user_files.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ports/builtin_files.h"
#include "config.h"
#include "dice.h"
#include "ports/files.h"
#include "render/theme.h"

#define SETTINGS_FILE "settings.json"
#define THEMES_FOLDER "themes"
#define LAYOUTS_FOLDER "layouts"
#define README_FILE "README.txt"
#define STATUS_FILE "STATUS.txt"

// themes/<name>/theme.json or layouts/<name>.json, with the name at its widest.
#define PATH_CAPACITY 48
// One line of STATUS.txt: a path, a separator and the parser's reason.
#define LINE_CAPACITY (PATH_CAPACITY + CONFIG_ERROR_CAPACITY + 4)

// What STATUS.txt said after the last turn.
static char status[USER_FILES_MESSAGE_CAPACITY] = "";

// The settings in use: the defaults until a turn reads the file.
static bool initialised = false;
static config_settings_t active;

static void initialise(void) {
    if (initialised) {
        return;
    }

    initialised = true;
    config_default_settings(&active);
}

static void theme_folder(const char *name, char *path) {
    snprintf(path, PATH_CAPACITY, THEMES_FOLDER "/%s", name);
}

static void theme_path(const char *name, char *path) {
    snprintf(path, PATH_CAPACITY, THEMES_FOLDER "/%s/theme.json", name);
}

static void layout_path(const char *name, char *path) {
    snprintf(path, PATH_CAPACITY, LAYOUTS_FOLDER "/%s.json", name);
}

/*
 * A missing built-in file is written back, so the drive always carries the
 * defaults and there is something to edit from: the built-in file, byte for
 * byte.
 */
static void restore_built_in(const char *path, const char *built_in_text, const char *what,
                             char *line, size_t capacity) {
    const bool wrote = files_write(path, built_in_text);
    snprintf(line, capacity, "%s %s with the built-in %s", path,
             wrote ? "written" : "missing and could not be written", what);
}

// A built-in file under a name that is not in use is put back too, quietly.
static void keep_built_in(const char *path, const char *built_in_text) {
    char *text;
    size_t length;
    if (files_read(path, &text, &length)) {
        free(text);
        return;
    }

    files_write(path, built_in_text);
}

// A refused settings file changes no setting: the ones in use stay.
static bool apply_settings(char *line, size_t capacity) {
    char *text;
    size_t length;
    if (!files_read(SETTINGS_FILE, &text, &length)) {
        config_default_settings(&active);
        restore_built_in(SETTINGS_FILE, settings_builtin_text(), "settings", line, capacity);
        return true;
    }

    config_settings_t parsed;
    char reason[CONFIG_ERROR_CAPACITY];
    const bool valid = config_parse_settings(text, length, &parsed, reason, sizeof(reason));
    free(text);
    if (!valid) {
        snprintf(line, capacity, "%s: %s", SETTINGS_FILE, reason);
        return false;
    }

    active = parsed;
    snprintf(line, capacity, "%s applied", SETTINGS_FILE);
    return true;
}

/*
 * The theme the settings name. A refused or missing file changes nothing,
 * the look in use stays; the built-in name is the exception, since its file
 * is always there to be written back.
 */
static bool apply_theme(const char *name, char *line, size_t capacity) {
    char path[PATH_CAPACITY];
    theme_path(name, path);

    char *text;
    size_t length;
    if (!files_read(path, &text, &length)) {
        if (strcmp(name, CONFIG_BUILTIN_THEME) != 0) {
            snprintf(line, capacity, "%s: not on the drive", path);
            return false;
        }

        theme_apply_file(NULL, name);
        restore_built_in(path, theme_builtin_text(), "look", line, capacity);
        return true;
    }

    config_theme_t parsed;
    char reason[CONFIG_ERROR_CAPACITY];
    const bool valid = config_parse_theme(text, length, &parsed, reason, sizeof(reason));
    free(text);
    if (!valid) {
        snprintf(line, capacity, "%s: %s", path, reason);
        return false;
    }

    theme_apply_file(&parsed, name);
    snprintf(line, capacity, "%s applied", path);
    return true;
}

// After the theme, since the names are checked against its faces.
static bool apply_layout(const char *name, char *line, size_t capacity) {
    char path[PATH_CAPACITY];
    layout_path(name, path);

    char *text;
    size_t length;
    if (!files_read(path, &text, &length)) {
        if (strcmp(name, CONFIG_BUILTIN_LAYOUT) != 0) {
            snprintf(line, capacity, "%s: not on the drive", path);
            return false;
        }

        dice_apply_file(NULL);
        restore_built_in(path, layout_builtin_text(), "dice", line, capacity);
        return true;
    }

    config_layout_t parsed;
    char reason[CONFIG_ERROR_CAPACITY];
    bool valid = config_parse_layout(text, length, &parsed, reason, sizeof(reason));
    free(text);
    if (valid) {
        valid = dice_check_drawable(&parsed, theme_active(), reason, sizeof(reason));
    }

    if (!valid) {
        snprintf(line, capacity, "%s: %s", path, reason);
        return false;
    }

    dice_apply_file(&parsed);
    snprintf(line, capacity, "%s applied: %u dice", path, (unsigned)parsed.count);
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
 * One turn: the settings, which name the other two files; the folders, made
 * before anything is written into them; the theme, then the layout, whose
 * names are checked against the theme's faces; the built-in theme and layout
 * put back whenever they are missing; then what the drive says about it all,
 * README.txt at every turn so a firmware update never leaves a stale copy
 * and STATUS.txt last, so it reports the whole turn.
 */
static user_files_result_t take_turn(void) {
    if (!files_open()) {
        return USER_FILES_QUIET;
    }

    initialise();

    char settings_line[LINE_CAPACITY];
    char theme_line[LINE_CAPACITY];
    char layout_line[LINE_CAPACITY];
    const bool settings_ok = apply_settings(settings_line, sizeof(settings_line));

    char folder[PATH_CAPACITY];
    files_mkdir(THEMES_FOLDER);
    theme_folder(CONFIG_BUILTIN_THEME, folder);
    files_mkdir(folder);
    files_mkdir(LAYOUTS_FOLDER);

    const bool theme_ok = apply_theme(active.theme, theme_line, sizeof(theme_line));
    const bool layout_ok = apply_layout(active.layout, layout_line, sizeof(layout_line));

    char path[PATH_CAPACITY];
    if (strcmp(active.theme, CONFIG_BUILTIN_THEME) != 0) {
        theme_path(CONFIG_BUILTIN_THEME, path);
        keep_built_in(path, theme_builtin_text());
    }

    if (strcmp(active.layout, CONFIG_BUILTIN_LAYOUT) != 0) {
        layout_path(CONFIG_BUILTIN_LAYOUT, path);
        keep_built_in(path, layout_builtin_text());
    }

    snprintf(status, sizeof(status), "%s\r\n%s\r\n%s\r\n", settings_line, theme_line, layout_line);

    write_with_crlf(README_FILE, readme_builtin_text());
    files_write(STATUS_FILE, status);
    files_close();

    return settings_ok && theme_ok && layout_ok ? USER_FILES_APPLIED : USER_FILES_REFUSED;
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

const config_settings_t *user_files_settings(void) {
    initialise();
    return &active;
}
