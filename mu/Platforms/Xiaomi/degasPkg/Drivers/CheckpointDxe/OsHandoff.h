// Redmi 9's platform leaves watchdog disabled for the OS. Adapt the operation
// to MT6897's mapped watchdog and preserve mode bits, as mtk_wdt_stop() does.
// Timer/GIC shutdown remains owned by the shared Arm TimerDxe/ArmGicDxe.
STATIC BOOLEAN mOsHandoffDone;
STATIC VOID DegasOsHandoff(VOID){
 UINT32 Mode,Tr,Tm;UINTN I;
 if(mOsHandoffDone)return;
 mOsHandoffDone=TRUE;mUfsActive=FALSE;
 Tr=MmioRead32(0x112b0058);Tm=MmioRead32(0x112b0078);
 // All boot reads are synchronous; no allocations or Boot Services here.
 MmioWrite32(0x112b0024,0); // UFS interrupt enable
 MmioWrite32(0x112b0060,0); // UTRL run/stop
 MmioWrite32(0x112b0080,0); // UTMRL run/stop
 // Windows MT6897 read-only miniport takes over the initialized PHY/link.
 // Keep HCE only when no command is outstanding and both lists are stopped.
 // Remove firmware descriptor addresses before the new OS allocates its own.
 for(I=0;I<1000;I++){
  if(!(MmioRead32(0x112b0060)&1) && !(MmioRead32(0x112b0080)&1))break;
  MicroSecondDelay(1);
 }
 if(Tr || Tm || I==1000 || (MmioRead32(0x112b0030)&15)!=15){
  MmioWrite32(0x112b0034,0);
  DEBUG((DEBUG_ERROR,"DEGAS UFS OS HANDOFF rejected pending DMA or stopped link\n"));
 }else{
  MmioWrite32(0x112b0050,0);MmioWrite32(0x112b0054,0);
  MmioWrite32(0x112b0070,0);MmioWrite32(0x112b0074,0);
  MemoryFence();
  DEBUG((DEBUG_ERROR,"DEGAS UFS OS HANDOFF live link idle queues no firmware DMA\n"));
 }
 Mode=MmioRead32(0x1c00a000);
#if defined(DEGAS_DIAGNOSTIC_EBS_WATCHDOG) && DEGAS_DIAGNOSTIC_EBS_WATCHDOG
 // Temporary diagnostic only: force automatic recovery even after successful EBS.
 // No Boot Services, allocation, timer callbacks or filesystem operations.
 MmioWrite32(0x1c00a004,(10u<<11)|8u);
 MmioWrite32(0x1c00a000,0x22000000u|((Mode&0x00ffffffu)&~0x48u)|1u);
 MmioWrite32(0x1c00a008,0x1971);
 DEBUG((DEBUG_ERROR,"DEGAS DIAGNOSTIC EBS RESET ARMED seconds=10\n"));
#else
 MmioWrite32(0x1c00a000,(Mode&~1u)|0x22000000u);
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS WATCHDOG DISABLED AT EBS\n"));
#endif
 MemoryFence();
 DEBUG((DEBUG_ERROR,"DEGAS HANDOFF utrl=%08x utmrl=%08x hce=%08x watchdog_before=%08x after=%08x\n",
  Tr,Tm,MmioRead32(0x112b0034),Mode,MmioRead32(0x1c00a000)));
}
