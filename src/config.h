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
 * Every colour theme.json can name, one list per section of the file. A row
 * is the enum stem, the key, and the general role the key follows when a
 * file leaves it out; a general role is its own fallback. The key is also the
 * field's name in theme_t, so the enum, CONFIG_COLORS, the struct and where a
 * file's value lands all expand from these rows. Adding a colour is a row
 * here and a line in data/theme.json.
 */
#define CONFIG_COLORS_SECTION(X)                                                                   \
    X(colors, BACKGROUND, background, BACKGROUND)                                                  \
    X(colors, PRIMARY, primary, PRIMARY)                                                           \
    X(colors, SECONDARY, secondary, SECONDARY)                                                     \
    X(colors, MUTED, muted, MUTED)                                                                 \
    X(colors, RING, ring, RING)                                                                    \
    X(colors, RING_ACTIVE, ring_active, RING_ACTIVE)

#define CONFIG_BOOT_SECTION(X)                                                                     \
    /* PYTHIA//, and the scanline that drifts over it */                                           \
    X(boot, BOOT_WORDMARK, wordmark, PRIMARY)                                                      \
    /* the glyphs a position cycles through */                                                     \
    X(boot, BOOT_SCRAMBLE, scramble, MUTED)                                                        \
    /* DELPHI SYSTEMS */                                                                           \
    X(boot, BOOT_CAPTION, caption, MUTED)                                                          \
    X(boot, BOOT_RING, ring, RING)                                                                 \
    /* the comet, and the rule under the wordmark */                                               \
    X(boot, BOOT_RING_ACTIVE, ring_active, RING_ACTIVE)

#define CONFIG_LIST_SECTION(X)                                                                     \
    X(list, LIST_NAME, name, MUTED)                                                                \
    X(list, LIST_RING, ring, RING)                                                                 \
    X(list, LIST_RING_ACTIVE, ring_active, RING_ACTIVE)

#define CONFIG_CAPTION_SECTION(X)                                                                  \
    /* the die name along the rim */                                                               \
    X(caption, CAPTION_TEXT, text, MUTED)

#define CONFIG_NUMBERS_SECTION(X)                                                                  \
    /* every numeric result, whichever effect brings it */                                         \
    X(numbers, NUMBERS_TEXT, text, PRIMARY)

#define CONFIG_ORACLE_SECTION(X)                                                                   \
    X(oracle, ORACLE_ANSWER, answer, PRIMARY)                                                      \
    X(oracle, ORACLE_MODIFIER, modifier, SECONDARY)

#define CONFIG_COIN_SECTION(X)                                                                     \
    /* the rim, grooves and highlight are shades of it */                                          \
    X(coin, COIN_FACE, face, PRIMARY)

// The sections in file order, each with the list of its rows.
#define CONFIG_SECTIONS(S)                                                                        \
    S(colors, CONFIG_COLORS_SECTION)                                                              \
    S(boot, CONFIG_BOOT_SECTION)                                                                  \
    S(list, CONFIG_LIST_SECTION)                                                                  \
    S(caption, CONFIG_CAPTION_SECTION)                                                            \
    S(numbers, CONFIG_NUMBERS_SECTION)                                                            \
    S(oracle, CONFIG_ORACLE_SECTION)                                                              \
    S(coin, CONFIG_COIN_SECTION)

#define CONFIG_ENUM_ENTRY(section, stem, key, fallback) CONFIG_##stem,
#define CONFIG_ENUM_SECTION(section, LIST) LIST(CONFIG_ENUM_ENTRY)

typedef enum {
    CONFIG_SECTIONS(CONFIG_ENUM_SECTION)
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
