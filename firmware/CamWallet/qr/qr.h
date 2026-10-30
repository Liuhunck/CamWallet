#ifndef CAMWALLET_QR_H
#define CAMWALLET_QR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qrcodegen.h"

#define QR_PAYLOAD_MAX_BYTES 128u
#define QR_SYMBOL_MAX_VERSION 9
#define QR_QUIET_ZONE_MODULES 4u
#define QR_DISPLAY_MARGIN 8u
#define QR_DISPLAY_HEADER 40u
#define QR_DISPLAY_FOOTER 40u
#define QR_DISPLAY_MIN_SCALE 3u

typedef struct {
    size_t length;
    uint32_t eci;
    uint8_t bytes[QR_PAYLOAD_MAX_BYTES];
} qr_payload_t;

typedef struct {
    uint8_t modules[qrcodegen_BUFFER_LEN_FOR_VERSION(QR_SYMBOL_MAX_VERSION)];
} qr_symbol_t;

typedef enum {
    QR_SCAN_NONE,
    QR_SCAN_DECODE_FAILED,
    QR_SCAN_OK,
    QR_SCAN_TOO_LONG,
    QR_SCAN_UNSUPPORTED,
    QR_SCAN_EMPTY,
    QR_SCAN_BAD_FRAME,
} qr_scan_status_t;

typedef struct {
    unsigned candidates;
    unsigned decoded;
    uint8_t gray_min;
    uint8_t gray_max;
    const char *decode_error;
    const char *mirrored_error;
} qr_scan_info_t;

/* One scanner, used by the main loop only. Allocates once at startup. */
bool qr_scanner_init(unsigned width, unsigned height);
void qr_scanner_destroy(void);

/* Pixels must be a completed, stable RGB565 frame, high byte first.
 * Returns the first supported payload; failure never returns partial bytes.
 * Numeric, alphanumeric and byte segments are supported. ECI: absent, 3 or 26.
 */
qr_scan_status_t qr_scan_rgb565_be(const uint8_t *pixels, size_t size,
                                  qr_payload_t *payload);
/* Diagnostics for the most recent scan, including failed decodes. Error
 * strings have static lifetime; the snapshot is safe to retain between frames.
 */
qr_scan_info_t qr_last_scan_info(void);
bool qr_encode_payload(const qr_payload_t *payload, qr_symbol_t *symbol);
/* Uppercase UR text uses QR alphanumeric mode; same physical size limits. */
bool qr_encode_ur(const char *text, qr_symbol_t *symbol);

/* Renders a white frame with a centered black QR, four-module quiet zone,
 * and at least three physical pixels per module. Header/footer stay white.
 * Call only after camera DMA has released this buffer.
 */
bool qr_render_rgb565_be(const qr_symbol_t *symbol, uint8_t *pixels, size_t size,
                         unsigned width, unsigned height);

#endif
