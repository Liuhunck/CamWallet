/* Register setup/pins from Waveshare RP2350-Touch-LCD-2 C/01-LCD,
 * CST816D.c/.h and DEV_Config.h (2025-03-13, Waveshare team).
 * See THIRD_PARTY_NOTICES.txt for the accompanying MIT notice. */
#include "touch.h"
#include "board_pins.h"
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include <stdio.h>

#define TOUCH_ADDRESS 0x15
#define TOUCH_TIMEOUT_US 3000

static bool read_regs(uint8_t reg, uint8_t *out, size_t size)
{
    return i2c_write_timeout_us(CAMWALLET_TOUCH_I2C, TOUCH_ADDRESS, &reg, 1, true,
                               TOUCH_TIMEOUT_US) == 1
        && i2c_read_timeout_us(CAMWALLET_TOUCH_I2C, TOUCH_ADDRESS, out, size, false,
                              TOUCH_TIMEOUT_US) == (int)size;
}

static bool write_reg(uint8_t reg, uint8_t value)
{
    const uint8_t data[] = {reg, value};
    return i2c_write_timeout_us(CAMWALLET_TOUCH_I2C, TOUCH_ADDRESS, data, 2, false,
                               TOUCH_TIMEOUT_US) == 2;
}

bool touch_init(void)
{
    i2c_init(CAMWALLET_TOUCH_I2C, 400000);
    gpio_set_function(CAMWALLET_TOUCH_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(CAMWALLET_TOUCH_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(CAMWALLET_TOUCH_SDA_PIN);
    gpio_pull_up(CAMWALLET_TOUCH_SCL_PIN);
    gpio_init(CAMWALLET_TOUCH_INT_PIN);
    gpio_set_dir(CAMWALLET_TOUCH_INT_PIN, GPIO_IN);
    gpio_pull_up(CAMWALLET_TOUCH_INT_PIN);
    /* No reset here: GPIO20 would reset the initialized LCD as well. */
    uint8_t id = 0, revision = 0;
    bool ok = read_regs(0xa7, &id, 1) && id == 0xb6
        && read_regs(0xa9, &revision, 1)
        && write_reg(0xfe, 1)     /* Disable auto sleep for bounded polling. */
        && write_reg(0xfa, 0x41)  /* Point mode. No I2C work in an ISR. */
        && write_reg(0xed, 1) && write_reg(0xee, 1);
    printf("[Touch] CST816D id=0x%02x fw=%u %s; i2c0 SDA12 SCL13 INT29 reset=LCD20\n",
           id, revision, ok ? "READY" : "FAILED");
    return ok;
}

bool touch_read(uint8_t *fingers, uint16_t *x, uint16_t *y)
{
    uint8_t report[5];
    return read_regs(0x02, report, sizeof(report)) && touch_decode(report, fingers, x, y);
}
