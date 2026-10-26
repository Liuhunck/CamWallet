/*
 * LCD initialization adapted from Waveshare's RP2350-Touch-LCD-2
 * C/01-LCD example (LCD_2in.c and DEV_Config.h, 2025-03-13).
 * Original author: Waveshare team.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "lcd.h"

#include <stddef.h>

#include "hardware/spi.h"
#include "pico/stdlib.h"

#include "board_pins.h"
#include "fonts.h"

static void output_pin_init(uint pin, bool high)
{
    gpio_init(pin);
    gpio_put(pin, high);
    gpio_set_dir(pin, GPIO_OUT);
}

static void write_command(uint8_t command)
{
    gpio_put(CAMWALLET_LCD_DC_PIN, 0);
    gpio_put(CAMWALLET_LCD_CS_PIN, 0);
    spi_write_blocking(CAMWALLET_LCD_SPI, &command, 1);
    gpio_put(CAMWALLET_LCD_CS_PIN, 1);
}

static void write_data(const uint8_t *data, size_t size)
{
    gpio_put(CAMWALLET_LCD_DC_PIN, 1);
    gpio_put(CAMWALLET_LCD_CS_PIN, 0);
    spi_write_blocking(CAMWALLET_LCD_SPI, data, size);
    gpio_put(CAMWALLET_LCD_CS_PIN, 1);
}

/* End coordinates are exclusive, matching rectangle widths and heights. */
static void set_window(uint16_t x, uint16_t y, uint16_t width, uint16_t height)
{
    const uint16_t right = x + width - 1u;
    const uint16_t bottom = y + height - 1u;
    const uint8_t columns[] = {x >> 8, x & 0xff, right >> 8, right & 0xff};
    const uint8_t rows[] = {y >> 8, y & 0xff, bottom >> 8, bottom & 0xff};

    write_command(0x2a);
    write_data(columns, sizeof(columns));
    write_command(0x2b);
    write_data(rows, sizeof(rows));
    write_command(0x2c);
}

void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color)
{
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || width == 0 || height == 0) {
        return;
    }
    if (width > LCD_WIDTH - x) {
        width = LCD_WIDTH - x;
    }
    if (height > LCD_HEIGHT - y) {
        height = LCD_HEIGHT - y;
    }

    /* One scanline, not a full framebuffer, keeps RAM available for the camera. */
    uint8_t row[LCD_WIDTH * 2u];
    for (uint16_t column = 0; column < width; ++column) {
        row[column * 2u] = color >> 8;
        row[column * 2u + 1u] = color & 0xff;
    }

    set_window(x, y, width, height);
    gpio_put(CAMWALLET_LCD_DC_PIN, 1);
    gpio_put(CAMWALLET_LCD_CS_PIN, 0);
    for (uint16_t line = 0; line < height; ++line) {
        spi_write_blocking(CAMWALLET_LCD_SPI, row, width * 2u);
    }
    gpio_put(CAMWALLET_LCD_CS_PIN, 1);
}

void lcd_clear(uint16_t color)
{
    lcd_fill_rect(0, 0, LCD_WIDTH, LCD_HEIGHT, color);
}

void lcd_init(void)
{
    output_pin_init(CAMWALLET_LCD_BL_PIN, false);
    output_pin_init(CAMWALLET_LCD_CS_PIN, true);
    output_pin_init(CAMWALLET_LCD_DC_PIN, false);
    output_pin_init(CAMWALLET_LCD_RST_PIN, true);

    /* Use a conservative SPI clock while bringing up the display. */
    spi_init(CAMWALLET_LCD_SPI, 40000000u);
    spi_set_format(CAMWALLET_LCD_SPI, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(CAMWALLET_LCD_SCK_PIN, GPIO_FUNC_SPI);
    gpio_set_function(CAMWALLET_LCD_MOSI_PIN, GPIO_FUNC_SPI);

    sleep_ms(100);
    gpio_put(CAMWALLET_LCD_RST_PIN, 0);
    sleep_ms(100);
    gpio_put(CAMWALLET_LCD_RST_PIN, 1);
    sleep_ms(100);

    write_command(0x11); /* Sleep out. */
    sleep_ms(120);
    write_command(0x36);
    /* Set MX to correct horizontal mirroring; retain the row and color order. */
    const uint8_t portrait[] = {0x48};
    write_data(portrait, sizeof(portrait));

    /* Preserve the panel-specific initialization values from the vendor demo. */
    static const struct {
        uint8_t command;
        uint8_t size;
        uint8_t data[14];
    } initialization[] = {
        {0x3a, 1, {0x05}},
        {0xf0, 1, {0xc3}},
        {0xf0, 1, {0x96}},
        {0xb4, 1, {0x01}},
        {0xb7, 1, {0xc6}},
        {0xc0, 2, {0x80, 0x45}},
        {0xc1, 1, {0x13}},
        {0xc2, 1, {0xa7}},
        {0xc5, 1, {0x0a}},
        {0xe8, 8, {0x40, 0x8a, 0x00, 0x00, 0x29, 0x19, 0xa5, 0x33}},
        {0xe0, 14, {0xd0, 0x08, 0x0f, 0x06, 0x06, 0x33, 0x30,
                     0x33, 0x47, 0x17, 0x13, 0x13, 0x2b, 0x31}},
        {0xe1, 14, {0xd0, 0x0a, 0x11, 0x0b, 0x09, 0x07, 0x2f,
                     0x33, 0x47, 0x38, 0x15, 0x16, 0x2c, 0x32}},
        {0xf0, 1, {0x3c}},
        {0xf0, 1, {0x69}},
    };

    for (size_t i = 0; i < sizeof(initialization) / sizeof(initialization[0]); ++i) {
        write_command(initialization[i].command);
        write_data(initialization[i].data, initialization[i].size);
    }
    sleep_ms(120);
    write_command(0x21); /* Panel inversion setting from the vendor demo. */
    write_command(0x29); /* Display on. */

    lcd_clear(LCD_BLACK);
    gpio_put(CAMWALLET_LCD_BL_PIN, 1);
}

static void draw_character(uint16_t x, uint16_t y, unsigned char character,
                            uint8_t scale, uint16_t foreground,
                            uint16_t background)
{
    const uint16_t stride = (Font16.Width + 7u) / 8u;
    const size_t offset = (size_t)(character - ' ') * Font16.Height * stride;
    const uint8_t *glyph = &Font16.table[offset];
    const uint16_t width = Font16.Width * scale;
    const uint16_t height = Font16.Height * scale;
    uint8_t row[LCD_WIDTH * 2u];

    set_window(x, y, width, height);
    gpio_put(CAMWALLET_LCD_DC_PIN, 1);
    gpio_put(CAMWALLET_LCD_CS_PIN, 0);
    for (uint16_t font_y = 0; font_y < Font16.Height; ++font_y) {
        for (uint16_t font_x = 0; font_x < Font16.Width; ++font_x) {
            const bool ink = (glyph[font_y * stride + font_x / 8u]
                               & (0x80u >> (font_x % 8u))) != 0;
            const uint16_t color = ink ? foreground : background;
            for (uint8_t repeat_x = 0; repeat_x < scale; ++repeat_x) {
                const uint16_t column = font_x * scale + repeat_x;
                row[column * 2u] = color >> 8;
                row[column * 2u + 1u] = color & 0xff;
            }
        }
        for (uint8_t repeat_y = 0; repeat_y < scale; ++repeat_y) {
            spi_write_blocking(CAMWALLET_LCD_SPI, row, width * 2u);
        }
    }
    gpio_put(CAMWALLET_LCD_CS_PIN, 1);
}

void lcd_draw_text(uint16_t x, uint16_t y, const char *text, uint8_t scale,
                   uint16_t foreground, uint16_t background)
{
    if (text == NULL || scale == 0 || scale > 4) {
        return;
    }

    const uint16_t width = Font16.Width * scale;
    const uint16_t height = Font16.Height * scale;
    if (x >= LCD_WIDTH || y >= LCD_HEIGHT || height > LCD_HEIGHT - y) {
        return;
    }

    while (*text != '\0' && width <= LCD_WIDTH - x) {
        unsigned char character = (unsigned char)*text++;
        if (character < ' ' || character > '~') {
            character = '?';
        }
        draw_character(x, y, character, scale, foreground, background);
        x += width;
    }
}
