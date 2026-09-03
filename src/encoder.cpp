#include "encoder.h"

#include <Arduino.h>

#include "esp_timer.h"
#include "knob_pins.h"

/**
 * The knob is a two-way switch rather than a quadrature encoder. Each click
 * pulls one line low for 6 to 100 ms, A for clockwise and B for
 * counter-clockwise, while the other line stays high. Waveshare's own driver
 * decodes it the same way, by polling.
 *
 * The lines are not clean. Contacts bounce at both ends of a pulse, and when
 * one line rises the other dips for a fraction of a millisecond. Sampling on a
 * timer sidesteps all of it: a line counts as low only after SETTLE_SAMPLES
 * consecutive low samples, and must be seen high for as many before it can
 * count again. An edge interrupt cannot do this, because the level it reads
 * after a bouncing edge lags the edge that fired.
 */
#define SAMPLE_INTERVAL_US 1000
#define SETTLE_SAMPLES 3

typedef struct {
    uint8_t pin;
    int32_t direction;
    uint8_t run;  // consecutive samples at the current level
    bool low;     // settled state of the line
} line_t;

static line_t lines[] = {
    {PIN_ENC_A, 1, 0, false},
    {PIN_ENC_B, -1, 0, false},
};

static portMUX_TYPE detent_lock = portMUX_INITIALIZER_UNLOCKED;
static int32_t pending_detents = 0;
static esp_timer_handle_t sampler = NULL;

static void sample_line(line_t *line) {
    const bool low_now = digitalRead(line->pin) == LOW;
    if (low_now == line->low) {
        line->run = 0;
        return;
    }

    if (line->run < SETTLE_SAMPLES) {
        line->run++;
        return;
    }

    line->run = 0;
    line->low = low_now;
    if (low_now) {
        portENTER_CRITICAL(&detent_lock);
        pending_detents += line->direction;
        portEXIT_CRITICAL(&detent_lock);
    }
}

static void on_sample(void *argument) {
    (void)argument;
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); index++) {
        sample_line(&lines[index]);
    }
}

void encoder_begin(void) {
    for (size_t index = 0; index < sizeof(lines) / sizeof(lines[0]); index++) {
        pinMode(lines[index].pin, INPUT_PULLUP);
    }

    const esp_timer_create_args_t sampler_args = {
        .callback = on_sample,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "encoder",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&sampler_args, &sampler));
    ESP_ERROR_CHECK(esp_timer_start_periodic(sampler, SAMPLE_INTERVAL_US));
}

int32_t encoder_take_detents(void) {
    portENTER_CRITICAL(&detent_lock);
    const int32_t detents = pending_detents;
    pending_detents = 0;
    portEXIT_CRITICAL(&detent_lock);

    return detents;
}
