#include "theme_file.h"

/*
 * data/theme.json, embedded by board_build.embed_txtfiles in platformio.ini,
 * which appends the NUL.
 */
extern "C" const char embedded_theme_json[] asm("_binary_data_theme_json_start");

const char *theme_builtin_text(void) {
    return embedded_theme_json;
}
