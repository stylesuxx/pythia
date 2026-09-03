#include "user_files.h"

#include <stdio.h>
#include <stdlib.h>

#include "config.h"
#include "files.h"
#include "render/theme.h"
#include "theme_file.h"

#define THEME_FILE "theme.json"

bool user_files_apply(char *message, size_t message_capacity) {
    char *text;
    size_t length;
    if (!files_read(THEME_FILE, &text, &length)) {
        theme_apply_file(NULL);

        // The drive always carries the look in use, so there is something
        // to edit from: the built-in file, byte for byte.
        const bool wrote = files_write(THEME_FILE, theme_builtin_text());
        snprintf(message, message_capacity, "%s %s with the built-in look", THEME_FILE,
                 wrote ? "written" : "missing and could not be written");
        return true;
    }

    config_theme_t parsed;
    char reason[CONFIG_ERROR_CAPACITY];
    const bool valid = config_parse_theme(text, length, &parsed, reason, sizeof(reason));
    free(text);
    // A refused file changes nothing: the look in use stays.
    if (!valid) {
        snprintf(message, message_capacity, "%s: %s", THEME_FILE, reason);
        return false;
    }

    theme_apply_file(&parsed);
    snprintf(message, message_capacity, "%s applied%s%s", THEME_FILE,
             parsed.name[0] != '\0' ? ": " : "", parsed.name);

    return true;
}
