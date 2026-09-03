#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The user's files, parsed and validated. A file is read into a struct in
 * full and applied only once every key has checked out; the first problem is
 * reported with its key path and the reason, so a typo is a message rather
 * than a silently kept default.
 */

/**
 * Every colour theme.json can name. The general roles come first; each
 * screen's section follows, and a section key left out takes the general
 * role CONFIG_COLORS names as its fallback.
 */
typedef enum {
    CONFIG_BACKGROUND,
    CONFIG_PRIMARY,
    CONFIG_SECONDARY,
    CONFIG_MUTED,
    CONFIG_RING,
    CONFIG_RING_ACTIVE,
    CONFIG_BOOT_WORDMARK,
    CONFIG_BOOT_SCRAMBLE,
    CONFIG_BOOT_CAPTION,
    CONFIG_BOOT_RING,
    CONFIG_BOOT_RING_ACTIVE,
    CONFIG_LIST_NAME,
    CONFIG_LIST_RING,
    CONFIG_LIST_RING_ACTIVE,
    CONFIG_CAPTION_TEXT,
    CONFIG_NUMBERS_TEXT,
    CONFIG_ORACLE_ANSWER,
    CONFIG_ORACLE_MODIFIER,
    CONFIG_COIN_FACE,
    CONFIG_COLOR_COUNT,
} config_color_t;

typedef struct {
    const char *section; // the object the key sits in
    const char *key;
    config_color_t fallback; // a general role is its own fallback
} config_color_spec_t;

extern const config_color_spec_t CONFIG_COLORS[CONFIG_COLOR_COUNT];

#define CONFIG_NAME_CAPACITY 24
#define CONFIG_ERROR_CAPACITY 96

/**
 * What theme.json set. Every colour is optional; the ones the file names are
 * flagged and carried as RGB565.
 */
typedef struct {
    char name[CONFIG_NAME_CAPACITY];
    bool has_color[CONFIG_COLOR_COUNT];
    uint16_t color[CONFIG_COLOR_COUNT];
} config_theme_t;

/**
 * Parses theme.json. text need not be NUL-terminated. On refusal returns
 * false with error filled, as "key.path: reason".
 */
bool config_parse_theme(const char *text, size_t length, config_theme_t *theme, char *error,
                        size_t error_capacity);

#ifdef __cplusplus
}
#endif
