#include "../../Include/RamProbeRange.h"
#define DEGAS_RAM_ALLOC_BYTES 0x10000000ULL
#define DEGAS_RAM_ALLOC_CHUNK 0x100000U
STATIC BOOLEAN RamAllocationInWindow(UINT64 Address,UINT64 Bytes){
 return Bytes!=0 && Address>=DEGAS_RAM_PROBE_BASE && Bytes<=DEGAS_RAM_PROBE_SIZE &&
  Address-DEGAS_RAM_PROBE_BASE<=DEGAS_RAM_PROBE_SIZE-Bytes;
}
STATIC UINT64 RamAllocationPattern(UINT64 Address,UINT32 Pass){
 UINT64 V=(Address^0xa5d33c799e3779b9ULL)*0xbf58476d1ce4e5b9ULL;
 return Pass==1 ? V : ~V;
}
