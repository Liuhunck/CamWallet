#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qr.h"

#define WIDTH 240u
#define HEIGHT 320u
#define FRAME_BYTES (WIDTH * HEIGHT * 2u)
#define GUARD_BYTES 32u
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static uint8_t source_frame[FRAME_BYTES];
static uint8_t guarded_response[GUARD_BYTES + FRAME_BYTES + GUARD_BYTES];
static uint8_t source_symbol[qrcodegen_BUFFER_LEN_FOR_VERSION(12)];
static unsigned test_count;

static void source_encode(const uint8_t *bytes, size_t length, uint32_t eci)
{
    CHECK(length <= 256u);
    uint8_t work[sizeof(source_symbol)];
    uint8_t data[256];
    uint8_t eci_data[3];
    struct qrcodegen_Segment segments[2];
    size_t count = 0;
    if (eci != 0) {
        segments[count++] = qrcodegen_makeEci((long)eci, eci_data);
    }
    segments[count++] = qrcodegen_makeBytes(bytes, length, data);
    CHECK(qrcodegen_encodeSegmentsAdvanced(segments, count, qrcodegen_Ecc_MEDIUM,
                                           1, 12, qrcodegen_Mask_AUTO, false,
                                           work, source_symbol));
}

/* Independent source-image renderer. This does not call the firmware renderer.
 * Orientation 1 rotates the QR 90 degrees; orientation 2 mirrors it.
 */
static void source_render(unsigned orientation, uint16_t ink)
{
    memset(source_frame, 0xff, sizeof(source_frame));
    const int modules = qrcodegen_getSize(source_symbol);
    const unsigned scale = WIDTH / ((unsigned)modules + 8u);
    CHECK(scale >= 2u);
    const unsigned left = (WIDTH - (unsigned)modules * scale) / 2u;
    const unsigned top = (HEIGHT - (unsigned)modules * scale) / 2u;
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (!qrcodegen_getModule(source_symbol, x, y)) {
                continue;
            }
            const unsigned dx = orientation == 1 ? (unsigned)(modules - 1 - y)
                : orientation == 2 ? (unsigned)(modules - 1 - x) : (unsigned)x;
            const unsigned dy = orientation == 1 ? (unsigned)x : (unsigned)y;
            for (unsigned py = 0; py < scale; ++py) {
                for (unsigned px = 0; px < scale; ++px) {
                    const size_t offset = ((top + dy * scale + py) * WIDTH
                                           + left + dx * scale + px) * 2u;
                    source_frame[offset] = (uint8_t)(ink >> 8);
                    source_frame[offset + 1u] = (uint8_t)ink;
                }
            }
        }
    }
}

static void check_guards(void)
{
    for (unsigned i = 0; i < GUARD_BYTES; ++i) {
        CHECK(guarded_response[i] == 0xa5);
        CHECK(guarded_response[GUARD_BYTES + FRAME_BYTES + i] == 0xa5);
    }
}

static void roundtrip(const char *name, const uint8_t *bytes, size_t length,
                       uint32_t eci, unsigned orientation, uint16_t ink)
{
    source_encode(bytes, length, eci);
    source_render(orientation, ink);
    qr_payload_t scanned;
    CHECK(qr_scan_rgb565_be(source_frame, sizeof(source_frame), &scanned) == QR_SCAN_OK);
    const qr_scan_info_t info = qr_last_scan_info();
    CHECK(info.candidates > 0);
    CHECK(info.decoded > 0);
    CHECK(info.gray_min < info.gray_max);
    CHECK(scanned.length == length);
    CHECK(scanned.eci == eci);
    CHECK(memcmp(scanned.bytes, bytes, length) == 0);
    qr_symbol_t echo;
    CHECK(qr_encode_payload(&scanned, &echo));
    memset(guarded_response, 0xa5, sizeof(guarded_response));
    uint8_t *response = guarded_response + GUARD_BYTES;
    CHECK(qr_render_rgb565_be(&echo, response, FRAME_BYTES, WIDTH, HEIGHT));
    check_guards();
    /* The UI's header/footer must stay outside the QR and its quiet zone. */
    for (size_t i = 0; i < WIDTH * QR_DISPLAY_HEADER * 2u; ++i) {
        CHECK(response[i] == 0xff);
    }
    for (size_t i = WIDTH * (HEIGHT - QR_DISPLAY_FOOTER) * 2u; i < FRAME_BYTES; ++i) {
        CHECK(response[i] == 0xff);
    }
    qr_payload_t returned;
    CHECK(qr_scan_rgb565_be(response, FRAME_BYTES, &returned) == QR_SCAN_OK);
    CHECK(returned.length == length);
    CHECK(returned.eci == eci);
    CHECK(memcmp(returned.bytes, bytes, length) == 0);
    printf("PASS %s (%u bytes, ECI %lu)\n", name, (unsigned)length, (unsigned long)eci);
    ++test_count;
}

static void rejection(const char *name, const uint8_t *bytes, size_t length,
                       uint32_t eci, qr_scan_status_t expected)
{
    source_encode(bytes, length, eci);
    source_render(0, 0);
    qr_payload_t payload;
    memset(&payload, 0xa5, sizeof(payload));
    CHECK(qr_scan_rgb565_be(source_frame, sizeof(source_frame), &payload) == expected);
    CHECK(payload.length == 0);
    CHECK(payload.eci == 0);
    for (size_t i = 0; i < sizeof(payload.bytes); ++i) {
        CHECK(payload.bytes[i] == 0);
    }
    printf("PASS %s (rejected without partial payload)\n", name);
    ++test_count;
}

static void write_fixture(const char *path)
{
    static const char message[] = "CamWallet:hello-12345";
    source_encode((const uint8_t *)message, strlen(message), 0);
    const int modules = qrcodegen_getSize(source_symbol);
    FILE *output = fopen(path, "wb");
    CHECK(output != NULL);
    fprintf(output, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!-- Public test data only: %s -->\n"
            "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"444\" height=\"444\" "
            "viewBox=\"0 0 %d %d\" shape-rendering=\"crispEdges\">\n"
            "<title>%s</title>\n<rect width=\"100%%\" height=\"100%%\" fill=\"white\"/>\n"
            "<path fill=\"black\" d=\"", message, modules + 8, modules + 8, message);
    for (int y = 0; y < modules; ++y) {
        for (int x = 0; x < modules; ++x) {
            if (qrcodegen_getModule(source_symbol, x, y)) {
                fprintf(output, "M%d %dh1v1h-1z", x + 4, y + 4);
            }
        }
    }
    fprintf(output, "\"/>\n</svg>\n");
    CHECK(fclose(output) == 0);
    printf("Wrote %s\n", path);
}

static void check_failed_decode_diagnostics(void)
{
    static const uint8_t message[] = "CamWallet:hello-12345";
    source_encode(message, sizeof(message) - 1, 0);
    source_render(0, 0);
    const unsigned modules = (unsigned)qrcodegen_getSize(source_symbol);
    const unsigned scale = WIDTH / (modules + 8u);
    const unsigned left = (WIDTH - modules * scale) / 2u;
    const unsigned top = (HEIGHT - modules * scale) / 2u;
    /* Damage payload cells but preserve finder/timing/alignment patterns.
     * The scanner should find a QR and expose the ECC error, not return NONE.
     */
    for (unsigned y = 9; y < modules; ++y) {
        for (unsigned x = 9; x < modules; ++x) {
            if ((x + y) % 2u != 0
                || (x >= modules - 9u && x <= modules - 5u
                    && y >= modules - 9u && y <= modules - 5u)) {
                continue;
            }
            for (unsigned dy = 0; dy < scale; ++dy) {
                for (unsigned dx = 0; dx < scale; ++dx) {
                    const size_t offset = ((top + y * scale + dy) * WIDTH
                                           + left + x * scale + dx) * 2u;
                    source_frame[offset] ^= 0xff;
                    source_frame[offset + 1u] ^= 0xff;
                }
            }
        }
    }
    qr_payload_t payload;
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES, &payload) == QR_SCAN_DECODE_FAILED);
    qr_scan_info_t info = qr_last_scan_info();
    CHECK(info.candidates > 0);
    CHECK(info.decoded == 0);
    CHECK(info.decode_error != NULL);
    CHECK(info.mirrored_error != NULL);
    CHECK(payload.length == 0);
    memset(source_frame, 0xff, sizeof(source_frame));
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES, &payload) == QR_SCAN_NONE);
    info = qr_last_scan_info();
    CHECK(info.candidates == 0 && info.decoded == 0);
    CHECK(info.decode_error == NULL && info.mirrored_error == NULL);
    CHECK(info.gray_min == 255 && info.gray_max == 255);
    puts("PASS damaged QR diagnostics and next-frame reset");
    ++test_count;
}

int main(int argc, char **argv)
{
    CHECK(!qr_scanner_init(0, HEIGHT));
    CHECK(qr_scanner_init(WIDTH, HEIGHT));
    CHECK(qr_scanner_init(WIDTH, HEIGHT));
    CHECK(!qr_scanner_init(120, 160));

    qr_payload_t payload;
    memset(&payload, 0xa5, sizeof(payload));
    CHECK(qr_scan_rgb565_be(NULL, FRAME_BYTES, &payload) == QR_SCAN_BAD_FRAME);
    CHECK(payload.length == 0);
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES - 1, &payload) == QR_SCAN_BAD_FRAME);
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES, NULL) == QR_SCAN_BAD_FRAME);
    memset(source_frame, 0xff, sizeof(source_frame));
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES, &payload) == QR_SCAN_NONE);
    memset(source_frame, 0, sizeof(source_frame));
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES, &payload) == QR_SCAN_NONE);
    puts("PASS blank frames and invalid scanner arguments");
    ++test_count;

    static const uint8_t hello[] = "CamWallet:hello-12345";
    roundtrip("ASCII", hello, sizeof(hello) - 1, 0, 0, 0);
    roundtrip("rotated source", hello, sizeof(hello) - 1, 0, 1, 0);
    roundtrip("mirrored source", hello, sizeof(hello) - 1, 0, 2, 0);
    roundtrip("colored RGB565 source", hello, sizeof(hello) - 1, 0, 0, 0xf800);
    static const uint8_t binary[] = {0, 0xff, 0x80, 0x1b, '\n', '\r', '"', '\\', 'x', 0};
    roundtrip("binary including NUL", binary, sizeof(binary), 0, 0, 0);
    static const uint8_t utf8[] = {0xe4, 0xbd, 0xa0, 0xe5, 0xa5, 0xbd, '-', 'Q', 'R'};
    roundtrip("UTF-8", utf8, sizeof(utf8), 26, 0, 0);
    static const uint8_t latin1[] = {'c', 'a', 'f', 0xe9};
    roundtrip("Latin-1", latin1, sizeof(latin1), 3, 0, 0);
    uint8_t limit[QR_PAYLOAD_MAX_BYTES + 1];
    for (size_t i = 0; i < sizeof(limit); ++i) {
        limit[i] = (uint8_t)('!' + i % 90);
    }
    roundtrip("128-byte limit", limit, QR_PAYLOAD_MAX_BYTES, 0, 0, 0);
    roundtrip("128-byte UTF-8 ECI", limit, QR_PAYLOAD_MAX_BYTES, 26, 0, 0);
    rejection("129-byte payload", limit, sizeof(limit), 0, QR_SCAN_TOO_LONG);
    rejection("empty payload", hello, 0, 0, QR_SCAN_EMPTY);
    rejection("unsupported ECI", hello, sizeof(hello) - 1, 20, QR_SCAN_UNSUPPORTED);
    check_failed_decode_diagnostics();

    qr_symbol_t symbol = {0};
    CHECK(!qr_render_rgb565_be(&symbol, source_frame, FRAME_BYTES, WIDTH, HEIGHT));
    payload.length = 0;
    CHECK(!qr_encode_payload(&payload, &symbol));
    payload.length = QR_PAYLOAD_MAX_BYTES + 1;
    CHECK(!qr_encode_payload(&payload, &symbol));
    payload.length = 1;
    payload.eci = 20;
    CHECK(!qr_encode_payload(&payload, &symbol));
    payload.eci = 0;
    payload.bytes[0] = 'x';
    CHECK(qr_encode_payload(&payload, &symbol));
    CHECK(!qr_render_rgb565_be(&symbol, source_frame, FRAME_BYTES - 1, WIDTH, HEIGHT));
    CHECK(!qr_render_rgb565_be(&symbol, source_frame, 100u * 100u * 2u, 100, 100));
    CHECK(!qr_render_rgb565_be(&symbol, NULL, FRAME_BYTES, WIDTH, HEIGHT));
    puts("PASS encoder bounds and renderer bounds");
    ++test_count;

    if (argc == 3 && strcmp(argv[1], "--write-fixture") == 0) {
        write_fixture(argv[2]);
    } else {
        CHECK(argc == 1);
    }
    qr_scanner_destroy();
    CHECK(qr_scan_rgb565_be(source_frame, FRAME_BYTES, &payload) == QR_SCAN_BAD_FRAME);
    printf("All %u QR integration cases passed.\n", test_count);
    return 0;
}
