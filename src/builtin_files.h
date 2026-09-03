#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Port: the files built into the firmware. data/theme.json and
 * data/layout.json are the one place the built-in palette and die table
 * exist, and data/README.txt is what the drive explains itself with; the
 * device embeds them verbatim at link time and the host build generates a
 * source from the same files, so what is parsed at startup and what is
 * written to the drive are those files byte for byte.
 */

const char *theme_builtin_text(void);
const char *layout_builtin_text(void);
const char *readme_builtin_text(void);

#ifdef __cplusplus
}
#endif
