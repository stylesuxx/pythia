#include "ports/haptics.h"

#include <Adafruit_DRV2605.h>
#include <Arduino.h>


static Adafruit_DRV2605 driver;
static bool driver_ready = false;
static bool enabled = true;

void haptics_begin(void) {
    driver_ready = driver.begin();
    if (!driver_ready) {
        Serial.println("haptics: DRV2605 did not answer on the I2C bus");
        return;
    }

    driver.useLRA();
    driver.selectLibrary(6);
    driver.setMode(DRV2605_MODE_INTTRIG);
}

void haptics_set_enabled(bool on) {
    enabled = on;
}

void haptics_play(uint8_t effect) {
    if (!driver_ready || !enabled) {
        return;
    }

    driver.setWaveform(0, effect);
    driver.setWaveform(1, 0);
    driver.go();
}
