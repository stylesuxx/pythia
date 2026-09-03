#include "settings.h"

#include <Preferences.h>

#define SETTINGS_NAMESPACE "die_oracle"
#define KEY_ROTATED "rotated"
#define KEY_HAPTICS "haptics"
#define KEY_COIN "coin"
#define KEY_EFFECT "effect"
#define KEY_DIE "die"
#define KEY_BOOT_ATTEMPTS "boots"

static Preferences storage;

// The die as stored, so settling on it again costs no write.
static uint8_t stored_die_index = 0;

void settings_begin(settings_t *settings) {
    storage.begin(SETTINGS_NAMESPACE, false);
    settings->display_rotated = storage.getBool(KEY_ROTATED, settings->display_rotated);
    settings->haptics_enabled = storage.getBool(KEY_HAPTICS, settings->haptics_enabled);
    settings->coin_enabled = storage.getBool(KEY_COIN, settings->coin_enabled);
    settings->effect_index = storage.getUChar(KEY_EFFECT, settings->effect_index);
    settings->die_index = storage.getUChar(KEY_DIE, settings->die_index);
    stored_die_index = settings->die_index;
}

void settings_set_die_index(uint8_t index) {
    if (index == stored_die_index) {
        return;
    }

    stored_die_index = index;
    storage.putUChar(KEY_DIE, index);
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
