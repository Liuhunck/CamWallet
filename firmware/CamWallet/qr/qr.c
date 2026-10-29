#include "qr.h"

#include <limits.h>
#include <string.h>

#include "quirc.h"

static struct quirc *scanner;
static unsigned scan_width;
static unsigned scan_height;
static qr_scan_info_t scan_info;

/* These upstream objects are several KiB each. Keep them off the MCU stack. */
static struct quirc_code scanned_code;
static struct quirc_data scanned_data;

static bool frame_size(unsigned width, unsigned height, size_t *bytes)
{
    if (width == 0 || height == 0 || width > INT_MAX || height > INT_MAX
        || (size_t)width > SIZE_MAX / height / 2u) {
        return false;
    }
    *bytes = (size_t)width * height * 2u;
    return true;
}

bool qr_scanner_init(unsigned width, unsigned height)
{
    size_t bytes;
    if (scanner != NULL) {
        return width == scan_width && height == scan_height;
    }
    if (!frame_size(width, height, &bytes)) {
        return false;
    }
    struct quirc *candidate = quirc_new();
    if (candidate == NULL) {
        return false;
    }
    if (quirc_resize(candidate, (int)width, (int)height) < 0) {
        quirc_destroy(candidate);
        return false;
    }
    scanner = candidate;
    scan_width = width;
    scan_height = height;
    return true;
}

void qr_scanner_destroy(void)
{
    if (scanner != NULL) {
        quirc_destroy(scanner);
    }
    scanner = NULL;
    scan_width = 0;
    scan_height = 0;
}

static bool supported_eci(uint32_t eci)
{
    /* No ECI, ISO-8859-1, or UTF-8. Do not guess other text encodings. */
    return eci == 0 || eci == 3 || eci == 26;
}

qr_scan_info_t qr_last_scan_info(void)
{
    return scan_info;
}

qr_scan_status_t qr_scan_rgb565_be(const uint8_t *pixels, size_t size,
                                  qr_payload_t *payload)
{
    memset(&scan_info, 0, sizeof(scan_info));
    if (payload == NULL) {
        return QR_SCAN_BAD_FRAME;
    }
    memset(payload, 0, sizeof(*payload));
    size_t expected_size;
    if (scanner == NULL || pixels == NULL
        || !frame_size(scan_width, scan_height, &expected_size)
        || size != expected_size) {
        return QR_SCAN_BAD_FRAME;
    }

    uint8_t *gray = quirc_begin(scanner, NULL, NULL);
    scan_info.gray_min = 255;
    for (size_t i = 0; i < (size_t)scan_width * scan_height; ++i) {
        const uint16_t rgb = (uint16_t)pixels[2u * i] << 8 | pixels[2u * i + 1u];
        const unsigned r5 = rgb >> 11;
        const unsigned g6 = (rgb >> 5) & 63u;
        const unsigned b5 = rgb & 31u;
        const unsigned red = (r5 << 3) | (r5 >> 2);
        const unsigned green = (g6 << 2) | (g6 >> 4);
        const unsigned blue = (b5 << 3) | (b5 >> 2);
        gray[i] = (uint8_t)((77u * red + 150u * green + 29u * blue) >> 8);
        if (gray[i] < scan_info.gray_min) {
            scan_info.gray_min = gray[i];
        }
        if (gray[i] > scan_info.gray_max) {
            scan_info.gray_max = gray[i];
        }
    }
    quirc_end(scanner);

    const int count = quirc_count(scanner);
    scan_info.candidates = (unsigned)count;
    qr_scan_status_t rejected = count > 0 ? QR_SCAN_DECODE_FAILED : QR_SCAN_NONE;
    for (int i = 0; i < count; ++i) {
        quirc_extract(scanner, i, &scanned_code);
        quirc_decode_error_t error = quirc_decode(&scanned_code, &scanned_data);
        if (error != QUIRC_SUCCESS) {
            scan_info.decode_error = quirc_strerror(error);
            quirc_flip(&scanned_code);
            error = quirc_decode(&scanned_code, &scanned_data);
            if (error != QUIRC_SUCCESS) {
                scan_info.mirrored_error = quirc_strerror(error);
            }
        }
        if (error != QUIRC_SUCCESS) {
            continue;
        }
        ++scan_info.decoded;
        if (scanned_data.payload_len <= 0) {
            rejected = QR_SCAN_EMPTY;
            continue;
        }
        if ((size_t)scanned_data.payload_len > QR_PAYLOAD_MAX_BYTES) {
            rejected = QR_SCAN_TOO_LONG;
            continue;
        }
        if (scanned_data.data_type == QUIRC_DATA_TYPE_KANJI
            || !supported_eci(scanned_data.eci)) {
            rejected = QR_SCAN_UNSUPPORTED;
            continue;
        }
        payload->length = (size_t)scanned_data.payload_len;
        payload->eci = scanned_data.eci;
        memcpy(payload->bytes, scanned_data.payload, payload->length);
        return QR_SCAN_OK;
    }
    return rejected;
}

bool qr_encode_payload(const qr_payload_t *payload, qr_symbol_t *symbol)
{
    if (symbol == NULL) {
        return false;
    }
    memset(symbol, 0, sizeof(*symbol));
    if (payload == NULL || payload->length == 0
        || payload->length > QR_PAYLOAD_MAX_BYTES || !supported_eci(payload->eci)) {
        return false;
    }

    uint8_t work[sizeof(symbol->modules)];
    uint8_t byte_segment[QR_PAYLOAD_MAX_BYTES];
    uint8_t eci_segment[3];
    struct qrcodegen_Segment segments[2];
    size_t count = 0;
    if (payload->eci != 0) {
        segments[count++] = qrcodegen_makeEci((long)payload->eci, eci_segment);
    }
    segments[count++] = qrcodegen_makeBytes(payload->bytes, payload->length,
                                           byte_segment);
    return qrcodegen_encodeSegmentsAdvanced(segments, count, qrcodegen_Ecc_MEDIUM,
                                            1, QR_SYMBOL_MAX_VERSION,
                                            qrcodegen_Mask_AUTO, true,
                                            work, symbol->modules);
}

bool qr_render_rgb565_be(const qr_symbol_t *symbol, uint8_t *pixels, size_t size,
                         unsigned width, unsigned height)
{
    size_t expected_size;
    if (symbol == NULL || pixels == NULL || !frame_size(width, height, &expected_size)
        || size != expected_size || width <= 2u * QR_DISPLAY_MARGIN
        || height <= QR_DISPLAY_HEADER + QR_DISPLAY_FOOTER) {
        return false;
    }
    /* qrcodegen stores its side length in byte zero. Validate it before any
     * accessor asserts on an uninitialized or unsuccessfully encoded symbol.
     */
    const int modules = symbol->modules[0];
    if (modules < 21 || modules > 4 * QR_SYMBOL_MAX_VERSION + 17
        || (modules - 17) % 4 != 0) {
        return false;
    }
    const unsigned total_modules = (unsigned)modules + 2u * QR_QUIET_ZONE_MODULES;
    const unsigned available_width = width - 2u * QR_DISPLAY_MARGIN;
    const unsigned available_height = height - QR_DISPLAY_HEADER - QR_DISPLAY_FOOTER;
    const unsigned available = available_width < available_height
        ? available_width : available_height;
    const unsigned scale = available / total_modules;
    if (scale < QR_DISPLAY_MIN_SCALE) {
        return false;
    }
    const unsigned square = total_modules * scale;
    const unsigned left = (width - square) / 2u + QR_QUIET_ZONE_MODULES * scale;
    const unsigned top = QR_DISPLAY_HEADER + (available_height - square) / 2u
        + QR_QUIET_ZONE_MODULES * scale;

    memset(pixels, 0xff, size);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (!qrcodegen_getModule(symbol->modules, x, y)) {
                continue;
            }
            for (unsigned dy = 0; dy < scale; ++dy) {
                const size_t start = ((size_t)(top + (unsigned)y * scale + dy) * width
                                      + left + (unsigned)x * scale) * 2u;
                memset(pixels + start, 0, scale * 2u);
            }
        }
    }
    return true;
}
