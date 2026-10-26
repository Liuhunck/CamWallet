#include <stdio.h>
#include "pico/stdlib.h"

#include "drivers/lcd.h"

static void draw_status_screen(void)
{
    lcd_clear(LCD_BLACK);
    lcd_draw_text(21, 32, "CamWallet", 2, LCD_WHITE, LCD_BLACK);
    lcd_fill_rect(21, 78, 198, 2, LCD_WHITE);
    lcd_draw_text(21, 96, "LCD READY", 1, LCD_GREEN, LCD_BLACK);
    lcd_draw_text(21, 124, "RP2350 / 240x320", 1, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(21, 152, "USB LOG ACTIVE", 1, LCD_WHITE, LCD_BLACK);
    lcd_draw_text(21, 196, "UPTIME", 1, LCD_WHITE, LCD_BLACK);

    /* These bars also make swapped RGB channels easy to spot on hardware. */
    lcd_fill_rect(21, 268, 66, 16, LCD_RED);
    lcd_fill_rect(87, 268, 66, 16, LCD_GREEN);
    lcd_fill_rect(153, 268, 66, 16, LCD_BLUE);
    lcd_draw_text(21, 296, "DISPLAY TEST", 1, LCD_WHITE, LCD_BLACK);
}

int main(void)
{
    stdio_init_all();
    lcd_init();
    draw_status_screen();

    while (true) {
        const unsigned long seconds = (unsigned long)(time_us_64() / 1000000u);
        char uptime[24];
        snprintf(uptime, sizeof(uptime), "%lu seconds", seconds);

        lcd_fill_rect(21, 224, 198, 16, LCD_BLACK);
        lcd_draw_text(21, 224, uptime, 1, LCD_WHITE, LCD_BLACK);
        printf("[CamWallet] LCD update sent; uptime=%lu seconds\n", seconds);
        sleep_ms(1000);
    }
}
