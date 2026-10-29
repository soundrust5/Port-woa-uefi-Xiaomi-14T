#include "framebuffer.h"

#include <stddef.h>
#include <stdint.h>

enum { FB_BYTES_PER_PIXEL = 4 };

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

static bool validate_surface(const FbSurface *surface)
{
    uint64_t row_bytes;
    uint64_t last_row_offset;
    uint64_t visible_end;
    uintptr_t base_address;

    if (surface == NULL || surface->base == NULL || surface->width == 0U ||
        surface->height == 0U || surface->pitch == 0U || surface->size == 0U) {
        return false;
    }

    base_address = (uintptr_t)surface->base;
    if ((base_address & (FB_BYTES_PER_PIXEL - 1U)) != 0U ||
        (surface->pitch & (FB_BYTES_PER_PIXEL - 1U)) != 0U) {
        return false;
    }

    if (surface->size > (uint64_t)UINTPTR_MAX - (uint64_t)base_address) {
        return false;
    }

    if (!mul_u64(surface->width, FB_BYTES_PER_PIXEL, &row_bytes) ||
        row_bytes > surface->pitch) {
        return false;
    }

    if (!mul_u64((uint64_t)surface->height - 1U,
                 surface->pitch,
                 &last_row_offset) ||
        !add_u64(last_row_offset, row_bytes, &visible_end)) {
        return false;
    }

    return visible_end <= surface->size;
}

bool fb_fill_rect(const FbSurface *surface,
                  uint32_t x,
                  uint32_t y,
                  uint32_t width,
                  uint32_t height,
                  uint32_t argb)
{
    uint64_t last_rect_row;
    uint64_t rect_right_bytes;
    uint64_t rect_end;
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
    uint32_t row;

    if (!validate_surface(surface) || width == 0U || height == 0U ||
        x >= surface->width || y >= surface->height ||
        width > surface->width - x || height > surface->height - y) {
        return false;
    }

    if (!mul_u64((uint64_t)y + height - 1U,
                 surface->pitch,
                 &last_rect_row) ||
        !mul_u64((uint64_t)x + width,
                 FB_BYTES_PER_PIXEL,
                 &rect_right_bytes) ||
        !add_u64(last_rect_row, rect_right_bytes, &rect_end) ||
        rect_end > surface->size) {
        return false;
    }

    blue = (uint8_t)argb;
    green = (uint8_t)(argb >> 8U);
    red = (uint8_t)(argb >> 16U);
    alpha = (uint8_t)(argb >> 24U);

    for (row = 0U; row < height; ++row) {
        uint64_t row_offset = ((uint64_t)y + row) * surface->pitch;
        uint64_t pixel_offset = row_offset + (uint64_t)x * FB_BYTES_PER_PIXEL;
        volatile uint8_t *pixel = surface->base + (size_t)pixel_offset;
        uint32_t column;

        for (column = 0U; column < width; ++column) {
            pixel[0] = blue;
            pixel[1] = green;
            pixel[2] = red;
            pixel[3] = alpha;
            pixel += FB_BYTES_PER_PIXEL;
        }
    }

    return true;
}
