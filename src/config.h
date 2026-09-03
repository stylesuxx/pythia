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

typedef enum {
    CONFIG_COLOR_BACKGROUND,
    CONFIG_COLOR_ANSWER,
    CONFIG_COLOR_MODIFIER,
    CONFIG_COLOR_LABEL,
    CONFIG_COLOR_RING,
    CONFIG_COLOR_RING_ACTIVE,
    CONFIG_COLOR_COUNT,
} config_color_t;

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

bool config_parse_theme(const char *text, size_t length, config_theme_t *theme, char *error,
                        size_t error_capacity);


#ifdef __cplusplus
}
#endif
