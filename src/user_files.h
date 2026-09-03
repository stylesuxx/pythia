#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Reads the user's files and applies what they describe: theme.json as a
 * palette laid over the built-in theme. A missing file leaves the built-in in
 * place and is not an error. message says what happened either way; on
 * refusal it names the file and the key, the result is false, and nothing has
 * been applied.
 */
bool user_files_apply(char *message, size_t message_capacity);

#ifdef __cplusplus
}
#endif
