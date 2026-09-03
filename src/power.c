#include "power.h"

#include <math.h>

/**
 * Slow enough out that a hand reaching for the device can catch it, brisk
 * enough back that waking feels immediate.
 */
#define SLEEP_FADE_MS 700
#define WAKE_FADE_MS 140

typedef enum {
    POWER_AWAKE,
    POWER_DIMMING,  // fading out, still catchable
    POWER_ASLEEP,   // dark, resting until input
    POWER_LIGHTING, // fading back in
} power_state_t;

static power_state_t state = POWER_AWAKE;
static uint32_t idle_timeout_ms = 0;
static uint32_t last_input_ms = 0;

/**
 * The running ramp: from the level it started at towards its end, linearly
 * over the state's duration. Restarting it from the current level is what
 * lets a caught dim brighten from where it was.
 */
static uint32_t ramp_started_ms = 0;
static float ramp_from = 255.0f;
static uint8_t level = 255;

static uint8_t ramp(uint32_t now, uint32_t duration, float to) {
    const uint32_t elapsed = now - ramp_started_ms;
    if (elapsed >= duration) {
        return (uint8_t)lroundf(to);
    }

    const float progress = (float)elapsed / (float)duration;
    return (uint8_t)lroundf(ramp_from + (to - ramp_from) * progress);
}

static void settle(uint32_t now) {
    if (state == POWER_DIMMING && (now - ramp_started_ms) >= SLEEP_FADE_MS) {
        state = POWER_ASLEEP;
    } else if (state == POWER_LIGHTING && (now - ramp_started_ms) >= WAKE_FADE_MS) {
        state = POWER_AWAKE;
    }
}

static void output(uint32_t now) {
    switch (state) {
        case POWER_AWAKE: {
            level = 255;
        } break;

        case POWER_ASLEEP: {
            level = 0;
        } break;

        case POWER_DIMMING: {
            level = ramp(now, SLEEP_FADE_MS, 0.0f);
        } break;

        case POWER_LIGHTING: {
            level = ramp(now, WAKE_FADE_MS, 255.0f);
        } break;

        default: {
            state = POWER_AWAKE;
            level = 255;
        } break;
    }
}

void power_begin(uint32_t now, uint32_t idle_ms) {
    state = POWER_AWAKE;
    idle_timeout_ms = idle_ms;
    last_input_ms = now;
    ramp_started_ms = now;
    ramp_from = 255.0f;
    level = 255;
}

void power_notice_input(uint32_t now) {
    settle(now);
    last_input_ms = now;

    if (state != POWER_AWAKE) {
        output(now);
        ramp_from = (float)level;
        ramp_started_ms = now;
        state = POWER_LIGHTING;
    }

    output(now);
}

uint8_t power_step(uint32_t now, bool may_sleep) {
    settle(now);

    if (state == POWER_AWAKE && may_sleep && (now - last_input_ms) > idle_timeout_ms) {
        state = POWER_DIMMING;
        ramp_started_ms = now;
        ramp_from = 255.0f;
    }

    output(now);
    return level;
}

uint8_t power_get_level(void) {
    return level;
}

bool power_is_awake(void) {
    return state == POWER_AWAKE;
}

bool power_is_asleep(void) {
    return state == POWER_ASLEEP;
}
