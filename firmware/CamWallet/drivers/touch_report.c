#include "touch.h"

bool touch_decode(const uint8_t r[5], uint8_t *fingers, uint16_t *x, uint16_t *y)
{
    /* Registers 0x02..0x06. Raw portrait coordinates match MADCTL=0x48.
     * The vendor's landscape example uses (raw_y, 240-raw_x) for 0x28;
     * do not apply that landscape rotation to our 240x320 portrait frame. */
    if (r[0] > 1) return false;
    *fingers = r[0];
    if (!*fingers) return true; /* Release coordinates may be stale. */
    *x = (uint16_t)(((r[1] & 15u) << 8) | r[2]);
    *y = (uint16_t)(((r[3] & 15u) << 8) | r[4]);
    return *x < 240 && *y < 320;
}
