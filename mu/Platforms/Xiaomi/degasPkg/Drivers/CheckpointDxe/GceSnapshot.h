// Read-only inventory, not command execution. MT6897 clock/power checker and
// exact LK GCTL evidence: docs/DEGAS-PANEL-AUDIT-20261026.md.
// A RAM breadcrumb precedes every new MMIO read. Never touch tokens/FIFO or
// reprogram clocks, power, IOMMU, security, GCE globals or any thread.
STATIC UINT32 GceSnapshotRead(UINTN Address){
 UINT32 Value;
 DEBUG((DEBUG_ERROR,"DEGAS GCE read_begin=%08x\n",(UINT32)Address));
 MemoryFence();Value=MmioRead32(Address);
 DEBUG((DEBUG_ERROR,"DEGAS GCE read_value=%08x value=%08x\n",(UINT32)Address,Value));
 return Value;
}
STATIC UINT32 GceSnapshot(VOID){
 STATIC CONST UINT16 Global[]={0x10,0x18,0x30,0x40,0x48,0x54,0xc0};
 STATIC CONST UINT16 Thread[]={0,8,0xc,0x10,0x14,0x20,0x24,0x28,0x30,0x40,0x44,0x50};
 STATIC CONST UINT16 Mmu[]={0,8,0x48,0x110};
 UINT32 Power,Cg0,Cg1,Gctl=0,Active=0;UINTN I;
 DEBUG((DEBUG_ERROR,"DEGAS GCE SNAPSHOT begin READ ONLY\n"));
 // MT6897_CHK_PD_MM_INFRA: SPM+0xEAC, both power acknowledgements required.
 Power=GceSnapshotRead(0x1c001eac);
 if((Power&0xc0000000U)!=0xc0000000U){
  DEBUG((DEBUG_ERROR,"DEGAS GCE skip reason=MMINFRA_POWER_OFF\n"));return 1;
 }
 Cg0=GceSnapshotRead(0x1e800100);Cg1=GceSnapshotRead(0x1e800110);
 if((Cg0&5U) || (Cg1&(1U<<17))){
  DEBUG((DEBUG_ERROR,"DEGAS GCE skip reason=CLOCK_GATED\n"));return 2;
 }
 // Check the inherited LK global control before the remaining GCE block.
 Gctl=GceSnapshotRead(0x1e980048);
 if(Gctl!=0x20002U){
  DEBUG((DEBUG_ERROR,"DEGAS GCE skip reason=UNEXPECTED_GCTL\n"));return 3;
 }
 for(I=0;I<ARRAY_SIZE(Global);I++)
  if(Global[I]!=0x48)GceSnapshotRead(0x1e980000+Global[I]);
 for(I=0;I<32;I++)if(GceSnapshotRead(0x1e980104+I*0x80)&1U)Active|=1U<<I;
 DEBUG((DEBUG_ERROR,"DEGAS GCE active_mask=%08x\n",Active));
 // Thread0 is a normal display channel in the saved actual FDT. Read-only
 // inspection does not claim ownership or permission to reset/start it.
 if(!Active)for(I=0;I<ARRAY_SIZE(Thread);I++)GceSnapshotRead(0x1e980100+Thread[I]);
 // Same powered MMINFRA/SMI domain; no page-table dereference or translation
 // assumptions. GCE_DM is a fake larb port: no NONSEC_CON address guessing.
 for(I=0;I<ARRAY_SIZE(Mmu);I++)GceSnapshotRead(0x1e810000+Mmu[I]);
 DEBUG((DEBUG_ERROR,"DEGAS GCE SNAPSHOT complete active=%08x NO WRITES\n",Active));
 return Active?4:0;
}
