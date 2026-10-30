#ifndef CAMWALLET_TOUCH_H
#define CAMWALLET_TOUCH_H
#include <stdbool.h>
#include <stdint.h>

/* Call after lcd_init: its shared GPIO20 reset also resets the CST816D. */
bool touch_init(void);
/* False is an I/O/invalid-report error, NEVER a synthetic finger release. */
bool touch_read(uint8_t *fingers, uint16_t *x, uint16_t *y);
bool touch_decode(const uint8_t report[5], uint8_t *fingers, uint16_t *x, uint16_t *y);
#endif
