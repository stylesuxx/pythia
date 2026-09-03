#include "hardware/touch_cst816.h"

#include <Arduino.h>
#include <Wire.h>

#include "knob_pins.h"

#define REG_STATUS 0x00
#define STATUS_LENGTH 7

void touch_begin(void) {
    pinMode(PIN_TOUCH_RST, OUTPUT);
    digitalWrite(PIN_TOUCH_RST, LOW);
    delay(10);

    digitalWrite(PIN_TOUCH_RST, HIGH);
    delay(60);

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(REG_STATUS);
    Wire.write(0x00);
    if (Wire.endTransmission() != 0) {
        Serial.println("CST816 did not acknowledge on the I2C bus");
    }
}

touch_t touch_read(void) {
    const touch_t none = {false, 0, 0};

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(REG_STATUS);
    if (Wire.endTransmission(true) != 0) {
        return none;
    }

    if (Wire.requestFrom(TOUCH_I2C_ADDR, STATUS_LENGTH) != STATUS_LENGTH) {
        return none;
    }

    uint8_t status[STATUS_LENGTH];
    for (uint8_t index = 0; index < STATUS_LENGTH; index++) {
        status[index] = Wire.read();
    }

    if (status[2] == 0) {
        return none;
    }

    const touch_t touch = {
        true,
        (uint16_t)(((uint16_t)(status[3] & 0x0F) << 8) | status[4]),
        (uint16_t)(((uint16_t)(status[5] & 0x0F) << 8) | status[6]),
    };

    return touch;
}
