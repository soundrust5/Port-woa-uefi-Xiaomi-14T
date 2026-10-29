#include "readback.h"

#include "degas_display.h"
#include "marker.h"

#include <stddef.h>
#include <stdint.h>

enum {
    FB_BYTES_PER_PIXEL = 4,
    MARKER_WIDTH = 640,
    MARKER_BAND_HEIGHT = 64,
    MARKER_HEIGHT = MARKER_BAND_HEIGHT * 3,
    MARKER_BIT_WIDTH = 20,
    MARKER_BUILD_ID_BYTES = 4,
    MARKER_BITS_PER_BYTE = 8
};

#define MARKER_MAGENTA UINT32_C(0xffff00ff)
#define MARKER_CYAN UINT32_C(0xff00ffff)

static bool add_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (right > UINT64_MAX - left) {
        return false;
    }

    *result = left + right;
    return true;
}

static bool mul_u64(uint64_t left, uint64_t right, uint64_t *result)
{
    if (left != 0U && right > UINT64_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool validate_marker_surface(const FbSurface *surface)
{
    uint64_t row_bytes;
    uint64_t last_row_offset;
    uint64_t visible_end;
    uint64_t marker_last_row;
    uint64_t marker_row_bytes;
    uint64_t marker_end;
    uintptr_t base_address;

    if (surface == NULL || surface->base == NULL ||
        surface->width < MARKER_WIDTH || surface->height < MARKER_HEIGHT ||
        surface->pitch == 0U || surface->size == 0U) {
        return false;
    }

    base_address = (uintptr_t)surface->base;
    if ((base_address & (FB_BYTES_PER_PIXEL - 1U)) != 0U ||
        (surface->pitch & (FB_BYTES_PER_PIXEL - 1U)) != 0U ||
        surface->size > (uint64_t)UINTPTR_MAX - (uint64_t)base_address) {
        return false;
    }

    if (!mul_u64(surface->width, FB_BYTES_PER_PIXEL, &row_bytes) ||
        row_bytes > surface->pitch ||
        !mul_u64((uint64_t)surface->height - 1U,
                 surface->pitch,
                 &last_row_offset) ||
        !add_u64(last_row_offset, row_bytes, &visible_end) ||
        visible_end > surface->size) {
        return false;
    }

    if (!mul_u64((uint64_t)MARKER_HEIGHT - 1U,
                 surface->pitch,
                 &marker_last_row) ||
        !mul_u64(MARKER_WIDTH, FB_BYTES_PER_PIXEL, &marker_row_bytes) ||
        !add_u64(marker_last_row, marker_row_bytes, &marker_end) ||
        marker_end > surface->size) {
        return false;
    }

    return true;
}

static uint32_t expected_color(uint32_t x,
                               uint32_t y,
                               const unsigned char *build_id)
{
    if (y < MARKER_BAND_HEIGHT) {
        return MARKER_MAGENTA;
    }
    if (y < MARKER_BAND_HEIGHT * 2U) {
        return MARKER_CYAN;
    }

    {
        uint32_t bit_index = x / MARKER_BIT_WIDTH;
        uint32_t byte_index = bit_index / MARKER_BITS_PER_BYTE;
        uint32_t bit_in_byte = bit_index % MARKER_BITS_PER_BYTE;
        uint32_t mask = UINT32_C(1) <<
                        (MARKER_BITS_PER_BYTE - 1U - bit_in_byte);

        return ((uint32_t)build_id[byte_index] & mask) != 0U
                   ? MARKER_MAGENTA
                   : MARKER_CYAN;
    }
}

bool marker_verify(const FbSurface *surface, const unsigned char *build_id)
{
    uint32_t y;

    if (build_id == NULL || !validate_marker_surface(surface)) {
        return false;
    }

    for (y = 0U; y < MARKER_HEIGHT; ++y) {
        const volatile uint8_t *pixel =
            surface->base + (size_t)((uint64_t)y * surface->pitch);
        uint32_t x;

        for (x = 0U; x < MARKER_WIDTH; ++x) {
            uint32_t color = expected_color(x, y, build_id);

            if (pixel[0] != (uint8_t)color ||
                pixel[1] != (uint8_t)(color >> 8U) ||
                pixel[2] != (uint8_t)(color >> 16U) ||
                pixel[3] != (uint8_t)(color >> 24U)) {
                return false;
            }
            pixel += FB_BYTES_PER_PIXEL;
        }
    }

    return true;
}

unsigned diagnostic_marker_readback(const unsigned char *build_id)
{
    FbSurface surface = degas_fb_surface(
        (volatile uint8_t *)(uintptr_t)DEGAS_FB_PHYSICAL_BASE);

    marker_draw(&surface, build_id);
#if defined(__aarch64__)
    __asm__ volatile ("dsb sy" ::: "memory");
#endif
    return marker_verify(&surface, build_id) ? 0U : 1U;
}
