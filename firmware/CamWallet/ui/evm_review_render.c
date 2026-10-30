#include "evm_review.h"
#include "drivers/lcd.h"
#include "fonts.h"
#include <stdio.h>
#include <string.h>

static void rect(uint8_t *frame, unsigned x, unsigned y, unsigned w, unsigned h, uint16_t color)
{
    for (unsigned row = y; row < y + h; ++row)
        for (unsigned col = x; col < x + w; ++col) {
            size_t offset = (row * LCD_WIDTH + col) * 2u;
            frame[offset] = color >> 8; frame[offset + 1] = color & 255;
        }
}
static void text(uint8_t *frame, unsigned x, unsigned y, const char *s)
{
    unsigned stride = (Font16.Width + 7u) / 8u;
    while (*s && x + Font16.Width <= LCD_WIDTH && y + Font16.Height <= LCD_HEIGHT) {
        unsigned char c = (unsigned char)*s++;
        if (c < ' ' || c > '~') c = '?';
        const uint8_t *glyph = Font16.table + (c - ' ') * Font16.Height * stride;
        for (unsigned row = 0; row < Font16.Height; ++row)
            for (unsigned col = 0; col < Font16.Width; ++col)
                if (glyph[row * stride + col / 8] & (0x80u >> (col % 8)))
                    rect(frame, x + col, y + row, 1, 1, LCD_WHITE);
        x += Font16.Width;
    }
}
bool evm_review_render(uint8_t *frame, size_t size, const evm_request_t *request,
                       const evm_review_t *review)
{
    if (!frame || !request || !review || size != LCD_WIDTH * LCD_HEIGHT * 2u
        || review->page >= REVIEW_PAGE_COUNT) return false;
    /* Reuse the drained camera buffer. One LCD transfer only when page changes. */
    memset(frame, 0, size);
    char title[21], rows[REVIEW_ROWS][REVIEW_COLUMNS + 1];
    snprintf(title, sizeof(title), "REVIEW %u/%u", review->page + 1, REVIEW_PAGE_COUNT);
    text(frame, 10, 12, title);
    if (review->page) {
        rect(frame, 160, 4, 76, 32, 0x39e7); text(frame, 176, 12, "BACK");
    }
    evm_review_lines(request, review->page, rows);
    for (unsigned i = 0; i < REVIEW_ROWS; ++i) text(frame, 10, 48 + i * 18, rows[i]);
    rect(frame, 4, 270, 108, 46, 0x9800); text(frame, 25, 285, "REJECT");
    rect(frame, 128, 270, 108, 46, 0x0320);
    text(frame, review->page + 1 == REVIEW_PAGE_COUNT ? 143 : 160, 285,
         review->page + 1 == REVIEW_PAGE_COUNT ? "CONFIRM" : "NEXT");
    return true;
}
