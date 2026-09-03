#include "hardware/drive.h"

#include <Arduino.h>
#include <USB.h>
#include <USBMSC.h>
#include <esp_partition.h>
#include <esp_vfs_fat.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wear_levelling.h>

#include "files.h"

#define PARTITION_LABEL "ffat"
#define MOUNT_POINT "/drive"
#define README_PATH MOUNT_POINT "/README.txt"
#define STATUS_PATH MOUNT_POINT "/STATUS.txt"
#define MAX_OPEN_FILES 4

// The largest file worth reading whole; a typeface is a few hundred KB.
#define MAX_FILE_BYTES (2u * 1024u * 1024u)

// Rewritten at every boot, so a firmware update never leaves a stale copy.
static const char README_TEXT[] =
    "PYTHIA// user files\r\n"
    "\r\n"
    "theme.json on this drive is the look in use. Edit it, then eject the drive:\r\n"
    "the terminal reads it at once and switches over. A file it cannot accept is\r\n"
    "refused and the previous look stays; STATUS.txt says what was applied and\r\n"
    "why a file was refused. Delete theme.json and eject to go back to the\r\n"
    "built-in look; it is written again from that.\r\n"
    "\r\n"
    "Every colour takes \"#RRGGBB\" and every key is optional. \"colors\" holds the\r\n"
    "general roles; each screen has a section of its own (boot, list, caption,\r\n"
    "numbers, oracle, coin) whose keys win over the roles they follow. A key left\r\n"
    "out follows its role; a role left out keeps the built-in value.\r\n";

static USBMSC msc;
static const esp_partition_t *partition = NULL;
static SemaphoreHandle_t lock = NULL;

// The wear-levelling handle in whichever role is current: the host's raw
// block device, or the one under the mounted filesystem.
static wl_handle_t wear = WL_INVALID_HANDLE;
static size_t sector_size = 0;
static uint32_t sector_count = 0;
static bool host_owns = false;
static bool mounted = false;
static volatile bool changed = false;

/*
 * One flash sector under construction. The host's writes arrive in whole
 * sectors as this build is configured, and the cache also absorbs the case
 * where they arrive in pieces, so a sector is erased and written once
 * whichever way it comes.
 */
static uint8_t *cache = NULL;
static uint32_t cache_sector = UINT32_MAX;

static bool flush_cache(void) {
    if (cache_sector == UINT32_MAX) {
        return true;
    }

    const size_t address = (size_t)cache_sector * sector_size;
    cache_sector = UINT32_MAX;
    return wl_erase_range(wear, address, sector_size) == ESP_OK &&
           wl_write(wear, address, cache, sector_size) == ESP_OK;
}

static int32_t on_read(uint32_t sector, uint32_t offset, void *buffer, uint32_t size) {
    xSemaphoreTake(lock, portMAX_DELAY);
    int32_t result = -1;
    if (host_owns && (sector != cache_sector || flush_cache())) {
        if (wl_read(wear, (size_t)sector * sector_size + offset, buffer, size) == ESP_OK) {
            result = (int32_t)size;
        }
    }

    xSemaphoreGive(lock);
    return result;
}

static int32_t on_write(uint32_t sector, uint32_t offset, uint8_t *buffer, uint32_t size) {
    xSemaphoreTake(lock, portMAX_DELAY);
    int32_t result = -1;
    if (host_owns && offset + size <= sector_size) {
        bool ready = true;
        if (sector != cache_sector) {
            ready = flush_cache();
            cache_sector = sector;
            if (ready && (offset != 0 || size != sector_size)) {
                // A partial write keeps the rest of the sector as it was.
                ready = wl_read(wear, (size_t)sector * sector_size, cache, sector_size) == ESP_OK;
            }
        }

        if (ready) {
            memcpy(cache + offset, buffer, size);
            if (offset + size == sector_size) {
                ready = flush_cache();
            }
        }

        if (ready) {
            result = (int32_t)size;
        }
    }

    xSemaphoreGive(lock);
    return result;
}

// An eject from the host is the signal that its files are complete.
static bool on_start_stop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    if (load_eject && !start) {
        changed = true;
    }

    return true;
}

// A pulled cable is the other way a host's turn ends.
static void on_usb_event(void *argument, esp_event_base_t base, int32_t id, void *data) {
    (void)argument;
    (void)base;
    (void)data;
    if (id == ARDUINO_USB_STOPPED_EVENT) {
        changed = true;
    }
}

static bool mount_filesystem(bool format_if_needed) {
    esp_vfs_fat_mount_config_t config;
    memset(&config, 0, sizeof(config));
    config.format_if_mount_failed = format_if_needed;
    config.max_files = MAX_OPEN_FILES;
    config.allocation_unit_size = 0;

    const esp_err_t result =
        esp_vfs_fat_spiflash_mount_rw_wl(MOUNT_POINT, PARTITION_LABEL, &config, &wear);
    if (result != ESP_OK) {
        Serial.printf("drive: mount failed: %s\n", esp_err_to_name(result));
        wear = WL_INVALID_HANDLE;
        return false;
    }

    mounted = true;
    return true;
}

static void unmount_filesystem(void) {
    if (mounted) {
        esp_vfs_fat_spiflash_unmount_rw_wl(MOUNT_POINT, wear);
        mounted = false;
        wear = WL_INVALID_HANDLE;
    }
}

// Hands the blocks to the host: the raw device under the mass storage class.
static bool attach_to_host(void) {
    if (wl_mount(partition, &wear) != ESP_OK) {
        Serial.println("drive: wear levelling would not mount");
        wear = WL_INVALID_HANDLE;
        return false;
    }

    sector_size = wl_sector_size(wear);
    sector_count = (uint32_t)(wl_size(wear) / sector_size);
    host_owns = true;
    msc.mediaPresent(true);

    return true;
}

static void detach_from_host(void) {
    host_owns = false;
    msc.mediaPresent(false);
    flush_cache();
    if (wear != WL_INVALID_HANDLE) {
        wl_unmount(wear);
        wear = WL_INVALID_HANDLE;
    }
}

static void write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        Serial.printf("drive: could not write %s\n", path);
        return;
    }

    fputs(text, file);
    fclose(file);
}

bool drive_begin(void) {
    partition = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_FAT,
                                         PARTITION_LABEL);
    if (partition == NULL) {
        Serial.println("drive: no FAT partition in the table");
        return false;
    }

    lock = xSemaphoreCreateMutex();

    // Formatting on first use and writing the README happen with the
    // filesystem in the firmware's hands.
    if (!mount_filesystem(true)) {
        return false;
    }

    write_text(README_PATH, README_TEXT);
    unmount_filesystem();

    if (!attach_to_host()) {
        return false;
    }

    cache = (uint8_t *)malloc(sector_size);
    if (cache == NULL) {
        Serial.println("drive: no memory for the sector cache");
        return false;
    }

    msc.vendorID("DELPHI");
    msc.productID("PYTHIA");
    msc.productRevision("1.0");
    msc.onStartStop(on_start_stop);
    msc.onRead(on_read);
    msc.onWrite(on_write);
    msc.isWritable(true);
    msc.begin(sector_count, (uint16_t)sector_size);
    USB.onEvent(on_usb_event);

    return true;
}

bool drive_take_change(void) {
    const bool was_changed = changed;
    changed = false;

    return was_changed;
}

bool drive_open(void) {
    if (partition == NULL) {
        return false;
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    detach_from_host();
    const bool ok = mount_filesystem(true);
    xSemaphoreGive(lock);

    return ok;
}

void drive_close(void) {
    if (partition == NULL) {
        return;
    }

    xSemaphoreTake(lock, portMAX_DELAY);
    unmount_filesystem();
    attach_to_host();
    xSemaphoreGive(lock);
}

void drive_note(const char *status) {
    if (!mounted) {
        return;
    }

    FILE *file = fopen(STATUS_PATH, "w");
    if (file == NULL) {
        Serial.println("drive: could not write STATUS.txt");
        return;
    }

    fputs(status, file);
    fputs("\r\n", file);
    fclose(file);
}

bool files_read(const char *name, char **text, size_t *length) {
    if (!mounted || strchr(name, '/') != NULL) {
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return false;
    }

    fseek(file, 0, SEEK_END);
    const long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (size < 0 || (unsigned long)size > MAX_FILE_BYTES) {
        Serial.printf("drive: %s is too large to read\n", name);
        fclose(file);
        return false;
    }

    char *buffer = (char *)malloc((size_t)size + 1);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }

    const size_t read = fread(buffer, 1, (size_t)size, file);
    fclose(file);
    buffer[read] = '\0';
    *text = buffer;
    *length = read;

    return true;
}

bool files_write(const char *name, const char *text) {
    if (!mounted || strchr(name, '/') != NULL) {
        return false;
    }

    char path[64];
    snprintf(path, sizeof(path), MOUNT_POINT "/%s", name);
    FILE *file = fopen(path, "w");
    if (file == NULL) {
        Serial.printf("drive: could not write %s\n", name);
        return false;
    }

    const bool written = fputs(text, file) >= 0;
    fclose(file);

    return written;
}
