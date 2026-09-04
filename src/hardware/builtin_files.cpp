#include "ports/builtin_files.h"

/*
 * The files under data/, embedded by board_build.embed_txtfiles in
 * platformio.ini, which appends the NUL to each.
 */
extern "C" const char embedded_settings_json[] asm("_binary_data_settings_json_start");
extern "C" const char embedded_theme_json[] asm("_binary_data_themes_neon_theme_json_start");
extern "C" const char embedded_layout_json[] asm("_binary_data_layouts_default_json_start");
extern "C" const char embedded_readme_txt[] asm("_binary_data_README_txt_start");

const char *settings_builtin_text(void) {
    return embedded_settings_json;
}

const char *theme_builtin_text(void) {
    return embedded_theme_json;
}

const char *layout_builtin_text(void) {
    return embedded_layout_json;
}

const char *readme_builtin_text(void) {
    return embedded_readme_txt;
}
