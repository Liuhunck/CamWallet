#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ui/evm_review.h"
#include "drivers/touch.h"
#include "drivers/lcd.h"

static uint64_t tick;
static void idle(evm_review_t *s)
{
    evm_review_touch(s, true, 0, 0, 0, tick);
    tick += 80; evm_review_touch(s, true, 0, 0, 0, tick);
}
static bool tap(evm_review_t *s, unsigned x, unsigned y)
{
    tick += 20; assert(!evm_review_touch(s, true, 1, x, y, tick));
    tick += 80; assert(!evm_review_touch(s, true, 1, x, y, tick));
    tick += 20; assert(!evm_review_touch(s, true, 0, x, y, tick));
    tick += 80; return evm_review_touch(s, true, 0, x, y, tick);
}
static void test_manual_flow(void)
{
    evm_review_t s; evm_review_init(&s); tick = 0;
    /* Finger already held when the request arrives cannot advance pages. */
    for (tick = 0; tick < 1000; tick += 20)
        assert(!evm_review_touch(&s, true, 1, 180, 290, tick));
    assert(s.page == 0 && s.decision == EVM_PENDING);
    idle(&s);
    assert(!tap(&s, 120, 290)); /* Gap, not a button. */
    assert(tap(&s, 180, 290)); assert(s.page == 1);
    assert(tap(&s, 180, 20)); assert(s.page == 0); /* Back. */
    for (unsigned i = 1; i < REVIEW_PAGE_COUNT; ++i) {
        assert(tap(&s, 180, 290)); assert(s.page == i && s.decision == EVM_PENDING);
    }
    tick += 600000; idle(&s); /* Waiting never approves or times out into approval. */
    assert(s.decision == EVM_PENDING);
    assert(tap(&s, 180, 290)); assert(s.decision == EVM_CONFIRMED);
    assert(!tap(&s, 40, 290)); assert(s.decision == EVM_CONFIRMED); /* Terminal. */
    for (unsigned page = 0; page < REVIEW_PAGE_COUNT; ++page) {
        evm_review_init(&s); idle(&s);
        for (unsigned i = 0; i < page; ++i) assert(tap(&s, 180, 290));
        assert(tap(&s, 40, 290)); assert(s.decision == EVM_REJECTED);
    }
}
static void test_invalid_gestures(void)
{
    for (unsigned variant = 0; variant < 6; ++variant) {
        evm_review_t s; evm_review_init(&s); idle(&s);
        evm_review_touch(&s, true, 1, 180, 290, tick += 20);
        switch (variant) {
        case 0: evm_review_touch(&s, false, 0, 0, 0, tick += 80); break; /* I2C error */
        case 1: evm_review_touch(&s, true, 2, 180, 290, tick += 80); break;
        case 2: evm_review_touch(&s, true, 1, 10, 100, tick += 80); break; /* Drag */
        case 3: evm_review_touch(&s, true, 1, 180, 290, tick += 2100); break;
        case 4: break; /* Too short. */
        case 5: evm_review_touch(&s, true, 1, 240, 290, tick += 80); break;
        }
        evm_review_touch(&s, true, 0, 0, 0, tick += 10);
        idle(&s); idle(&s);
        assert(s.page == 0 && s.decision == EVM_PENDING);
        assert(tap(&s, 180, 290)); assert(s.page == 1); /* Recover on fresh tap. */
    }
    evm_review_t s; evm_review_init(&s); idle(&s);
    evm_review_touch(&s, true, 1, 180, 290, tick += 20);
    evm_review_touch(&s, true, 0, 180, 290, tick += 80);
    evm_review_touch(&s, true, 1, 180, 290, tick += 20); /* Bouncing release. */
    idle(&s); idle(&s); assert(s.page == 0);
}
static void test_reports(void)
{
    uint8_t fingers = 0; uint16_t x = 0, y = 0;
    uint8_t r[] = {1, 0x80, 239, 1, 63}; /* Contact flag masked; bottom right. */
    assert(touch_decode(r, &fingers, &x, &y) && fingers == 1 && x == 239 && y == 319);
    r[2] = 240; assert(!touch_decode(r, &fingers, &x, &y));
    r[2] = 0; r[4] = 64; assert(!touch_decode(r, &fingers, &x, &y));
    r[0] = 2; assert(!touch_decode(r, &fingers, &x, &y));
    r[0] = 0; assert(touch_decode(r, &fingers, &x, &y) && fingers == 0);
}
static void test_render(const char *prefix)
{
    static uint8_t guarded[LCD_WIDTH * LCD_HEIGHT * 2 + 2];
    uint8_t *frame = guarded + 1;
    evm_review_t s; evm_review_init(&s);
    evm_request_t r = {.chain_id = 1, .gas_limit = UINT32_MAX,
        .nonce = UINT64_MAX, .max_fee_per_gas = UINT64_MAX,
        .priority_fee_per_gas = UINT64_MAX, .has_address = true};
    memset(r.value, 255, sizeof(r.value)); memset(r.to, 255, sizeof(r.to));
    memset(r.address, 0x22, sizeof(r.address));
    for (s.page = 0; s.page < REVIEW_PAGE_COUNT; ++s.page) {
        memset(guarded, 0xa5, sizeof(guarded));
        assert(evm_review_render(frame, sizeof(guarded) - 2, &r, &s));
        assert(guarded[0] == 0xa5 && guarded[sizeof(guarded) - 1] == 0xa5);
        if (prefix) {
            char path[512]; snprintf(path, sizeof(path), "%s-%u.ppm", prefix, s.page + 1);
            FILE *f = fopen(path, "wb"); assert(f);
            fprintf(f, "P6\n240 320\n255\n");
            for (unsigned i = 0; i < LCD_WIDTH * LCD_HEIGHT; ++i) {
                unsigned p = ((unsigned)frame[2*i] << 8) | frame[2*i + 1];
                unsigned char rgb[] = {(unsigned char)(((p >> 11) & 31) * 255 / 31),
                    (unsigned char)(((p >> 5) & 63) * 255 / 63), (unsigned char)((p & 31) * 255 / 31)};
                assert(fwrite(rgb, 1, 3, f) == 3);
            }
            assert(fclose(f) == 0);
        }
        char rows[REVIEW_ROWS][REVIEW_COLUMNS + 1];
        evm_review_lines(&r, s.page, rows);
        for (unsigned row = 0; row < REVIEW_ROWS; ++row) assert(strlen(rows[row]) <= 20);
    }
    assert(!evm_review_render(frame, sizeof(guarded) - 2, &r, &s)); /* Invalid page. */
    s.page = 2;
    char rows[REVIEW_ROWS][REVIEW_COLUMNS + 1], joined[100] = "";
    evm_review_lines(&r, 2, rows);
    for (unsigned i = 1; i <= 4; ++i) strcat(joined, rows[i]);
    assert(!strcmp(joined, "115792089237316195423570985008687907853269984665640564039457584007913129639935"));
    assert(!evm_review_render(frame, 12, &r, &s));
    /* No decision, no receipt; invalid requests cannot be acknowledged. */
    uint8_t out[57]; memset(out, 0xa5, sizeof(out));
    const uint8_t bad[] = {0xa0};
    assert(!evm_decision_response(bad, sizeof(bad), EVM_PENDING, out, sizeof(out)));
    assert(!evm_decision_response(bad, sizeof(bad), EVM_CONFIRMED, out, sizeof(out)));
    for (unsigned i = 0; i < sizeof(out); ++i) assert(out[i] == 0xa5);
}
int main(int argc, char **argv)
{
    test_manual_flow(); test_invalid_gestures(); test_reports(); test_render(argc == 2 ? argv[1] : NULL);
    puts("touch reports, debounced manual review, complete rendering and response gating passed");
    return 0;
}
