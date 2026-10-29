#include "marker.h"

#include "degas_display.h"

#include <stddef.h>
#include <stdint.h>

enum {
    MARKER_WIDTH = 640,
    MARKER_BAND_HEIGHT = 64,
    MARKER_BIT_WIDTH = 20,
    MARKER_BUILD_ID_BYTES = 4,
    MARKER_BITS_PER_BYTE = 8
};

#define MARKER_MAGENTA UINT32_C(0xffff00ff)
#define MARKER_CYAN UINT32_C(0xff00ffff)

void marker_draw(FbSurface *surface, const unsigned char *build_id)
{
    uint32_t bit_index;

    if (surface == NULL || build_id == NULL) {
        return;
    }

    (void)fb_fill_rect(surface, 0U, 0U,
                       MARKER_WIDTH, MARKER_BAND_HEIGHT, MARKER_MAGENTA);
    (void)fb_fill_rect(surface, 0U, MARKER_BAND_HEIGHT,
                       MARKER_WIDTH, MARKER_BAND_HEIGHT, MARKER_CYAN);

    for (bit_index = 0U;
         bit_index < MARKER_BUILD_ID_BYTES * MARKER_BITS_PER_BYTE;
         ++bit_index) {
        uint32_t byte_index = bit_index / MARKER_BITS_PER_BYTE;
        uint32_t bit_in_byte = bit_index % MARKER_BITS_PER_BYTE;
        uint32_t mask = UINT32_C(1) <<
                        (MARKER_BITS_PER_BYTE - 1U - bit_in_byte);
        uint32_t color = ((uint32_t)build_id[byte_index] & mask) != 0U
                             ? MARKER_MAGENTA
                             : MARKER_CYAN;

        (void)fb_fill_rect(surface,
                           bit_index * MARKER_BIT_WIDTH,
                           MARKER_BAND_HEIGHT * 2U,
                           MARKER_BIT_WIDTH,
                           MARKER_BAND_HEIGHT,
                           color);
    }
}

void diagnostic_marker(const unsigned char *build_id)
{
    FbSurface surface = degas_fb_surface(
        (volatile uint8_t *)(uintptr_t)DEGAS_FB_PHYSICAL_BASE);

    marker_draw(&surface, build_id);
}
