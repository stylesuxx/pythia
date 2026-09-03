#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Screen power: the light level over time.
 *
 * After the idle timeout the light fades out and the screen is asleep. Any
 * input brings it back with a ramp that continues from whatever level the
 * fade had reached. The machine is free of hardware: the caller applies
 * power_get_level() to the panel.
 */

void power_begin(uint32_t now, uint32_t idle_ms);
uint8_t power_get_level(void);
bool power_is_awake(void);
bool power_is_asleep(void);

/**
 * Records input at now: resets the idle timer and, if the screen is not
 * awake, starts the ramp back up. Callers that want to spend a waking input
 * rather than act on it check power_is_awake() first.
 */
void power_notice_input(uint32_t now);

/**
 * Advances to now and returns the light level, 0 to 255. Sleep begins only
 * while may_sleep holds; call every step.
 */
uint8_t power_step(uint32_t now, bool may_sleep);

#ifdef __cplusplus
}
#endif
