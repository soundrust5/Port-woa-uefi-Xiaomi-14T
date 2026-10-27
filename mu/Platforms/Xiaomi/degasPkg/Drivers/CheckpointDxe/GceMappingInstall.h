#include "../../Include/GceMap.h"

// Exact GZ A6 ABI: X0 low32=NS descriptor, high32=protected descriptor.
// A negative signed X0 can be a valid protected mapping. Only exact errors
// are rejected; never dereference an inherited hardware page-table address.
STATIC BOOLEAN GceMapQuery(UINT64 Iova,UINT64 *Value){
 ARM_SMC_ARGS Q={0};UINTN Error;
 Q.Arg0=0xbb00ffa6;Q.Arg1=(UINT32)Iova;Q.Arg2=(UINT32)(Iova>>32);
 MemoryFence();ArmCallSmc(&Q);MemoryFence();*Value=Q.Arg0;
 for(Error=1;Error<=3;Error++)
  if(*Value==(UINT64)(0ULL-Error) || *Value==(UINT32)(0U-Error))return FALSE;
 return TRUE;
}
STATIC UINT32 GceMapType(UINT32 Descriptor){return (Descriptor>>8)&3U;}
STATIC BOOLEAN GceMapEmpty(UINT64 Value){
 return GceMapType((UINT32)Value)==0 && GceMapType((UINT32)(Value>>32))==0;
}

// Mapping-only, sole firmware client, reset required on ALL outcomes. Never
// chainload an OS after changing the guest association. INV synchronizes the
// whole 1 MiB leaf, including invalid leaves, so all 256 pages must be empty.
// A conflicting already registered root may panic inside GZ before the write;
// the independently armed hardware watchdog provides recovery in that case.
STATIC UINT32 GceMappingInstall(UINT32 *Root,UINT32 *Leaf){
 STATIC CONST UINT64 Markers[]={0x107400000ULL,0x1074ff000ULL,0xfcc2f000ULL};
 UINT64 Before[ARRAY_SIZE(Markers)],Value;UINT32 Status,I,Ns;ARM_SMC_ARGS Q={0};
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP begin MAPPING ONLY RESET REQUIRED\n"));
 if(Root==NULL || Leaf==NULL)return 10;
 if(ArmReadCurrentEL()!=4)return 11;
 Status=GceSnapshot();if(Status)return 20+Status;
 Q.Arg0=0xbb00ffa4;MemoryFence();ArmCallSmc(&Q);MemoryFence();
 if(Q.Arg0!=1)return 12;
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP preflight begin pages=256 table=0 base=%Lx target=%Lx\n",DEGAS_GCE_MAP_BASE,DEGAS_GCE_IOVA));
 for(I=0;I<256;I++){
  if((I&63U)==0)DEBUG((DEBUG_ERROR,"DEGAS GCEMAP preflight page=%u\n",I));
  if(!GceMapQuery(DEGAS_GCE_MAP_BASE+((UINT64)I<<12),&Value)){
   DEBUG((DEBUG_ERROR,"DEGAS GCEMAP preflight query_error page=%u raw=%016Lx\n",I,Value));return 13;
  }
  if(!GceMapEmpty(Value)){
   DEBUG((DEBUG_ERROR,"DEGAS GCEMAP preflight occupied page=%u raw=%016Lx\n",I,Value));return 14;
  }
 }
 for(I=0;I<ARRAY_SIZE(Markers);I++){
  if(!GceMapQuery(Markers[I],&Before[I]))return 15;
  DEBUG((DEBUG_ERROR,"DEGAS GCEMAP marker before iova=%Lx raw=%016Lx\n",Markers[I],Before[I]));
 }
 if(!DegasGceBuildMap(Root,Leaf))return 16;
 // NC reserved memory is already mapped by MemoryMapLib. Fence publishes all
 // table entries before GZ reads them; there is no cached alias in this build.
 ArmDataSynchronizationBarrier();
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP root_write_begin guest=44440000 leaf=44450000 packet=44451000\n"));
 ArmDataSynchronizationBarrier();MmioWrite32(0x1e810000,(UINT32)DEGAS_GCE_ROOT_PA);ArmDataSynchronizationBarrier();
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP root_write_return observed=%08x\n",MmioRead32(0x1e810000)));
 // A7 uses encoded page addresses, inclusive end, MM_TABLE0. GZ syncs 1 MiB.
 ZeroMem(&Q,sizeof(Q));Q.Arg0=0xbb00ffa7;Q.Arg1=DEGAS_GCE_IOVA;Q.Arg2=DEGAS_GCE_IOVA;
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP sync_begin\n"));
 ArmDataSynchronizationBarrier();ArmCallSmc(&Q);ArmDataSynchronizationBarrier();
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP sync_return=%016Lx\n",(UINT64)Q.Arg0));
 if(Q.Arg0!=0)return 17;
 for(I=0;I<256;I++){
  if((I&63U)==0)DEBUG((DEBUG_ERROR,"DEGAS GCEMAP verification page=%u\n",I));
  if(!GceMapQuery(DEGAS_GCE_MAP_BASE+((UINT64)I<<12),&Value))return 18;
  Ns=(UINT32)Value;
  if(I==DEGAS_GCE_PAGE_INDEX){
   DEBUG((DEBUG_ERROR,"DEGAS GCEMAP mapped_page raw=%016Lx pa=%Lx\n",Value,DegasGceQueryPa(Ns)));
   if(GceMapType(Ns)!=2 || ((Ns>>10)&3U)!=2 ||
      DegasGceQueryPa(Ns)!=DEGAS_GCE_PACKET_PA || GceMapType((UINT32)(Value>>32))!=0)return 19;
  }else if(!GceMapEmpty(Value))return 30;
 }
 for(I=0;I<ARRAY_SIZE(Markers);I++){
  if(!GceMapQuery(Markers[I],&Value))return 31;
  DEBUG((DEBUG_ERROR,"DEGAS GCEMAP marker after iova=%Lx raw=%016Lx\n",Markers[I],Value));
  if(Value!=Before[I])return 32;
 }
 DEBUG((DEBUG_ERROR,"DEGAS GCEMAP PASS one NS page verified; no GCE execution; quarantine until reset\n"));
 return 0;
}
