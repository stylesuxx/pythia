#include "settings.h"

#include <Preferences.h>
#include <string.h>

#define SETTINGS_NAMESPACE "die_oracle"
#define KEY_DIE "die"
#define KEY_BOOT_ATTEMPTS "boots"

static Preferences storage;

// The die as stored, so settling on it again costs no write.
static char stored_die_name[DIE_NAME_CAPACITY] = "";

void settings_begin(settings_t *settings) {
    storage.begin(SETTINGS_NAMESPACE, false);
    storage.getString(KEY_DIE, settings->die_name, DIE_NAME_CAPACITY);
    memcpy(stored_die_name, settings->die_name, DIE_NAME_CAPACITY);
}

void settings_set_die_name(const char *name) {
    if (strncmp(name, stored_die_name, DIE_NAME_CAPACITY) == 0) {
        return;
    }

    strncpy(stored_die_name, name, DIE_NAME_CAPACITY - 1);
    stored_die_name[DIE_NAME_CAPACITY - 1] = '\0';
    storage.putString(KEY_DIE, stored_die_name);
}

uint8_t settings_note_boot_attempt(void) {
    const uint8_t attempts = (uint8_t)(storage.getUChar(KEY_BOOT_ATTEMPTS, 0) + 1);
    storage.putUChar(KEY_BOOT_ATTEMPTS, attempts);
    return attempts;
}

void settings_clear_boot_attempts(void) {
    if (storage.getUChar(KEY_BOOT_ATTEMPTS, 0) != 0) {
        storage.putUChar(KEY_BOOT_ATTEMPTS, 0);
    }
}
