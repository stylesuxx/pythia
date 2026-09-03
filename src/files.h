#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Port: the user's files on the drive. hardware/drive.cpp satisfies it on the
 * device, where a read is only possible while the firmware holds the
 * filesystem between the host's turns, and the host adapters satisfy it with
 * no files at all.
 */

bool files_read(const char *name, char **text, size_t *length);
bool files_write(const char *name, const char *text);

#ifdef __cplusplus
}
#endif
