// Query only. Linux mtk_iommu.c:688 and exact saved GZ dispatcher/query
// corroborate this ID and seven zero inputs. See 2026-10-26 GCE evidence.
// A successful query does NOT establish a DMA mapping or permission to run GCE.
STATIC UINT32 GceMappingQuery(VOID){
 ARM_SMC_ARGS Query={0};
 UINTN El=ArmReadCurrentEL();
 UINT32 SnapshotStatus;
 DEBUG((DEBUG_ERROR,"DEGAS GCE MAPPING begin CurrentEL=%Lx QUERY ONLY\n",(UINT64)El));
 SnapshotStatus=GceSnapshot();
 if(SnapshotStatus)return SnapshotStatus;
 if(El!=4){
  DEBUG((DEBUG_ERROR,"DEGAS GCE MAPPING skip unexpected exception level\n"));return 5;
 }
 Query.Arg0=0xbb00ffa4;
 DEBUG((DEBUG_ERROR,"DEGAS GCE MAPPING query_begin id=bb00ffa4 inputs=zero\n"));
 MemoryFence();
 ArmCallSmc(&Query);
 MemoryFence();
 DEBUG((DEBUG_ERROR,"DEGAS GCE MAPPING query_return a0=%016Lx a1=%016Lx a2=%016Lx a3=%016Lx\n",
  (UINT64)Query.Arg0,(UINT64)Query.Arg1,(UINT64)Query.Arg2,(UINT64)Query.Arg3));
 // Re-observe only previously accessible words. Do not dereference any root.
 GceSnapshotRead(0x1e980054);
 GceSnapshotRead(0x1e810000);
 GceSnapshotRead(0x1e810048);
 GceSnapshotRead(0x1e810110);
 if(Query.Arg0==0 || Query.Arg0==1){
  DEBUG((DEBUG_ERROR,"DEGAS GCE MAPPING query_valid type2=%u DMA_MAPPING_UNPROVEN\n",(UINT32)Query.Arg0));
  return 0;
 }
 DEBUG((DEBUG_ERROR,"DEGAS GCE MAPPING query unavailable or unexpected reply NO INIT\n"));
 return 6;
}
