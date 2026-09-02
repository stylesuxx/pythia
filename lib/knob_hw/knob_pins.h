#pragma once

// GPIO assignments for the Waveshare ESP32-S3-Knob-Touch-LCD-1.8, transcribed
// from the vendor's demo code.
//
// Waveshare's demo code is authoritative for pin numbers and takes precedence
// over their published schematics, which have disagreed with it on the SD card
// lines. The demo code is what ships running on the hardware.

// LCD, QSPI ST77916
#define PIN_LCD_CS    14
#define PIN_LCD_CLK   13
#define PIN_LCD_D0    15
#define PIN_LCD_D1    16
#define PIN_LCD_D2    17
#define PIN_LCD_D3    18
#define PIN_LCD_RST   21
#define PIN_LCD_BL    47

#define LCD_H_RES     360
#define LCD_V_RES     360
#define LCD_BPP       16

// Rotary encoder
#define PIN_ENC_A     8
#define PIN_ENC_B     7

// I2C, shared by the CST816 touch controller and the DRV2605 haptic driver
#define PIN_I2C_SDA   11
#define PIN_I2C_SCL   12

// SD card, SDMMC 4-wire
#define SD_CMD_PIN    3
#define SD_CLK_PIN    4
#define SD_D0_PIN     5
#define SD_D1_PIN     6
#define SD_D2_PIN     42
#define SD_D3_PIN     2

// Touch, CST816 on the shared I2C bus
#define TOUCH_I2C_ADDR  0x15
#define PIN_TOUCH_INT   9
#define PIN_TOUCH_RST   10

// Haptics, DRV2605 on the shared I2C bus
#define HAPTIC_I2C_ADDR 0x5A
