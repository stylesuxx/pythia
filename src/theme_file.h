#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Port: the theme file built into the firmware. data/theme.json is the one
 * place the built-in palette exists; the device embeds it verbatim at link
 * time and the host build generates a source from the same file, so the
 * theme parsed at startup and the file written to the drive are that file
 * byte for byte.
 */

// The text of data/theme.json, NUL-terminated.
const char *theme_builtin_text(void);

#ifdef __cplusplus
}
#endif
