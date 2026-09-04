#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The user's drive: the FAT data partition, shown to a computer over USB mass
 * storage beside the serial port. drive.cpp satisfies the files port with it:
 * the computer owns the blocks whenever the drive is attached, and the
 * firmware mounts the filesystem only between files_open() and files_close(),
 * during which the computer sees a drive with no medium in it.
 */

/**
 * Formats the partition on first use and registers the mass storage
 * interface. Call before USB.begin(). False when there is no usable
 * partition, in which case there is no drive and no user files.
 */
bool drive_begin(void);

#ifdef __cplusplus
}
#endif
