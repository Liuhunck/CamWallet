#ifndef CAMWALLET_CAMERA_H
#define CAMWALLET_CAMERA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define CAMERA_WIDTH 240u
#define CAMERA_HEIGHT 320u
#define CAMERA_FRAME_BYTES (CAMERA_WIDTH * CAMERA_HEIGHT * 2u)

/* Initialize once. chip_id is filled when the SCCB sensor ID can be read. */
bool camera_init(uint16_t *chip_id);

/* Start a frame in LCD wire order (RGB565, high byte first), then return.
 * The caller owns the buffer. It must be word-aligned and must not be read or
 * reused until camera_capture_wait() returns. Only a successful capture yields
 * a valid frame. Only one capture may be active.
 */
bool camera_capture_start(uint8_t *frame, size_t size);

/* Wait for the active frame. The timeout includes time since camera_capture_start(),
 * including work done by the caller before waiting. A timeout aborts capture;
 * a subsequent start can retry. elapsed_us includes frame synchronization and
 * the time until completion is observed, and may be NULL.
 */
bool camera_capture_wait(uint32_t timeout_ms, uint32_t *elapsed_us);

/* Last interval between sensor VSYNC rising edges; zero until measured.
 * Independent of how often the main loop displays a completed frame.
 */
uint32_t camera_sensor_period_us(void);
const char *camera_last_error(void);

#endif
