#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "config.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The turn: the span in which the firmware holds the drive. It takes the
 * drive, reads settings.json, then the theme and the layout it names,
 * themes/<name>/theme.json as a palette laid over the built-in theme and
 * layouts/<name>.json as the dice the knob offers, writes README.txt and
 * STATUS.txt, and hands the drive back. Each file stands alone as far as it
 * can: a refused one changes nothing and names its key, a missing settings
 * file means the defaults, a missing built-in theme or layout is written
 * back so the drive always carries the defaults to copy from, and a name
 * with no file behind it leaves what is in use.
 */

// Room for one line per file, the path and key named on a refusal.
#define USER_FILES_MESSAGE_CAPACITY 512

typedef enum {
    USER_FILES_QUIET,   // no turn: the computer holds the drive, or it could not be taken
    USER_FILES_APPLIED, // a turn ran and every file present was accepted
    USER_FILES_REFUSED, // a turn ran and a file was refused; the status names it
} user_files_result_t;

user_files_result_t user_files_begin(void);
user_files_result_t user_files_step(void);
const char *user_files_status(void);

/**
 * The settings in use: the defaults until a turn reads settings.json, and
 * what the last accepted file said from then on. The shell hands each to the
 * module that acts on it after every turn.
 */
const config_settings_t *user_files_settings(void);

#ifdef __cplusplus
}
#endif
