/* Bounded single-packet FIFO engine, based on captured MiCode spi-mt65xx.c.
 * No physical addresses. Caller exclusively
 * owns controller and has configured clocks, pins, mode0, CS0 and timing.
 * This engine cannot transfer a full report (>32 bytes); no silent CS split.
 */
#ifndef DEGAS_MTK_SPI_FIFO_H
#define DEGAS_MTK_SPI_FIFO_H
#ifndef DEGAS_TOUCH_UEFI_TYPES
#include <stddef.h>
#include <stdint.h>
#endif
typedef struct {
 void *context;
 uint32_t (*read)(void *, uint32_t);
 void (*write)(void *, uint32_t, uint32_t);
 void (*barrier)(void *);
 void (*delay_us)(void *, uint32_t);
} DEGAS_SPI_IO;
/* 0 success, 1 invalid argument, 2 timeout, 3 incompatible controller state.
 * Timeout resets host engine only, leaves RX output unchanged. */
static inline int DegasSpiFifoOnce(const DEGAS_SPI_IO *io,
 const uint8_t *tx, uint8_t *rx, size_t length, unsigned timeout_us) {
 uint8_t result[32];
 uint32_t cmd, cfg, status;
 size_t i, j;
 unsigned elapsed;
 if (!io || !io->read || !io->write || !io->barrier || !io->delay_us ||
     !tx || !rx || !length || length > 32 || !timeout_us || timeout_us > 200000) return 1;
 cmd = io->read(io->context, 0x18);
 /* Reject DMA, interrupts, pause, pending start/resume/reset, endian swap,
  * CPHA/CPOL. Require MSB-first TX/RX. Initialization is a separate step. */
 if ((cmd & (7u | (1u<<4) | (3u<<8) | (3u<<10) | (15u<<14))) ||
     (cmd & (3u<<12)) != (3u<<12)) return 3;
 /* STATUS1 is already1 on idle MT6897 hardware after reset (STATUS0=0).
  * Do not require it to be zero before ACT. Android's FIFO polling path
  * tests STATUS1 only after issuing ACT; STATUS0 consumes event status. */
 (void)io->read(io->context, 0x1c);
 cfg = io->read(io->context, 4);
 cfg = (cfg & 255u) | ((uint32_t)(length-1) << 16);
 io->write(io->context, 4, cfg); /* IPM packet length, one packet. */
 for (i=0; i<length; i+=4) {
  uint32_t word=0;
  for(j=0;j<4 && i+j<length;++j) word |= (uint32_t)tx[i+j] << (8*j);
  io->write(io->context, 0x10, word);
 }
 io->barrier(io->context);
 io->write(io->context, 0x18, cmd | 1u);
 for(elapsed=0;;++elapsed) {
  status=io->read(io->context, 0x20);
  if(status) break;
  if(elapsed==timeout_us) {
   io->write(io->context, 0x18, cmd | 4u);
   io->barrier(io->context);
   io->write(io->context, 0x18, cmd);
   return 2;
  }
  io->delay_us(io->context, 1);
 }
 (void)io->read(io->context, 0x1c);
 io->barrier(io->context);
 for(i=0;i<length;i+=4) {
  uint32_t word=io->read(io->context, 0x14);
  for(j=0;j<4 && i+j<length;++j) result[i+j]=(uint8_t)(word>>(8*j));
 }
 for(i=0;i<length;++i) rx[i]=result[i];
 return 0;
}
#endif
