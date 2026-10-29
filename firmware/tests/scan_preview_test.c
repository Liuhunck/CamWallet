#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "drivers/lcd.h"
#include "ui/scan_preview.h"

#define FRAME_BYTES (LCD_WIDTH * LCD_HEIGHT * 2u)
#define BAR_BYTES (24u * LCD_WIDTH * 2u)
#define GUARD_BYTES 32u
#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static uint8_t guarded[GUARD_BYTES + FRAME_BYTES + GUARD_BYTES];
static uint8_t reference[FRAME_BYTES];

static void check_guards_and_camera(uint8_t value)
{
    for (size_t i = 0; i < GUARD_BYTES; ++i) {
        CHECK(guarded[i] == 0xa5);
        CHECK(guarded[GUARD_BYTES + FRAME_BYTES + i] == 0xa5);
    }
    for (size_t i = BAR_BYTES; i < FRAME_BYTES - BAR_BYTES; ++i) {
        CHECK(guarded[GUARD_BYTES + i] == value);
    }
}

int main(void)
{
    uint8_t *frame = guarded + GUARD_BYTES;
    memset(guarded, 0xa5, sizeof(guarded));
    memset(frame, 0x12, FRAME_BYTES);
    CHECK(scan_preview_compose(frame, FRAME_BYTES, "NO QR FOUND", "FOUND 0 READ 0"));
    check_guards_and_camera(0x12);
    memcpy(reference, frame, FRAME_BYTES);

    /* Different camera pixels must not leak through either status bar. */
    memset(frame, 0x34, FRAME_BYTES);
    CHECK(scan_preview_compose(frame, FRAME_BYTES, "NO QR FOUND", "FOUND 0 READ 0"));
    CHECK(memcmp(frame, reference, BAR_BYTES) == 0);
    CHECK(memcmp(frame + FRAME_BYTES - BAR_BYTES,
                 reference + FRAME_BYTES - BAR_BYTES, BAR_BYTES) == 0);
    CHECK(memchr(frame, 0xff, BAR_BYTES) != NULL); /* Text is visible. */
    CHECK(frame[0] == 0 && frame[1] == 0); /* Margin is black. */
    check_guards_and_camera(0x34);

    /* Long labels clip at the right edge; replacing them clears old glyphs. */
    char long_label[128];
    memset(long_label, 'W', sizeof(long_label) - 1u);
    long_label[sizeof(long_label) - 1u] = '\0';
    CHECK(scan_preview_compose(frame, FRAME_BYTES, long_label, long_label));
    check_guards_and_camera(0x34);
    CHECK(scan_preview_compose(frame, FRAME_BYTES, "A", ""));
    for (unsigned y = 0; y < 24u; ++y) {
        for (unsigned x = 21u; x < LCD_WIDTH; ++x) {
            const size_t offset = ((size_t)y * LCD_WIDTH + x) * 2u;
            CHECK(frame[offset] == 0 && frame[offset + 1u] == 0);
        }
    }
    for (size_t i = FRAME_BYTES - BAR_BYTES; i < FRAME_BYTES; ++i) {
        CHECK(frame[i] == 0);
    }
    CHECK(scan_preview_compose(frame, FRAME_BYTES, "?", "?"));
    memcpy(reference, frame, FRAME_BYTES);
    CHECK(scan_preview_compose(frame, FRAME_BYTES, "\x01", "\xff"));
    CHECK(memcmp(frame, reference, FRAME_BYTES) == 0);

    /* Bad arguments must leave the entire supplied buffer untouched. */
    CHECK(!scan_preview_compose(NULL, FRAME_BYTES, "x", "y"));
    CHECK(!scan_preview_compose(frame, FRAME_BYTES - 1u, "x", "y"));
    CHECK(!scan_preview_compose(frame, FRAME_BYTES + 1u, "x", "y"));
    CHECK(!scan_preview_compose(frame, FRAME_BYTES, NULL, "y"));
    CHECK(!scan_preview_compose(frame, FRAME_BYTES, "x", NULL));
    CHECK(memcmp(frame, reference, FRAME_BYTES) == 0);
    check_guards_and_camera(0x34);
    puts("PASS: stable status bars, camera preservation, text clipping and buffer guards");
    return EXIT_SUCCESS;
}
