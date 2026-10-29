/*
 * OV5640 register configuration adapted from Waveshare's RP2350-Touch-LCD-2
 * C/02-CAM example (2025-03-13). Original author: Waveshare team.
 * Permission and disclaimer: see THIRD_PARTY_NOTICES.txt.
 */

#include "camera.h"

#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/pwm.h"
#include "pico/stdlib.h"

#include "board_pins.h"
#include "camera_capture.pio.h"
#include "ov5640_default_regs.h"

#define CAMERA_ADDRESS 0x3cu
#define CAMERA_I2C_TIMEOUT_US 20000u

_Static_assert(CAMWALLET_CAM_VSYNC_PIN == CAMWALLET_CAM_DATA_PIN + 8,
               "PIO VSYNC pin must follow the eight data pins");
_Static_assert(CAMWALLET_CAM_HREF_PIN == CAMWALLET_CAM_DATA_PIN + 9,
               "PIO HREF pin must follow VSYNC");
_Static_assert(CAMWALLET_CAM_PCLK_PIN == CAMWALLET_CAM_DATA_PIN + 10,
               "PIO PCLK pin must follow HREF");
_Static_assert(CAMERA_FRAME_BYTES % sizeof(uint32_t) == 0,
               "Word-wide DMA requires a whole number of words");

static PIO camera_pio = pio0;
static int camera_sm = -1;
static int camera_dma = -1;
static uint camera_program_offset;
static bool camera_ready;
static bool capture_active;
static absolute_time_t capture_started_at;
static uint32_t last_vsync_us;
static bool vsync_seen;
static volatile uint32_t sensor_period_us;
static const char *last_error = "NOT INITIALIZED";

const char *camera_last_error(void)
{
    return last_error;
}

static void sensor_vsync_callback(uint gpio, uint32_t events)
{
    if (gpio != CAMWALLET_CAM_VSYNC_PIN || !(events & GPIO_IRQ_EDGE_RISE)) {
        return;
    }
    const uint32_t now = time_us_32();
    if (vsync_seen) {
        sensor_period_us = now - last_vsync_us;
    }
    last_vsync_us = now;
    vsync_seen = true;
}

uint32_t camera_sensor_period_us(void)
{
    /* Aligned 32-bit reads are atomic; only the GPIO ISR writes this value. */
    return sensor_period_us;
}

static bool write_register(uint16_t address, uint8_t value)
{
    const uint8_t data[] = {address >> 8, address & 0xff, value};
    const int result = i2c_write_timeout_us(CAMWALLET_CAM_I2C, CAMERA_ADDRESS,
                                           data, sizeof(data), false,
                                           CAMERA_I2C_TIMEOUT_US);
    if (result != (int)sizeof(data)) {
        printf("[Camera] register write failed: reg=0x%04x result=%d\n",
               address, result);
        last_error = "REGISTER WRITE ERROR";
        return false;
    }
    return true;
}

static bool read_register(uint16_t address, uint8_t *value)
{
    const uint8_t reg[] = {address >> 8, address & 0xff};
    int result = i2c_write_timeout_us(CAMWALLET_CAM_I2C, CAMERA_ADDRESS,
                                     reg, sizeof(reg), true,
                                     CAMERA_I2C_TIMEOUT_US);
    if (result != (int)sizeof(reg)) {
        last_error = "SCCB READ ERROR";
        return false;
    }
    result = i2c_read_timeout_us(CAMWALLET_CAM_I2C, CAMERA_ADDRESS, value, 1,
                                 false, CAMERA_I2C_TIMEOUT_US);
    if (result != 1) {
        last_error = "SCCB READ ERROR";
        return false;
    }
    return true;
}

static bool write_u16(uint16_t address, uint16_t value)
{
    return write_register(address, value >> 8)
        && write_register(address + 1u, value & 0xff);
}

static void start_external_clock(void)
{
    /* Match the vendor's approximately 37 MHz XCLK, with an exact 50% duty
     * cycle at the current 150 MHz system clock (37.5 MHz = 150 MHz / 4).
     * No system clock or voltage changes are required.
     */
    uint32_t period = clock_get_hz(clk_sys) / 37000000u;
    if (period < 2u) {
        period = 2u;
    }
    const uint slice = pwm_gpio_to_slice_num(CAMWALLET_CAM_XCLK_PIN);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, period - 1u);
    pwm_config_set_clkdiv(&config, 1.0f);
    gpio_set_function(CAMWALLET_CAM_XCLK_PIN, GPIO_FUNC_PWM);
    pwm_init(slice, &config, false);
    pwm_set_gpio_level(CAMWALLET_CAM_XCLK_PIN, period / 2u);
    pwm_set_enabled(slice, true);
    printf("[Camera] XCLK=%lu Hz\n",
           (unsigned long)(clock_get_hz(clk_sys) / period));
}

static bool configure_sensor(void)
{
    for (size_t i = 0; i < sizeof(ov5640_default_regs) / sizeof(ov5640_default_regs[0]); ++i) {
        const uint16_t address = ov5640_default_regs[i][0];
        const uint16_t value = ov5640_default_regs[i][1];
        if (address == 0 && value == 0) {
            break;
        }
        if (address == 0xffffu) {
            sleep_ms(value);
        } else if (!write_register(address, (uint8_t)value)) {
            return false;
        }
    }
    sleep_ms(50);

    /* Vendor portrait crop and output timing. Each output frame is exactly
     * one LCD image (153600 bytes).
     */
    if (!write_u16(0x3800, 352) || !write_u16(0x3802, 26)
        || !write_u16(0x3804, 1792) || !write_u16(0x3806, 1946)
        || !write_u16(0x3808, CAMERA_WIDTH) || !write_u16(0x380a, CAMERA_HEIGHT)
        || !write_u16(0x380c, 2592) || !write_u16(0x380e, 1944)
        || !write_u16(0x3810, 16) || !write_u16(0x3812, 14)) {
        return false;
    }
    uint8_t isp_control;
    if (!read_register(0x5001, &isp_control)
        || !write_register(0x5001, isp_control | 0x20u)) {
        return false;
    }
    sleep_ms(50);

    static const struct {
        uint16_t address;
        uint8_t value;
    } preview_registers[] = {
        /* Correct the upside-down preview at the sensor: enable both sensor
         * and ISP vertical flip (bits 2:1), retaining vertical binning (bit 0).
         * Leave horizontal orientation unchanged. 0x4514 follows the binned
         * V-flip setting in the Espressif OV5640 driver's set_image_options.
         */
        {0x3820, 0x07}, {0x3821, 0x00},
        {0x4514, 0xbb}, {0x4520, 0x0b},
        {0x3814, 0x31}, {0x3815, 0x31},
        {0xffff, 50},
        {0x3039, 0x00}, {0x3034, 0x1a},
        {0x3035, 0x11}, {0x3036, 0x0b}, {0x3037, 0x01},
        {0x3108, 0x16}, {0x3824, 0x04}, {0x460c, 0x22}, {0x3103, 0x13},
        {0xffff, 50},
        /* 0x61 outputs {R[4:0], G[5:3]} then {G[2:0], B[4:0]}.
         * PIO and word-wide DMA preserve this LCD-compatible wire order.
         */
        {0x501f, 0x01}, {0x4300, 0x61},
    };
    for (size_t i = 0; i < sizeof(preview_registers) / sizeof(preview_registers[0]); ++i) {
        if (preview_registers[i].address == 0xffffu) {
            sleep_ms(preview_registers[i].value);
            continue;
        }
        if (!write_register(preview_registers[i].address, preview_registers[i].value)) {
            return false;
        }
    }
    sleep_ms(300);

    uint8_t width_h, width_l, height_h, height_l;
    if (!read_register(0x3808, &width_h) || !read_register(0x3809, &width_l)
        || !read_register(0x380a, &height_h) || !read_register(0x380b, &height_l)) {
        return false;
    }
    if (((uint16_t)width_h << 8 | width_l) != CAMERA_WIDTH
        || ((uint16_t)height_h << 8 | height_l) != CAMERA_HEIGHT) {
        last_error = "SIZE VERIFY ERROR";
        return false;
    }
    return true;
}

bool camera_init(uint16_t *chip_id)
{
    if (camera_ready) {
        return true;
    }
    if (chip_id != NULL) {
        *chip_id = 0;
    }

    gpio_init(CAMWALLET_CAM_PWDN_PIN);
    gpio_put(CAMWALLET_CAM_PWDN_PIN, 1);
    gpio_set_dir(CAMWALLET_CAM_PWDN_PIN, GPIO_OUT);
    start_external_clock();
    sleep_ms(10);
    gpio_put(CAMWALLET_CAM_PWDN_PIN, 0);
    sleep_ms(50);

    i2c_init(CAMWALLET_CAM_I2C, 100000u);
    gpio_set_function(CAMWALLET_CAM_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(CAMWALLET_CAM_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(CAMWALLET_CAM_SDA_PIN);
    gpio_pull_up(CAMWALLET_CAM_SCL_PIN);

    uint8_t id_h, id_l;
    if (!read_register(0x300a, &id_h) || !read_register(0x300b, &id_l)) {
        printf("[Camera] no SCCB response at 0x3c\n");
        return false;
    }
    const uint16_t id = (uint16_t)id_h << 8 | id_l;
    if (chip_id != NULL) {
        *chip_id = id;
    }
    printf("[Camera] chip_id=0x%04x\n", id);
    if (id != 0x5640u) {
        last_error = "WRONG SENSOR ID";
        return false;
    }
    if (!configure_sensor()) {
        return false;
    }

    if (!pio_can_add_program(camera_pio, &camera_capture_program)) {
        last_error = "PIO MEMORY BUSY";
        return false;
    }
    camera_sm = pio_claim_unused_sm(camera_pio, false);
    if (camera_sm < 0) {
        last_error = "PIO BUSY";
        return false;
    }
    camera_dma = dma_claim_unused_channel(false);
    if (camera_dma < 0) {
        pio_sm_unclaim(camera_pio, (uint)camera_sm);
        camera_sm = -1;
        last_error = "DMA BUSY";
        return false;
    }

    camera_program_offset = pio_add_program(camera_pio, &camera_capture_program);
    for (uint pin = CAMWALLET_CAM_DATA_PIN; pin <= CAMWALLET_CAM_PCLK_PIN; ++pin) {
        pio_gpio_init(camera_pio, pin);
        gpio_disable_pulls(pin);
    }
    pio_sm_set_consecutive_pindirs(camera_pio, (uint)camera_sm,
                                   CAMWALLET_CAM_DATA_PIN, 11, false);
    pio_sm_config config = camera_capture_program_get_default_config(camera_program_offset);
    sm_config_set_in_pins(&config, CAMWALLET_CAM_DATA_PIN);
    sm_config_set_in_shift(&config, true, true, 32);
    /* Keep the TX FIFO available for the byte-count PULL at each capture. */
    sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_NONE);
    pio_sm_init(camera_pio, (uint)camera_sm, camera_program_offset, &config);

    /* Observe the sensor's own frame period even while the CPU drives LCD SPI.
     * This callback runs on the same core as init and only timestamps VSYNC;
     * DVP sampling remains entirely in PIO/DMA.
     */
    gpio_set_irq_enabled_with_callback(CAMWALLET_CAM_VSYNC_PIN,
                                       GPIO_IRQ_EDGE_RISE, true,
                                       sensor_vsync_callback);

    camera_ready = true;
    last_error = "OK";
    printf("[Camera] ready: %ux%u RGB565, pipelined capture, DMA32\n",
           CAMERA_WIDTH, CAMERA_HEIGHT);
    return true;
}

bool camera_capture_start(uint8_t *frame, size_t size)
{
    if (!camera_ready) {
        last_error = "NOT INITIALIZED";
        return false;
    }
    if (capture_active) {
        last_error = "CAPTURE BUSY";
        return false;
    }
    if (frame == NULL || size < CAMERA_FRAME_BYTES
        || (uintptr_t)frame % sizeof(uint32_t) != 0) {
        last_error = "BUFFER ERROR";
        return false;
    }

    const uint sm = (uint)camera_sm;
    pio_sm_set_enabled(camera_pio, sm, false);
    pio_sm_clear_fifos(camera_pio, sm);
    pio_sm_restart(camera_pio, sm);
    pio_sm_clkdiv_restart(camera_pio, sm);
    pio_sm_exec(camera_pio, sm, pio_encode_jmp(camera_program_offset));
    pio_sm_put(camera_pio, sm, CAMERA_FRAME_BYTES - 1u);

    dma_channel_config config = dma_channel_get_default_config((uint)camera_dma);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_32);
    channel_config_set_read_increment(&config, false);
    channel_config_set_write_increment(&config, true);
    channel_config_set_dreq(&config, pio_get_dreq(camera_pio, sm, false));
    dma_channel_configure((uint)camera_dma, &config, frame, &camera_pio->rxf[sm],
                           CAMERA_FRAME_BYTES / sizeof(uint32_t), false);

    capture_started_at = get_absolute_time();
    capture_active = true;
    dma_channel_start((uint)camera_dma);
    pio_sm_set_enabled(camera_pio, sm, true);
    last_error = "OK";
    return true;
}

bool camera_capture_wait(uint32_t timeout_ms, uint32_t *elapsed_us)
{
    if (!capture_active) {
        last_error = "NO ACTIVE CAPTURE";
        return false;
    }

    const uint sm = (uint)camera_sm;
    const absolute_time_t deadline = delayed_by_ms(capture_started_at, timeout_ms);
    while (dma_channel_is_busy((uint)camera_dma)) {
        if (time_reached(deadline)) {
            pio_sm_set_enabled(camera_pio, sm, false);
            const unsigned long remaining =
                dma_channel_hw_addr((uint)camera_dma)->transfer_count * sizeof(uint32_t);
            dma_channel_abort((uint)camera_dma);
            capture_active = false;
            last_error = "FRAME TIMEOUT";
            printf("[Camera] timeout: remaining=%lu VSYNC=%d HREF=%d PCLK=%d PC=%u\n",
                   remaining, gpio_get(CAMWALLET_CAM_VSYNC_PIN),
                   gpio_get(CAMWALLET_CAM_HREF_PIN), gpio_get(CAMWALLET_CAM_PCLK_PIN),
                   pio_sm_get_pc(camera_pio, sm));
            return false;
        }
        tight_loop_contents();
    }
    pio_sm_set_enabled(camera_pio, sm, false);
    /* Match the SDK DMA wait helper: keep framebuffer reads after completion. */
    __compiler_memory_barrier();
    capture_active = false;
    if (elapsed_us != NULL) {
        *elapsed_us = (uint32_t)absolute_time_diff_us(capture_started_at,
                                                    get_absolute_time());
    }
    last_error = "OK";
    return true;
}
