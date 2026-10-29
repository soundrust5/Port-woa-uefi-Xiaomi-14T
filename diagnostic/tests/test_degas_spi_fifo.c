#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../degas/touch/mtk_spi_fifo.h"
typedef struct { uint32_t cmd, cfg, words[8]; unsigned written, read, ticks, started, hang, resets; } MODEL;
static uint32_t rd(void *v,uint32_t a) {
 MODEL *m=v;
 switch(a) {
 case 0x18:return m->cmd;
 case 4:return m->cfg;
 /* Captured MT6897 idle state: STATUS1=1, STATUS0=0 before ACT. */
 case 0x20:return !m->started || (!m->hang && m->ticks>=3);
 case 0x1c:return m->started && !m->hang && m->ticks>=3;
 case 0x14:assert(m->read<m->written);return m->words[m->read++];
 default:assert(0);return 0;
 }
}
static void wr(void *v,uint32_t a,uint32_t x) {
 MODEL *m=v;
 if(a==4) m->cfg=x;
 else if(a==0x10) {assert(m->written<8);m->words[m->written++]=x;}
 else {assert(a==0x18);m->cmd=x;if(x&1)m->started=1;if(x&4)m->resets++;}
}
static void barrier(void *v){(void)v;}
static void delay(void *v,uint32_t n){((MODEL*)v)->ticks+=n;}
int main(void) {
 uint8_t tx[32],rx[32];unsigned n,i;
 for(i=0;i<32;i++)tx[i]=(uint8_t)(i*7);
 for(n=1;n<=32;n++) {
  MODEL m={0};m.cmd=(3u<<12)|(1u<<18)|(1u<<19)|(3u<<22)|(1u<<7);m.cfg=0xffffab55;
  DEGAS_SPI_IO io={&m,rd,wr,barrier,delay};
  memset(rx,0xaa,sizeof(rx));
  assert(!DegasSpiFifoOnce(&io,tx,rx,n,10));
  assert(!memcmp(tx,rx,n));
  assert(m.cfg==(((n-1)<<16)|0x55));
  assert(m.written==(n+3)/4 && m.read==m.written && m.ticks==3);
  for(i=n;i<32;i++)assert(rx[i]==0xaa);
 }
 {
  MODEL m={0};m.cmd=3u<<12;m.hang=1;
  DEGAS_SPI_IO io={&m,rd,wr,barrier,delay};
  memset(rx,0xaa,sizeof(rx));
  assert(DegasSpiFifoOnce(&io,tx,rx,10,7)==2);
  assert(m.ticks==7 && m.resets==1 && m.read==0);
  for(i=0;i<32;i++)assert(rx[i]==0xaa);
  assert(DegasSpiFifoOnce(&io,tx,rx,33,7)==1);
  m.cmd|=1u<<10;
  assert(DegasSpiFifoOnce(&io,tx,rx,10,7)==3);
 }
 puts("MediaTek single-packet FIFO model: PASS (not hardware validation)");
}
