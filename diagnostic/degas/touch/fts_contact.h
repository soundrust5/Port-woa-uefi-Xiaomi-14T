/* Record decoder only: not a packet dispatcher. Reconstructed from the exact
 * phone module fts_irq_handler 0x8ba8..0x8c58 (8-byte contact branch).
 * Caller must establish packet type and validate SPI CRC before calling.
 * Android uses (fixed_point_coordinate * super_resolution) >> 4.
 * The observed board super_resolution is 100. Hardware replay pending. */
#ifndef DEGAS_FTS_CONTACT_H
#define DEGAS_FTS_CONTACT_H
#ifndef DEGAS_TOUCH_UEFI_TYPES
#include <stddef.h>
#include <stdint.h>
#endif
typedef struct {
 uint32_t x, y;
 uint8_t id, event, major, minor;
} DEGAS_FTS_CONTACT;
static inline int DegasFtsContact8(const uint8_t *p, size_t size,
 uint32_t scale, uint32_t xmax, uint32_t ymax, DEGAS_FTS_CONTACT *out) {
 DEGAS_FTS_CONTACT c;
 uint32_t x, y;
 if (!p || !out || size < 8 || !scale || scale > 100) return 0;
 c.id = p[2] >> 4; c.event = p[0] >> 6;
 if (c.id >= 10 || c.event == 3) return 0;
 x = ((uint32_t)(p[0] & 15) << 12) | ((uint32_t)p[1] << 4) | (p[4] >> 4);
 y = ((uint32_t)(p[2] & 15) << 12) | ((uint32_t)p[3] << 4) | (p[4] & 15);
 c.x = (x * scale) >> 4; c.y = (y * scale) >> 4;
 if (c.x > xmax || c.y > ymax) return 0;
 c.major = p[5] ? p[5] : 9;
 c.minor = p[6] ? p[6] : 9;
 *out = c;
 return 1;
}
typedef struct {
 uint8_t count;
 DEGAS_FTS_CONTACT contacts[10];
} DEGAS_FTS_REPORT;
/* IRQ 0x861c/0x8648 selects type2 from high nibble of payload[1].
 * 0x87a0 requires count1..max; records begin at4, stride8 (0x8ba8).
 * Reject unknown packet types rather than treating them as finger releases.
 * An UP record (event1) remains in output for the future HID state machine. */
static inline int DegasFtsReport2(const uint8_t *p, size_t size,
 DEGAS_FTS_REPORT *out) {
 DEGAS_FTS_REPORT r = {0};
 uint16_t ids = 0;
 size_t i;
 if (!p || !out || size < 4 || (p[1] >> 4) != 2) return 0;
 r.count = p[1] & 15;
 if (!r.count || r.count > 10 || size < 4u + 8u * r.count) return 0;
 for (i = 0; i < r.count; ++i) {
  uint16_t bit;
  if (!DegasFtsContact8(p + 4 + 8*i, 8, 100, 121999, 271199, &r.contacts[i])) return 0;
  bit = (uint16_t)(1u << r.contacts[i].id);
  if (ids & bit) return 0;
  ids |= bit;
 }
 *out = r;
 return 1;
}
#endif
