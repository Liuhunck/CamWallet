#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ur_transport.h"
#include "cur_memory.h"
#include "evm_request.h"
#include "qr.h"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); exit(1); } } while (0)
static char parts[CW_UR_SEQUENCE_MAX][CW_UR_PART_MAX + 1];
static uint8_t frame[240 * 320 * 2];

int main(void)
{
    uint8_t data[CW_UR_MESSAGE_MAX];
    for (size_t i = 0; i < sizeof(data); ++i) data[i] = (uint8_t)i;
    CHECK(cw_ur_tx_begin("bytes", data, sizeof(data)));
    unsigned count = cw_ur_tx_count();
    CHECK(count == CW_UR_SEQUENCE_MAX);
    for (unsigned i = 0; i < count; ++i) CHECK(cw_ur_tx_next(parts[i]));
    cw_ur_tx_end();
    CHECK(cw_ur_memory_used() == 0);
    cw_ur_message_t result;
    for (unsigned i = count; i > 0; --i) {
        cw_ur_status_t status = cw_ur_receive((uint8_t *)parts[i-1], strlen(parts[i-1]), count-i, &result);
        CHECK(status == (i == 1 ? CW_UR_COMPLETE : CW_UR_MORE));
        if (i > 1) {
            unsigned received = cw_ur_received();
            CHECK(cw_ur_receive((uint8_t *)parts[i-1], strlen(parts[i-1]), count-i, &result) == CW_UR_MORE);
            CHECK(cw_ur_received() == received);
        }
    }
    CHECK(result.length == sizeof(data) && !memcmp(result.cbor, data, sizeof(data)));
    CHECK(cw_ur_memory_used() == 0);
    CHECK(cw_ur_receive((uint8_t *)parts[0], strlen(parts[0]), 100, &result) == CW_UR_MORE);
    CHECK(!cw_ur_expire(30099));
    CHECK(cw_ur_expire(30100));
    CHECK(!cw_ur_active() && cw_ur_memory_used() == 0);

    /* No partial result on an embedded NUL, and no single-part takeover of a
     * multipart session. A bad frame cannot reset the absolute deadline. */
    CHECK(cw_ur_receive((uint8_t *)parts[0], strlen(parts[0]), 0, &result) == CW_UR_MORE);
    CHECK(cw_ur_receive((uint8_t *)"UR:BYTES/AA", 11, 10, &result) == CW_UR_REJECTED);
    CHECK(cw_ur_receive((uint8_t *)"UR:BYTES/AA\0XX", 14, 20, &result) == CW_UR_REJECTED);
    CHECK(cw_ur_received() == 1);
    CHECK(cw_ur_expire(CW_UR_TIMEOUT_MS));
    void *all = cw_ur_malloc(CW_UR_ARENA_BYTES - 128);
    CHECK(all != NULL);
    CHECK(cw_ur_receive((uint8_t *)parts[0], strlen(parts[0]), 0, &result) == CW_UR_NO_MEMORY);
    cw_ur_free(all);
    CHECK(cw_ur_memory_used() == 0);
    CHECK(cw_ur_receive((uint8_t *)parts[0], strlen(parts[0]), 0, &result) == CW_UR_MORE);
    cw_ur_reset();
    CHECK(cw_ur_memory_used() == 0);

    CHECK(qr_scanner_init(240, 320));
    qr_symbol_t symbol;
    qr_payload_t scanned;
    for (unsigned i = 0; i < count; ++i) {
        CHECK(qr_encode_ur(parts[i], &symbol));
        CHECK(qr_render_rgb565_be(&symbol, frame, sizeof(frame), 240, 320));
        CHECK(qr_scan_rgb565_be(frame, sizeof(frame), &scanned) == QR_SCAN_OK);
        CHECK(scanned.length == strlen(parts[i]) && !memcmp(scanned.bytes, parts[i], scanned.length));
    }
    qr_scanner_destroy();
    uint8_t wrapped[770]; const uint8_t *view; size_t length;
    CHECK(cw_cbor_wrap_bytes(data, 767, wrapped, 770) == 770);
    CHECK(cw_cbor_unwrap_bytes(wrapped, 770, &view, &length) && length == 767 && !memcmp(view, data, length));
    CHECK(!cw_cbor_unwrap_bytes(wrapped, 769, &view, &length));
    CHECK(!cw_cbor_unwrap_bytes((uint8_t[]){0x58, 1, 0}, 3, &view, &length));
    printf("PASS: max-size reverse/duplicate UR, timeout, rejection, OOM recovery, 32 rendered QR frames; arena peak=%u/%u\n",
        (unsigned)cw_ur_memory_peak(), CW_UR_ARENA_BYTES);
    return 0;
}
