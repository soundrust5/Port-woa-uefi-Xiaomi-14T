#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsReadCommand.h"
int main(void){
 unsigned char q[32];unsigned n;
 for(n=1;n<=14;n++){
  DegasUfsReadCommand(q,29267968,n,0xa7);
  assert(DegasUfsReadValid(q,n*4096));assert(q[3]==0xa7 && q[24]==n);
  assert(0x44472000ULL+n*4096<=0x44480000ULL);
 }
 DegasUfsReadCommand(q,DEGAS_UFS_LAST_LBA,1,0);assert(DegasUfsReadValid(q,4096));
 DegasUfsReadCommand(q,DEGAS_UFS_LAST_LBA,2,0);assert(!DegasUfsReadValid(q,8192));
 DegasUfsReadCommand(q,DEGAS_UFS_LAST_LBA-13,14,255);assert(DegasUfsReadValid(q,57344));
 DegasUfsReadCommand(q,DEGAS_UFS_LAST_LBA-12,14,255);assert(!DegasUfsReadValid(q,57344));
 DegasUfsReadCommand(q,0,15,0);assert(!DegasUfsReadValid(q,61440));
 DegasUfsReadCommand(q,0,0,0);assert(!DegasUfsReadValid(q,0));
 DegasUfsReadCommand(q,0,14,0);q[24]=13;assert(!DegasUfsReadValid(q,57344));
 DegasUfsReadCommand(q,0,14,0);q[16]=0x2a;assert(!DegasUfsReadValid(q,57344));
 DegasUfsReadCommand(q,0,14,0);q[2]=0;assert(!DegasUfsReadValid(q,57344));
 puts("PASS: READ10 encoding, DMA limits, last LBA, reject writes and wrong LUN/length");
}
