#include "config.h"

#include <stdio.h>
#include <string.h>

#include "render/canvas.h"

#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"

// theme.json in full is under a hundred tokens; a file needing more is refused.
#define TOKEN_CAPACITY 128
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

static bool parse_name(const parser_t *parser, const jsmntok_t *value, config_theme_t *theme) {
    if (value->type != JSMN_STRING) {
        return refuse(parser, "name", "expected a string");
    }

    if (token_length(value) >= CONFIG_NAME_CAPACITY) {
        return refuse(parser, "name", "longer than 23 characters");
    }

    memcpy(theme->name, parser->text + value->start, token_length(value));
    theme->name[token_length(value)] = '\0';
    return true;
}

bool config_parse_theme(const char *text, size_t length, config_theme_t *theme, char *error,
                        size_t error_capacity) {
    jsmntok_t tokens[TOKEN_CAPACITY];
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
        const jsmntok_t *value = &tokens[index + 1];
        char path[KEY_PATH_CAPACITY];
        snprintf(path, sizeof(path), "%.*s", quoted_length(key), text + key->start);

        if (token_is(&parser, key, "name")) {
            if (!parse_name(&parser, value, theme)) {
                return false;
            }
        } else if (key->type == JSMN_STRING && is_section(path)) {
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
