#ifndef DIAGNOSTIC_MARKER_H
#define DIAGNOSTIC_MARKER_H

#include "framebuffer.h"

/*
 * Draw a bounded boot marker at the surface origin. Build-ID bits are drawn
 * most-significant-first, with magenta for one and cyan for zero.
 */
void marker_draw(FbSurface *surface, const unsigned char *build_id);

/* Draw the marker on the verified stock LK framebuffer physical address. */
void diagnostic_marker(const unsigned char *build_id);

#endif
