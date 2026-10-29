#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "../degas/touch/fts_protocol.h"
int main(void){
 uint8_t tx[521],rx[521],out[512];size_t n,i;uint16_t crc;
 assert(DegasFtsCrc((const uint8_t*)"123456789",9)==0x6f91);
 for(n=1;n<=512;n++){
  memset(tx,0x55,sizeof tx);assert(DegasFtsReadFrame(0xa3,n,tx,sizeof tx)==n+9);
  assert(tx[0]==0xa3 && tx[1]==0xa0 && tx[2]==n/256 && tx[3]==n%256);
  memset(rx,0,sizeof rx);for(i=0;i<n;i++)rx[7+i]=(uint8_t)(i*73+n);
  crc=DegasFtsCrc(rx+7,n);rx[7+n]=(uint8_t)crc;rx[8+n]=(uint8_t)(crc>>8);
  assert(DegasFtsReadReply(rx,n+9,n,out,sizeof out));assert(!memcmp(out,rx+7,n));
  memset(out,0xa5,sizeof out);rx[3]=0x20;assert(!DegasFtsReadReply(rx,n+9,n,out,sizeof out));rx[3]=0;
  rx[7]^=1;assert(!DegasFtsReadReply(rx,n+9,n,out,sizeof out));
  for(i=0;i<sizeof out;i++)assert(out[i]==0xa5);
  assert(!DegasFtsReadReply(rx,n+8,n,out,sizeof out));
  assert(!DegasFtsReadFrame(0,n,tx,n+8));
 }
 assert(!DegasFtsReadFrame(0,0,tx,sizeof tx));assert(!DegasFtsReadFrame(0,513,tx,sizeof tx));
 puts("FTS framing/status/CRC/bounds tests PASS (offline only)");return 0;
}
