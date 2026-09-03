#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The user's drive: the FAT data partition, shown to a host over USB mass
 * storage beside the serial port. FAT has no locking, so the two sides take
 * turns. The host owns the blocks whenever the drive is attached, and the
 * firmware takes the filesystem only between drive_open() and drive_close(),
 * during which the host sees a drive with no medium in it.
 */

/**
 * Formats the partition on first use, writes the README and registers the
 * mass storage interface. Call before USB.begin(). False when there is no
 * usable partition, in which case there is no drive and no user files.
 */
bool drive_begin(void);

// True once after the host ejected the drive or the cable was pulled.
bool drive_take_change(void);

/**
 * Takes the filesystem from the host. files_read() works until drive_close()
 * hands it back. False when it could not be mounted, even after a format.
 */
bool drive_open(void);

void drive_close(void);
void drive_note(const char *status);

#ifdef __cplusplus
}
#endif
