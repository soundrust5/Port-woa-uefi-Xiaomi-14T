#ifndef DIAGNOSTIC_READBACK_H
#define DIAGNOSTIC_READBACK_H

#include "framebuffer.h"

#include <stdbool.h>

/* Verify the boot marker at the surface origin without changing the surface. */
bool marker_verify(const FbSurface *surface, const unsigned char *build_id);

/* Draw and verify the marker on the confirmed stock LK framebuffer address. */
unsigned diagnostic_marker_readback(const unsigned char *build_id);

#endif
