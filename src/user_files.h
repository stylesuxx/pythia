#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The turn: the span in which the firmware holds the drive. It takes the
 * drive, reads the user's files and applies what they describe, theme.json as
 * a palette laid over the built-in theme and layout.json as the dice the knob
 * offers, writes README.txt and STATUS.txt, and hands the drive back. Each
 * file stands alone: a missing one restores the built-in and is written back
 * so the drive always carries what is in use, a refused one changes nothing
 * and names its key, and the other file still applies.
 */

// Room for one line per file, the file and key named on a refusal.
#define USER_FILES_MESSAGE_CAPACITY 256

typedef enum {
    USER_FILES_QUIET,   // no turn: the computer holds the drive, or it could not be taken
    USER_FILES_APPLIED, // a turn ran and every file present was accepted
    USER_FILES_REFUSED, // a turn ran and a file was refused; the status names it
} user_files_result_t;

user_files_result_t user_files_begin(void);
user_files_result_t user_files_step(void);
const char *user_files_status(void);

#ifdef __cplusplus
}
#endif
