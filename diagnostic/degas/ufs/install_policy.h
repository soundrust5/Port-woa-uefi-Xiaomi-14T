/* Partition-scoped write authority for the captured degas LUN2 GPT.
 * End LBAs are exclusive. Caller still validates the current disk/GPT before
 * formatting/installing; this policy never authorizes partition-table writes.
 */
#ifndef DEGAS_UFS_INSTALL_POLICY_H
#define DEGAS_UFS_INSTALL_POLICY_H
#include <stdint.h>
#ifndef DEGAS_UFS_INSTALL_MODE
#define DEGAS_UFS_INSTALL_MODE 0
#endif
#if DEGAS_UFS_INSTALL_MODE != 0 && DEGAS_UFS_INSTALL_MODE != 1
#error DEGAS_UFS_INSTALL_MODE must be explicitly0 or1
#endif
#define DEGAS_WINDOWS_FIRST 29796352ULL
#define DEGAS_WINDOWS_END   64573432ULL
#define DEGAS_ESP_FIRST     29267968ULL
#define DEGAS_ESP_END       29792256ULL
static inline int DegasUfsRangeInside(uint64_t lba,uint64_t blocks,uint64_t first,uint64_t end){
 return blocks && lba>=first && lba<end && blocks<=end-lba;
}
static inline int DegasUfsWritableRange(unsigned lun,uint64_t lba,uint64_t blocks){
 if(lun!=2)return 0;
#if DEGAS_UFS_INSTALL_MODE
 return DegasUfsRangeInside(lba,blocks,DEGAS_WINDOWS_FIRST,DEGAS_WINDOWS_END) ||
        DegasUfsRangeInside(lba,blocks,DEGAS_ESP_FIRST,DEGAS_ESP_END);
#else
 return DegasUfsRangeInside(lba,blocks,29800448ULL,29800448ULL+65536ULL);
#endif
}
static inline uint32_t DegasUfsBe32(const uint8_t *p){
 return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
/* Only qualified4096-byte logical blocks. FUA is accepted only in install
 * mode and is implemented by WRITE then real SYNCHRONIZE CACHE completion.
 * No protection information, DPO, group/control bits or vendor flags. */
static inline int DegasUfsWriteAllowed(const uint8_t *c,unsigned length,
 unsigned lun,uint32_t bytes,uint32_t maximum){
 uint64_t lba,blocks;
 if(!c || bytes>maximum)return 0;
 if(length==10 && c[0]==0x2a){
  lba=DegasUfsBe32(c+2);blocks=((unsigned)c[7]<<8)|c[8];
  if(c[6] || c[9])return 0;
 }else if(DEGAS_UFS_INSTALL_MODE && length==16 && c[0]==0x8a){
  lba=((uint64_t)DegasUfsBe32(c+2)<<32)|DegasUfsBe32(c+6);
  blocks=DegasUfsBe32(c+10);
  if(c[14] || c[15])return 0;
 }else return 0;
 if(c[1]&~(DEGAS_UFS_INSTALL_MODE?8u:0u))return 0;
 return blocks<=maximum/4096 && bytes==blocks*4096 && DegasUfsWritableRange(lun,lba,blocks);
}
#endif
