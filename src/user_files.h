#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Room for one line per file, the file and key named on a refusal.
#define USER_FILES_MESSAGE_CAPACITY 256

/**
 * Reads the user's files and applies what they describe: theme.json as a
 * palette laid over the built-in theme, layout.json as the dice the knob
 * offers. Each file stands alone: a missing one restores the built-in and is
 * written back so the drive always carries what is in use, a refused one
 * changes nothing and names its key, and the other file still applies.
 * message gets one line per file; the result is false when any file was
 * refused.
 */
bool user_files_apply(char *message, size_t message_capacity);

#ifdef __cplusplus
}
#endif
