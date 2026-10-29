#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../degas/touch/fts_contact.h"
int main(void) {
 unsigned x, event, id;
 uint8_t p[8] = {0};
 DEGAS_FTS_CONTACT c, before;
 /* Exhaust all packed 16-bit coordinates, against an independent arithmetic
  * reference; include every contact ID and event used by the Android branch. */
 for (x = 0; x < 65536; ++x) {
  unsigned y = 65535 - x;
  p[0] = (uint8_t)(x >> 12); p[1] = (uint8_t)(x >> 4);
  p[2] = (uint8_t)(y >> 12); p[3] = (uint8_t)(y >> 4);
  p[4] = (uint8_t)(((x % 16) << 4) | (y % 16));
  assert(DegasFtsContact8(p, 8, 100, 409599, 409599, &c));
  assert(c.x == x * 100 / 16 && c.y == y * 100 / 16);
  assert(c.major == 9 && c.minor == 9);
 }
 memset(p, 0, sizeof(p));
 for (event = 0; event < 4; ++event) for (id = 0; id < 16; ++id) {
  p[0] = (uint8_t)(event << 6); p[2] = (uint8_t)(id << 4);
  memset(&c, 0x55, sizeof(c)); before = c;
  int ok = DegasFtsContact8(p, 8, 100, 121999, 271199, &c);
  assert(ok == (event < 3 && id < 10));
  if (ok) assert(c.event == event && c.id == id);
  else assert(memcmp(&c, &before, sizeof(c)) == 0);
 }
 memset(p, 0, sizeof(p));
 for (x = 0; x < 8; ++x) assert(!DegasFtsContact8(p, x, 100, 121999, 271199, &c));
 p[0] = 15; p[1] = 255;
 before = c;
 assert(!DegasFtsContact8(p, 8, 100, 121999, 271199, &c));
 assert(memcmp(&c, &before, sizeof(c)) == 0);
 assert(!DegasFtsContact8(p, 8, 101, 121999, 271199, &c));
 {
  uint8_t packet[84] = {0};
  DEGAS_FTS_REPORT report, saved;
  packet[1] = 0x2a;
  for (id = 0; id < 10; ++id) {
   packet[4+id*8] = (uint8_t)((id%3)<<6);
   packet[6+id*8] = (uint8_t)(id<<4);
  }
  assert(DegasFtsReport2(packet, sizeof(packet), &report));
  assert(report.count == 10 && report.contacts[1].event == 1);
  saved = report;
  for (x = 0; x < sizeof(packet); ++x) {
   assert(!DegasFtsReport2(packet, x, &report));
   assert(memcmp(&report, &saved, sizeof(report)) == 0);
  }
  packet[6+9*8] = 0;
  assert(!DegasFtsReport2(packet, sizeof(packet), &report));
  assert(memcmp(&report, &saved, sizeof(report)) == 0);
  packet[1] = 0x20;
  assert(!DegasFtsReport2(packet, sizeof(packet), &report));
  packet[1] = 0x81;
  assert(!DegasFtsReport2(packet, sizeof(packet), &report));
 }
 {
  /* Actual CRC-valid packet from packet-v2 after9E=00. */
  uint8_t p[16]={0,0x21,0xff,0xff,0x82,0x80,3,0x67,0xc4,0x0b,0x16,0x21,0xff,0xff,0xff,0xff};
  DEGAS_FTS_REPORT r;
  assert(DegasFtsReport2(p,sizeof(p),&r));
  assert(r.count==1 && r.contacts[0].id==0 && r.contacts[0].event==2);
  assert(r.contacts[0].x==64075 && r.contacts[0].y==87125);
  p[4]=0x42;
  assert(DegasFtsReport2(p,sizeof(p),&r) && r.contacts[0].event==1);
  p[1]=0x22; /* second contact absent from partial sample: must reject */
  assert(!DegasFtsReport2(p,sizeof(p),&r));
 }
 puts("FocalTech contact decoder: PASS (synthetic and captured packet)");
 return 0;
}
