#include "settings.h"

#include <Preferences.h>

#include "scenes/effects/effect.h"
#include "oracle.h"

#define SETTINGS_NAMESPACE "die_oracle"
#define KEY_ROTATED "rotated"
#define KEY_HAPTICS "haptics"
#define KEY_COIN "coin"
#define KEY_EFFECT "effect"
#define KEY_DIE "die"
#define KEY_BOOT_ATTEMPTS "boots"

#define DEFAULT_ROTATED true
#define DEFAULT_HAPTICS true
#define DEFAULT_COIN true
#define DEFAULT_EFFECT "tear"

static Preferences storage;
static bool display_rotated = DEFAULT_ROTATED;
static bool haptics_enabled = DEFAULT_HAPTICS;
static bool coin_enabled = DEFAULT_COIN;
static uint8_t effect_index = 0;
static uint8_t die_index = 0;

void settings_begin(void) {
    storage.begin(SETTINGS_NAMESPACE, false);
    display_rotated = storage.getBool(KEY_ROTATED, DEFAULT_ROTATED);
    haptics_enabled = storage.getBool(KEY_HAPTICS, DEFAULT_HAPTICS);
    coin_enabled = storage.getBool(KEY_COIN, DEFAULT_COIN);
    effect_index = storage.getUChar(KEY_EFFECT, effect_index_of(DEFAULT_EFFECT));
    die_index = storage.getUChar(KEY_DIE, die_index_of("ORACLE"));

    if (effect_index >= EFFECT_COUNT) {
        effect_index = effect_index_of(DEFAULT_EFFECT);
    }

    if (die_index >= DIE_COUNT) {
        die_index = die_index_of("ORACLE");
    }
}

bool settings_is_display_rotated(void) {
    return display_rotated;
}

void settings_set_display_rotated(bool rotated) {
    display_rotated = rotated;
    storage.putBool(KEY_ROTATED, rotated);
}

bool settings_is_haptics_enabled(void) {
    return haptics_enabled;
}

void settings_set_haptics_enabled(bool enabled) {
    haptics_enabled = enabled;
    storage.putBool(KEY_HAPTICS, enabled);
}

bool settings_is_coin_enabled(void) {
    return coin_enabled;
}

void settings_set_coin_enabled(bool enabled) {
    coin_enabled = enabled;
    storage.putBool(KEY_COIN, enabled);
}

uint8_t settings_effect_index(void) {
    return effect_index;
}

void settings_set_effect_index(uint8_t index) {
    if (index >= EFFECT_COUNT) {
        return;
    }

    effect_index = index;
    storage.putUChar(KEY_EFFECT, index);
}

uint8_t settings_die_index(void) {
    return die_index;
}

void settings_set_die_index(uint8_t index) {
    if (index >= DIE_COUNT || index == die_index) {
        return;
    }

    die_index = index;
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
