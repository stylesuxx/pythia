#include "safe_mode.h"

#include "ports/settings.h"

static uint32_t started_ms = 0;
static bool active = false;
static bool settled = false;

void safe_mode_begin(uint32_t now) {
    started_ms = now;
    settled = false;
    active = settings_note_boot_attempt() >= SAFE_MODE_AFTER_ATTEMPTS;
    if (active) {
        settings_clear_boot_attempts();
    }
}

bool safe_mode_is_active(void) {
    return active;
}

void safe_mode_step(uint32_t now) {
    if (settled || active || (now - started_ms) < SAFE_MODE_SETTLED_MS) {
        return;
    }

    settled = true;
    settings_clear_boot_attempts();
}
