#include "config.h"

#include <stdio.h>
#include <string.h>

#include "render/canvas.h"
#include "scenes/effects/effect.h"

#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"

/*
 * theme.json in full is under a hundred tokens and layout.json at its widest
 * under two hundred; a file needing more is refused. Static, since parsing
 * is single-threaded and the arrays are too large for the loop task's stack.
 */
#define TOKEN_CAPACITY 256
static jsmntok_t tokens[TOKEN_CAPACITY];
#define KEY_PATH_CAPACITY 64

#define CONFIG_SPEC_ENTRY(section, stem, key, fallback) \
    [CONFIG_##stem] = {#section, #key, CONFIG_##fallback},
#define CONFIG_SPEC_SECTION(section, LIST) LIST(CONFIG_SPEC_ENTRY)

const config_color_spec_t CONFIG_COLORS[CONFIG_COLOR_COUNT] = {
    CONFIG_SECTIONS(CONFIG_SPEC_SECTION)
};

typedef struct {
    const char *text;
    const jsmntok_t *tokens;
    char *error;
    size_t error_capacity;
} parser_t;

static bool refuse(const parser_t *parser, const char *path, const char *reason) {
    snprintf(parser->error, parser->error_capacity, "%s: %s", path, reason);
    return false;
}

static size_t token_length(const jsmntok_t *token) {
    return (size_t)(token->end - token->start);
}

// Characters of a key worth quoting in a message; a longer key is cut.
#define KEY_QUOTE_LIMIT 24

static int quoted_length(const jsmntok_t *token) {
    const size_t length = token_length(token);
    return (int)(length < KEY_QUOTE_LIMIT ? length : KEY_QUOTE_LIMIT);
}

static bool token_is(const parser_t *parser, const jsmntok_t *token, const char *text) {
    const size_t length = token_length(token);
    return token->type == JSMN_STRING && strlen(text) == length &&
           memcmp(parser->text + token->start, text, length) == 0;
}

// The index just past the token at index and everything nested inside it.
static int skip(const parser_t *parser, int index) {
    int remaining = 1;
    while (remaining > 0) {
        remaining += parser->tokens[index].size - 1;
        index++;
    }

    return index;
}

static bool hex_nibble(char character, uint8_t *value) {
    if (character >= '0' && character <= '9') {
        *value = (uint8_t)(character - '0');
    } else if (character >= 'a' && character <= 'f') {
        *value = (uint8_t)(character - 'a' + 10);
    } else if (character >= 'A' && character <= 'F') {
        *value = (uint8_t)(character - 'A' + 10);
    } else {
        return false;
    }

    return true;
}

// "#RRGGBB" to RGB565, the same quantisation the compiled palette takes.
static bool parse_color(const parser_t *parser, const jsmntok_t *token, uint16_t *color) {
    if (token->type != JSMN_STRING || token_length(token) != 7 ||
        parser->text[token->start] != '#') {
        return false;
    }

    uint8_t channels[3];
    for (int channel = 0; channel < 3; channel++) {
        uint8_t high;
        uint8_t low;
        if (!hex_nibble(parser->text[token->start + 1 + channel * 2], &high) ||
            !hex_nibble(parser->text[token->start + 2 + channel * 2], &low)) {
            return false;
        }

        channels[channel] = (uint8_t)(high << 4 | low);
    }

    *color = CANVAS_RGB(channels[0], channels[1], channels[2]);
    return true;
}

static bool is_section(const char *name) {
    for (int which = 0; which < CONFIG_COLOR_COUNT; which++) {
        if (strcmp(CONFIG_COLORS[which].section, name) == 0) {
            return true;
        }
    }

    return false;
}

// One section object: every key must be a colour the table lists under it.
static bool parse_section(const parser_t *parser, const char *section, int index,
                          config_theme_t *theme) {
    const jsmntok_t *object = &parser->tokens[index];
    if (object->type != JSMN_OBJECT) {
        return refuse(parser, section, "expected an object");
    }

    index++;
    for (int entry = 0; entry < object->size; entry++) {
        const jsmntok_t *key = &parser->tokens[index];
        const jsmntok_t *value = &parser->tokens[index + 1];
        char path[KEY_PATH_CAPACITY];
        snprintf(path, sizeof(path), "%.*s.%.*s", KEY_QUOTE_LIMIT, section, quoted_length(key),
                 parser->text + key->start);

        int which = 0;
        while (which < CONFIG_COLOR_COUNT &&
               !(strcmp(CONFIG_COLORS[which].section, section) == 0 &&
                 token_is(parser, key, CONFIG_COLORS[which].key))) {
            which++;
        }

        if (which == CONFIG_COLOR_COUNT) {
            return refuse(parser, path, "unknown key");
        }

        if (!parse_color(parser, value, &theme->color[which])) {
            return refuse(parser, path, "expected \"#RRGGBB\"");
        }

        theme->has_color[which] = true;
        index = skip(parser, index + 1);
    }

    return true;
}

bool config_parse_theme(const char *text, size_t length, config_theme_t *theme, char *error,
                        size_t error_capacity) {
    const parser_t parser = {text, tokens, error, error_capacity};
    memset(theme, 0, sizeof(*theme));

    jsmn_parser state;
    jsmn_init(&state);
    const int count = jsmn_parse(&state, text, length, tokens, TOKEN_CAPACITY);
    if (count == JSMN_ERROR_NOMEM) {
        return refuse(&parser, "theme", "more entries than a theme can hold");
    }

    if (count < 0) {
        return refuse(&parser, "theme", "not valid JSON");
    }

    if (count == 0 || tokens[0].type != JSMN_OBJECT) {
        return refuse(&parser, "theme", "expected an object at the top level");
    }

    int index = 1;
    for (int entry = 0; entry < tokens[0].size; entry++) {
        const jsmntok_t *key = &tokens[index];
        char path[KEY_PATH_CAPACITY];
        snprintf(path, sizeof(path), "%.*s", quoted_length(key), text + key->start);

        if (key->type == JSMN_STRING && is_section(path)) {
            if (!parse_section(&parser, path, index + 1, theme)) {
                return false;
            }
        } else {
            return refuse(&parser, path, "unknown key");
        }

        index = skip(&parser, index + 1);
    }

    return true;
}

// The names EFFECTS holds, for a refusal to list.
static void list_effects(char *text, size_t capacity) {
    size_t used = 0;
    text[0] = '\0';
    for (uint8_t index = 0; index < EFFECT_COUNT && used < capacity; index++) {
        used += (size_t)snprintf(text + used, capacity - used, "%s%s", index > 0 ? ", " : "",
                                 EFFECTS[index]->name);
    }
}

static bool parse_effect(const parser_t *parser, const char *path, const jsmntok_t *value,
                         uint8_t *effect) {
    if (value->type == JSMN_STRING) {
        for (uint8_t index = 0; index < EFFECT_COUNT; index++) {
            if (token_is(parser, value, EFFECTS[index]->name)) {
                *effect = index;
                return true;
            }
        }
    }

    char names[64];
    list_effects(names, sizeof(names));
    char reason[96];
    snprintf(reason, sizeof(reason), "no effect named %.*s; the table holds %s",
             value->type == JSMN_STRING ? quoted_length(value) : 0,
             value->type == JSMN_STRING ? parser->text + value->start : "", names);

    return refuse(parser, path, reason);
}

static bool parse_kind(const parser_t *parser, const jsmntok_t *value, die_kind_t *kind) {
    if (token_is(parser, value, "numeric")) {
        *kind = DIE_NUMERIC;
    } else if (token_is(parser, value, "coin")) {
        *kind = DIE_COIN;
    } else if (token_is(parser, value, "d66")) {
        *kind = DIE_D66;
    } else if (token_is(parser, value, "oracle")) {
        *kind = DIE_ORACLE;
    } else {
        return false;
    }

    return true;
}

/*
 * A whole number between low and high inclusive. Digits only, so a sign or a
 * fraction is refused; a run that has already passed high stops early rather
 * than overflowing.
 */
static bool parse_integer(const parser_t *parser, const jsmntok_t *value, unsigned long low,
                          unsigned long high, unsigned long *number) {
    if (value->type != JSMN_PRIMITIVE) {
        return false;
    }

    *number = 0;
    for (int at = value->start; at < value->end; at++) {
        const char digit = parser->text[at];
        if (digit < '0' || digit > '9' || *number > high) {
            return false;
        }

        *number = *number * 10 + (unsigned long)(digit - '0');
    }

    return value->end > value->start && *number >= low && *number <= high;
}

static bool parse_sides(const parser_t *parser, const jsmntok_t *value, uint16_t *sides) {
    unsigned long number;
    if (!parse_integer(parser, value, 2, 100, &number)) {
        return false;
    }

    *sides = (uint16_t)number;
    return true;
}

// One entry of the dice array, at index in the tokens.
static bool parse_die(const parser_t *parser, uint8_t position, int index, uint8_t default_effect,
                      config_die_t *die) {
    char path[KEY_PATH_CAPACITY];
    snprintf(path, sizeof(path), "dice[%u]", (unsigned)position);

    const jsmntok_t *object = &parser->tokens[index];
    if (object->type != JSMN_OBJECT) {
        return refuse(parser, path, "expected an object");
    }

    memset(die, 0, sizeof(*die));
    die->effect = default_effect;
    bool has_name = false;
    bool has_kind = false;
    bool has_sides = false;
    bool has_effect = false;

    index++;
    for (int entry = 0; entry < object->size; entry++) {
        const jsmntok_t *key = &parser->tokens[index];
        const jsmntok_t *value = &parser->tokens[index + 1];
        snprintf(path, sizeof(path), "dice[%u].%.*s", (unsigned)position, quoted_length(key),
                 parser->text + key->start);

        if (token_is(parser, key, "name")) {
            if (value->type != JSMN_STRING) {
                return refuse(parser, path, "expected a string");
            }

            if (token_length(value) < 1 || token_length(value) >= DIE_NAME_CAPACITY) {
                return refuse(parser, path, "1 to 11 characters");
            }

            memcpy(die->name, parser->text + value->start, token_length(value));
            die->name[token_length(value)] = '\0';
            has_name = true;
        } else if (token_is(parser, key, "kind")) {
            if (!parse_kind(parser, value, &die->kind)) {
                return refuse(parser, path, "numeric, coin, d66 or oracle");
            }

            has_kind = true;
        } else if (token_is(parser, key, "sides")) {
            if (!parse_sides(parser, value, &die->sides)) {
                return refuse(parser, path, "2 to 100");
            }

            has_sides = true;
        } else if (token_is(parser, key, "effect")) {
            if (!parse_effect(parser, path, value, &die->effect)) {
                return false;
            }

            has_effect = true;
        } else {
            return refuse(parser, path, "unknown key");
        }

        index = skip(parser, index + 1);
    }

    snprintf(path, sizeof(path), "dice[%u]", (unsigned)position);
    if (!has_name) {
        return refuse(parser, path, "needs a name");
    }

    if (!has_kind) {
        return refuse(parser, path, "needs a kind");
    }

    snprintf(path, sizeof(path), "dice[%u].sides", (unsigned)position);
    if (die->kind == DIE_NUMERIC && !has_sides) {
        return refuse(parser, path, "a numeric die needs sides");
    }

    if (die->kind != DIE_NUMERIC && has_sides) {
        return refuse(parser, path, "only a numeric die has sides");
    }

    if (die->kind == DIE_ORACLE && has_effect) {
        snprintf(path, sizeof(path), "dice[%u].effect", (unsigned)position);
        return refuse(parser, path, "the oracle has no effect");
    }

    if (die->kind == DIE_COIN) {
        die->sides = 2;
    }

    return true;
}

bool config_parse_layout(const char *text, size_t length, config_layout_t *layout, char *error,
                         size_t error_capacity) {
    const parser_t parser = {text, tokens, error, error_capacity};
    memset(layout, 0, sizeof(*layout));

    jsmn_parser state;
    jsmn_init(&state);
    const int count = jsmn_parse(&state, text, length, tokens, TOKEN_CAPACITY);
    if (count == JSMN_ERROR_NOMEM) {
        return refuse(&parser, "layout", "more entries than a layout can hold");
    }

    if (count < 0) {
        return refuse(&parser, "layout", "not valid JSON");
    }

    if (count == 0 || tokens[0].type != JSMN_OBJECT) {
        return refuse(&parser, "layout", "expected an object at the top level");
    }

    /*
     * The default effect may follow the dice in the file, so the entries are
     * located on the first pass and read on the second.
     */
    uint8_t default_effect = 0;
    int dice_index = -1;
    int index = 1;
    for (int entry = 0; entry < tokens[0].size; entry++) {
        const jsmntok_t *key = &tokens[index];
        const jsmntok_t *value = &tokens[index + 1];
        char path[KEY_PATH_CAPACITY];
        snprintf(path, sizeof(path), "%.*s", quoted_length(key), text + key->start);

        if (token_is(&parser, key, "default_effect")) {
            if (!parse_effect(&parser, path, value, &default_effect)) {
                return false;
            }
        } else if (token_is(&parser, key, "dice")) {
            if (value->type != JSMN_ARRAY) {
                return refuse(&parser, path, "expected an array");
            }

            dice_index = index + 1;
        } else {
            return refuse(&parser, path, "unknown key");
        }

        index = skip(&parser, index + 1);
    }

    if (dice_index < 0) {
        return refuse(&parser, "dice", "missing");
    }

    const jsmntok_t *array = &tokens[dice_index];
    if (array->size < 1) {
        return refuse(&parser, "dice", "at least one die");
    }

    if (array->size > DICE_CAPACITY) {
        return refuse(&parser, "dice", "more than 16 dice");
    }

    index = dice_index + 1;
    for (int position = 0; position < array->size; position++) {
        if (!parse_die(&parser, (uint8_t)position, index, default_effect,
                       &layout->dice[position])) {
            return false;
        }

        index = skip(&parser, index);
    }

    layout->count = (uint8_t)array->size;
    return true;
}

#define NAME_RULE "lower-case letters, digits, - and _, 1 to 16 characters"

// A theme's or a layout's name, as CONFIG_FILE_NAME_CAPACITY describes it.
static bool parse_file_name(const parser_t *parser, const jsmntok_t *value, char *name) {
    if (value->type != JSMN_STRING) {
        return false;
    }

    const size_t length = token_length(value);
    if (length < 1 || length >= CONFIG_FILE_NAME_CAPACITY) {
        return false;
    }

    for (size_t at = 0; at < length; at++) {
        const char character = parser->text[value->start + at];
        const bool allowed = (
            (character >= 'a' && character <= 'z') ||
            (character >= '0' && character <= '9') ||
            character == '-' ||
            character == '_'
        );

        if (!allowed) {
            return false;
        }
    }

    memcpy(name, parser->text + value->start, length);
    name[length] = '\0';
    return true;
}

static bool parse_switch(const parser_t *parser, const jsmntok_t *value, bool *on) {
    if (value->type != JSMN_PRIMITIVE) {
        return false;
    }

    const size_t length = token_length(value);
    const char *text = parser->text + value->start;
    if (length == 4 && memcmp(text, "true", 4) == 0) {
        *on = true;
    } else if (length == 5 && memcmp(text, "false", 5) == 0) {
        *on = false;
    } else {
        return false;
    }

    return true;
}

void config_default_settings(config_settings_t *settings) {
    memset(settings, 0, sizeof(*settings));
    snprintf(settings->theme, sizeof(settings->theme), "%s", CONFIG_BUILTIN_THEME);
    snprintf(settings->layout, sizeof(settings->layout), "%s", CONFIG_BUILTIN_LAYOUT);
    settings->display_rotated = true;
    settings->haptics = true;
    settings->reverse_knob = false;
    settings->sleep_after = 120;
    settings->brightness = 100;
}

bool config_parse_settings(const char *text, size_t length, config_settings_t *settings,
                           char *error, size_t error_capacity) {
    const parser_t parser = {text, tokens, error, error_capacity};
    config_default_settings(settings);

    jsmn_parser state;
    jsmn_init(&state);
    const int count = jsmn_parse(&state, text, length, tokens, TOKEN_CAPACITY);
    if (count == JSMN_ERROR_NOMEM) {
        return refuse(&parser, "settings", "more entries than the settings can hold");
    }

    if (count < 0) {
        return refuse(&parser, "settings", "not valid JSON");
    }

    if (count == 0 || tokens[0].type != JSMN_OBJECT) {
        return refuse(&parser, "settings", "expected an object at the top level");
    }

    int index = 1;
    for (int entry = 0; entry < tokens[0].size; entry++) {
        const jsmntok_t *key = &tokens[index];
        const jsmntok_t *value = &tokens[index + 1];
        char path[KEY_PATH_CAPACITY];
        snprintf(path, sizeof(path), "%.*s", quoted_length(key), text + key->start);

        unsigned long number;
        if (token_is(&parser, key, "theme")) {
            if (!parse_file_name(&parser, value, settings->theme)) {
                return refuse(&parser, path, NAME_RULE);
            }
        } else if (token_is(&parser, key, "layout")) {
            if (!parse_file_name(&parser, value, settings->layout)) {
                return refuse(&parser, path, NAME_RULE);
            }
        } else if (token_is(&parser, key, "display_rotated")) {
            if (!parse_switch(&parser, value, &settings->display_rotated)) {
                return refuse(&parser, path, "expected true or false");
            }
        } else if (token_is(&parser, key, "haptics")) {
            if (!parse_switch(&parser, value, &settings->haptics)) {
                return refuse(&parser, path, "expected true or false");
            }
        } else if (token_is(&parser, key, "reverse_knob")) {
            if (!parse_switch(&parser, value, &settings->reverse_knob)) {
                return refuse(&parser, path, "expected true or false");
            }
        } else if (token_is(&parser, key, "sleep_after")) {
            if (!parse_integer(&parser, value, 0, CONFIG_SLEEP_LIMIT_SECONDS, &number)) {
                return refuse(&parser, path, "0 to 86400");
            }

            settings->sleep_after = (uint32_t)number;
        } else if (token_is(&parser, key, "brightness")) {
            if (!parse_integer(&parser, value, 1, 100, &number)) {
                return refuse(&parser, path, "1 to 100");
            }

            settings->brightness = (uint8_t)number;
        } else {
            return refuse(&parser, path, "unknown key");
        }

        index = skip(&parser, index + 1);
    }

    return true;
}
