#ifndef DIAGNOSTIC_FRAMEBUFFER_H
#define DIAGNOSTIC_FRAMEBUFFER_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    volatile uint8_t *base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
} FbSurface;

/*
 * Fill a rectangle on a 32-bpp little-endian BGRA surface.
 *
 * The color is supplied as 0xAARRGGBB. The function performs all descriptor,
 * bounds, and overflow checks before writing and returns false without changing
 * memory when any check fails.
 */
bool fb_fill_rect(const FbSurface *surface,
                  uint32_t x,
                  uint32_t y,
                  uint32_t width,
                  uint32_t height,
                  uint32_t argb);

#endif
