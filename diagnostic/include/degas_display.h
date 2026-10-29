#ifndef DEGAS_DISPLAY_H
#define DEGAS_DISPLAY_H

#include "framebuffer.h"

/* Verified stock LK contract: docs/FRAMEBUFFER-CONTRACT.md.
 * A mapped pointer must be supplied by an owner of the display surface.
 * These constants do not establish mapping, cache policy or DSI ownership.
 */
#define DEGAS_FB_PHYSICAL_BASE UINT64_C(0xfcc2f000)
#define DEGAS_FB_RESERVATION_SIZE UINT64_C(0x033d0000)
#define DEGAS_FB_FRAME_SIZE UINT64_C(0x00cf3000)
#define DEGAS_FB_WIDTH UINT32_C(1220)
#define DEGAS_FB_HEIGHT UINT32_C(2712)
#define DEGAS_FB_PITCH UINT32_C(4992)

static inline FbSurface degas_fb_surface(volatile uint8_t *mapped_base)
{
    FbSurface surface = {mapped_base, DEGAS_FB_FRAME_SIZE,
                         DEGAS_FB_WIDTH, DEGAS_FB_HEIGHT, DEGAS_FB_PITCH};
    return surface;
}

#endif
