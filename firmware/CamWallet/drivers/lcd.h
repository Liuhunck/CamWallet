#ifndef CAMWALLET_LCD_H
#define CAMWALLET_LCD_H

#include <stdint.h>

#define LCD_WIDTH 240u
#define LCD_HEIGHT 320u

#define LCD_BLACK 0x0000u
#define LCD_WHITE 0xffffu
#define LCD_RED 0xf800u
#define LCD_GREEN 0x07e0u
#define LCD_BLUE 0x001fu

/* Blocking display operations; call from the main loop only. */
void lcd_init(void);
void lcd_clear(uint16_t color);
void lcd_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height,
                   uint16_t color);

/* One ASCII line using the supplied 11x16 font. Scale must be 1..4. */
void lcd_draw_text(uint16_t x, uint16_t y, const char *text, uint8_t scale,
                   uint16_t foreground, uint16_t background);

#endif
