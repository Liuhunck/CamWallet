#ifndef CAMWALLET_SCAN_PREVIEW_H
#define CAMWALLET_SCAN_PREVIEW_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Compose white ASCII text on the top/bottom 24-pixel black bars of a full
 * LCD-sized RGB565 BE frame. The middle camera image is left intact.
 * Call AFTER QR decoding, on a completed frame not owned by capture DMA,
 * then send the composed frame to the LCD in one transfer. No allocation/I/O.
 */
bool scan_preview_compose(uint8_t *frame, size_t size,
                          const char *status, const char *counts);

#endif
