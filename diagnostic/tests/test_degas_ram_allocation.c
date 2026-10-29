#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/RamAllocationCore.h"
int main(void){
 UINT64 B=DEGAS_RAM_PROBE_BASE,E=B+DEGAS_RAM_PROBE_SIZE,A;
 assert(RamAllocationInWindow(B,DEGAS_RAM_PROBE_SIZE));
 assert(RamAllocationInWindow(E-DEGAS_RAM_ALLOC_BYTES,DEGAS_RAM_ALLOC_BYTES));
 assert(!RamAllocationInWindow(B-4096,DEGAS_RAM_ALLOC_BYTES));
 assert(!RamAllocationInWindow(E-DEGAS_RAM_ALLOC_BYTES+1,DEGAS_RAM_ALLOC_BYTES));
 assert(!RamAllocationInWindow(B,0));
 assert(!RamAllocationInWindow(B,MAX_UINT64));
 assert(!RamAllocationInWindow(MAX_UINT64-4095,8192));
 for(A=B;A<E;A+=0x100008){
  assert(RamAllocationPattern(A,1)==~RamAllocationPattern(A,2));
  assert(RamAllocationPattern(A,1)!=RamAllocationPattern(A+8,1));
 }
 puts("Allocation window rejects crossing/overflow/zero ranges; patterns PASS");return 0;
}
