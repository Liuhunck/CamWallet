#include "scan_preview.h"

#include <string.h>

#include "drivers/lcd.h"
#include "fonts.h"

#define BAR_HEIGHT 24u
#define TEXT_X 10u
#define TEXT_Y 4u

static void compose_bar(uint8_t *frame, unsigned top, const char *text)
{
    memset(frame + (size_t)top * LCD_WIDTH * 2u, 0,
           BAR_HEIGHT * LCD_WIDTH * 2u);

    const unsigned stride = (Font16.Width + 7u) / 8u;
    unsigned x = TEXT_X;
    while (*text != '\0' && Font16.Width <= LCD_WIDTH - x) {
        unsigned char character = (unsigned char)*text++;
        if (character < ' ' || character > '~') {
            character = '?';
        }
        const uint8_t *glyph = Font16.table
            + (size_t)(character - ' ') * Font16.Height * stride;
        for (unsigned y = 0; y < Font16.Height; ++y) {
            for (unsigned column = 0; column < Font16.Width; ++column) {
                if ((glyph[y * stride + column / 8u]
                     & (0x80u >> (column % 8u))) != 0) {
                    const size_t offset =
                        ((size_t)(top + TEXT_Y + y) * LCD_WIDTH + x + column) * 2u;
                    frame[offset] = 0xff;
                    frame[offset + 1u] = 0xff;
                }
            }
        }
        x += Font16.Width;
    }
}

bool scan_preview_compose(uint8_t *frame, size_t size,
                          const char *status, const char *counts)
{
    if (frame == NULL || size != LCD_WIDTH * LCD_HEIGHT * 2u
        || status == NULL || counts == NULL
        || Font16.Width == 0 || Font16.Height > BAR_HEIGHT - TEXT_Y) {
        return false;
    }
    compose_bar(frame, 0, status);
    compose_bar(frame, LCD_HEIGHT - BAR_HEIGHT, counts);
    return true;
}
