#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Port: the user's files on the drive, and the turns in which the firmware
 * holds it. FAT has no locking, so the two sides take turns: the computer
 * holds the drive whenever it is attached, and the firmware holds it between
 * files_open() and files_close(), during which the computer sees a drive with
 * no medium in it. hardware/drive.cpp satisfies it on the device; the host
 * adapters satisfy it with a drive that can be taken and carries nothing.
 */

// True once after the computer ejected the drive or the cable was pulled.
bool files_take_change(void);

bool files_open(void);
void files_close(void);
bool files_mkdir(const char *name);
bool files_read(const char *name, char **text, size_t *length);
bool files_write(const char *name, const char *text);

#ifdef __cplusplus
}
#endif
