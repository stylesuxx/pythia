/*
 * Default host adapters for the hardware the shared sources talk to. Each is
 * weak, so a host program that needs to observe or script one, the way
 * tests/reveal.c records haptics and tests/oracle.c feeds the entropy draws,
 * defines its own and the linker takes that instead.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

#include "esp_random.h"
#include "ports/files.h"
#include "ports/haptics.h"
#include "ports/settings.h"

__attribute__((weak)) uint32_t esp_random(void) {
    return ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 6) ^ (uint32_t)rand();
}

__attribute__((weak)) void haptics_begin(void) {}

__attribute__((weak)) void haptics_set_enabled(bool enabled) {
    (void)enabled;
}

__attribute__((weak)) void haptics_play(uint8_t effect) {
    (void)effect;
}

__attribute__((weak)) void settings_set_die_name(const char *name) {
    (void)name;
}

// The host remembers no boots; a program that counts them defines its own.
__attribute__((weak)) uint8_t settings_note_boot_attempt(void) {
    return 1;
}

__attribute__((weak)) void settings_clear_boot_attempts(void) {}

/*
 * The host's drive can always be taken, is never ejected and carries nothing;
 * a program with files to serve defines its own.
 */
__attribute__((weak)) bool files_take_change(void) {
    return false;
}

__attribute__((weak)) bool files_open(void) {
    return true;
}

__attribute__((weak)) void files_close(void) {}

__attribute__((weak)) bool files_mkdir(const char *name) {
    (void)name;

    return true;
}

__attribute__((weak)) bool files_read(const char *name, char **text, size_t *length) {
    (void)name;
    (void)text;
    (void)length;

    return false;
}

__attribute__((weak)) bool files_write(const char *name, const char *text) {
    (void)name;
    (void)text;

    return false;
}
