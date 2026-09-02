#include "panel.h"

#include <Arduino.h>

#include "canvas.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_st77916.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "knob_lcd_init.h"
#include "knob_pins.h"
#include "settings.h"

// One band per DMA transfer. 360x40 keeps each staging buffer under 29 KB of
// internal RAM and divides the panel height exactly. Two buffers, so the next
// band is converted while the previous one is still on the wire.
#define PANEL_BAND_LINES 40
#define PANEL_BAND_COUNT 2

// The ST77916 datasheet caps the QSPI clock at 50 MHz and the ESP32-S3 offers
// 40 or 80, so 40 is the in-spec choice. The panel does accept 80, but with
// conversion overlapped the wire is no longer what bounds a present.
#define PANEL_QSPI_HZ (40 * 1000 * 1000)

static esp_lcd_panel_handle_t panel = NULL;
static uint16_t *bands[PANEL_BAND_COUNT] = {NULL, NULL};
static SemaphoreHandle_t band_sent = NULL;

static bool on_band_sent(esp_lcd_panel_io_handle_t io,
                         esp_lcd_panel_io_event_data_t *event_data,
                         void *user_context) {
    BaseType_t woke_higher_priority_task = pdFALSE;
    xSemaphoreGiveFromISR(band_sent, &woke_higher_priority_task);
    return woke_higher_priority_task == pdTRUE;
}

bool panel_begin(void) {
    band_sent = xSemaphoreCreateCounting(PANEL_BAND_COUNT, 0);
    for (int i = 0; i < PANEL_BAND_COUNT; i++) {
        bands[i] = (uint16_t *)heap_caps_malloc(
            (size_t)CANVAS_WIDTH * PANEL_BAND_LINES * sizeof(uint16_t), MALLOC_CAP_DMA);
        if (bands[i] == NULL) {
            Serial.println("panel: no DMA memory for the staging bands");
            return false;
        }
    }

    if (band_sent == NULL) {
        return false;
    }

    // ST77916_PANEL_BUS_QSPI_CONFIG orders .sclk_io_num before .data0_io_num,
    // but spi_bus_config_t declares data0 first and C++ requires designated
    // initialisers in declaration order, so this is filled out by hand.
    spi_bus_config_t bus_config = {};
    bus_config.data0_io_num = PIN_LCD_D0;
    bus_config.data1_io_num = PIN_LCD_D1;
    bus_config.sclk_io_num = PIN_LCD_CLK;
    bus_config.data2_io_num = PIN_LCD_D2;
    bus_config.data3_io_num = PIN_LCD_D3;
    bus_config.max_transfer_sz = CANVAS_WIDTH * PANEL_BAND_LINES * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus_config, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_spi_config_t io_config =
        ST77916_PANEL_IO_QSPI_CONFIG(PIN_LCD_CS, on_band_sent, NULL);
    io_config.pclk_hz = PANEL_QSPI_HZ;
    ESP_ERROR_CHECK(
        esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI2_HOST, &io_config, &io));
    st77916_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {.use_qspi_interface = 1},
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = PIN_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BPP,
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));

    ledcAttach(PIN_LCD_BL, 50000, 8);
    panel_set_backlight(0);

    return true;
}

void panel_present(const uint16_t *pixels) {
    panel_present_rows(pixels, 0, CANVAS_HEIGHT);
}

// Converts one band to panel byte order, two pixels per word. Rotated, the
// band is read backwards and every word's pixel pair swaps too, which is
// exactly a 32-bit byte reversal.
static void convert_band(uint16_t *destination, const uint16_t *source, size_t pixels,
                         bool rotated) {
    const uint32_t *from = (const uint32_t *)source;
    uint32_t *to = (uint32_t *)destination;
    const size_t words = pixels / 2;

    if (rotated) {
        for (size_t i = 0; i < words; i++) {
            to[i] = __builtin_bswap32(from[words - 1 - i]);
        }

        return;
    }

    for (size_t i = 0; i < words; i++) {
        const uint32_t word = from[i];
        to[i] = ((word & 0x00FF00FFu) << 8) | ((word >> 8) & 0x00FF00FFu);
    }
}

void panel_present_rows(const uint16_t *pixels, int top, int height) {
    int first = top & ~1;
    int last = (top + height + 1) & ~1;
    if (first < 0) {
        first = 0;
    }

    if (last > CANVAS_HEIGHT) {
        last = CANVAS_HEIGHT;
    }

    const bool rotated = settings_is_display_rotated();

    // A buffer is reused only once the transfer that last used it has
    // finished, which the driver reports through band_sent in order.
    int sent = 0;
    for (int row = first; row < last; row += PANEL_BAND_LINES) {
        const int lines = (row + PANEL_BAND_LINES <= last) ? PANEL_BAND_LINES : (last - row);
        const uint16_t *source = pixels + (size_t)row * CANVAS_WIDTH;
        const size_t count = (size_t)CANVAS_WIDTH * lines;
        uint16_t *band = bands[sent % PANEL_BAND_COUNT];

        if (sent >= PANEL_BAND_COUNT) {
            xSemaphoreTake(band_sent, portMAX_DELAY);
        }
        convert_band(band, source, count, rotated);

        const int top_row = rotated ? (CANVAS_HEIGHT - row - lines) : row;
        esp_lcd_panel_draw_bitmap(panel, 0, top_row, CANVAS_WIDTH, top_row + lines, band);

        sent++;
    }

    const int outstanding = sent < PANEL_BAND_COUNT ? sent : PANEL_BAND_COUNT;
    for (int index = 0; index < outstanding; index++) {
        xSemaphoreTake(band_sent, portMAX_DELAY);
    }
}

void panel_set_display_on(bool on) {
    esp_lcd_panel_disp_on_off(panel, on);
}

void panel_set_backlight(uint8_t level) {
    ledcWrite(PIN_LCD_BL, level);
}
