/* Independent bounded SPI read framing, reconstructed from the phone's
 * focaltech_touch_rothko.ko: fts_read 0x33a8c, header 0x33b74,
 * status 0x33c34, CRC 0x33c54..0x33d04. No MMIO or controller writes.
 * This is a protocol core, not yet a Windows hardware driver. */
#ifndef DEGAS_FTS_PROTOCOL_H
#define DEGAS_FTS_PROTOCOL_H
#ifndef DEGAS_TOUCH_UEFI_TYPES
#include <stddef.h>
#include <stdint.h>
#endif
#define DEGAS_FTS_MAX_READ 512u
static inline uint16_t DegasFtsCrc(const uint8_t *p,size_t n){
 uint16_t crc=0xffff;size_t i;unsigned bit;
 for(i=0;i<n;i++){crc^=p[i];for(bit=0;bit<8;bit++)crc=(uint16_t)((crc>>1)^((crc&1)?0x8408:0));}
 return crc;
}
static inline size_t DegasFtsReadFrame(uint8_t address,size_t count,uint8_t *tx,size_t capacity){
 size_t i,total;
 if(!tx || !count || count>DEGAS_FTS_MAX_READ)return 0;
 total=count+9;if(capacity<total)return 0;
 for(i=0;i<total;i++)tx[i]=0;
 tx[0]=address;tx[1]=0xa0;tx[2]=(uint8_t)(count>>8);tx[3]=(uint8_t)count;
 return total;
}
/* Output remains untouched on a short/status/CRC failure. */
static inline int DegasFtsReadReply(const uint8_t *rx,size_t length,size_t count,uint8_t *out,size_t capacity){
 size_t i;uint16_t crc;
 if(!rx || !out || !count || count>DEGAS_FTS_MAX_READ || capacity<count || length!=count+9)return 0;
 if(rx[3]&0xa0)return 0;
 crc=(uint16_t)(rx[7+count]|((uint16_t)rx[8+count]<<8));
 if(DegasFtsCrc(rx+7,count)!=crc)return 0;
 for(i=0;i<count;i++)out[i]=rx[7+i];
 return 1;
}
#endif
