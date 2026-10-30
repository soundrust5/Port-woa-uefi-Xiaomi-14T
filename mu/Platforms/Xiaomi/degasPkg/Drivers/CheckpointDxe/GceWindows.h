// Mapping/publication before StartImage; hardware-only activation at successful
// EBS, after the last CPU frame. No code or CPU callback is needed by GCE later.
STATIC UINT64 mWindowsGceWords[DEGAS_GCE_TE_FRAME_WORDS];
STATIC UINTN mWindowsGceCount;
STATIC BOOLEAN mWindowsGceReady,mWindowsGceAttempted,mWindowsGceActive;
STATIC BOOLEAN GceWindowsQualifyTe(VOID){
 UINT64 Start=GetPerformanceCounter(),Previous=Start,Now,Delta[3]={0};UINT32 Edges=0,Control;
 Control=MmioRead32(0x1400d018);
 if(!GceFrameGuard())return FALSE;
 MmioWrite32(0x1400d00c,0xfffffffbU);MemoryFence();
 while(GetTimeInNanoSecond(GetPerformanceCounter()-Start)<100000000ULL && Edges<3){
  if(MmioRead32(0x1400d00c)&4U){
   Now=GetPerformanceCounter();Delta[Edges++]=GetTimeInNanoSecond(Now-Previous);Previous=Now;
   MmioWrite32(0x1400d00c,0xfffffffbU);MemoryFence();
  }
  MicroSecondDelay(10);
 }
 // Require two plausible complete periods; first edge can arrive immediately.
 BOOLEAN Valid=Edges==3 && Delta[1]>=4000000ULL && Delta[1]<=25000000ULL &&
  Delta[2]>=4000000ULL && Delta[2]<=25000000ULL;
 DEBUG((DEBUG_ERROR,"DEGAS GCE TE qualify=%u edges=%u txrx=%08x ns=%Lu,%Lu,%Lu\n",Valid,Edges,Control,Delta[0],Delta[1],Delta[2]));
 return Valid;
}
STATIC UINT32 GceWindowsPrepare(VOID){
 UINT32 Result;UINTN I;BOOLEAN Te;
 volatile UINT64 *Packet=(volatile UINT64 *)(UINTN)DEGAS_GCE_PACKET_PA;
 if(mWindowsGceReady || mWindowsGceAttempted)return 56;
 Result=GceMappingInstall((UINT32 *)(UINTN)DEGAS_GCE_ROOT_PA,(UINT32 *)(UINTN)DEGAS_GCE_LEAF_PA);
 if(Result)return Result;
 Te=GceWindowsQualifyTe();mWindowsGceCount=Te?DEGAS_GCE_TE_FRAME_WORDS:DEGAS_GCE_FRAME_WORDS;
 if(!GceBuildFrameEx(mWindowsGceWords,ARRAY_SIZE(mWindowsGceWords),Te))return 51;
 mWindowsGceWords[mWindowsGceCount-1]=0x1000000100000000ULL|((DEGAS_GCE_IOVA+0x40000000ULL)>>3);
 for(I=0;I<mWindowsGceCount;I++)Packet[I]=mWindowsGceWords[I];
 ArmDataSynchronizationBarrier();
 mWindowsGceReady=TRUE;
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS GCE PREPARED RESERVED PACKET THREAD DISABLED\n"));
 return 0;
}
STATIC UINT32 GceWindowsStart(VOID){
 UINT32 Result;UINTN I;
 volatile UINT64 *Packet=(volatile UINT64 *)(UINTN)DEGAS_GCE_PACKET_PA;
 if(!mWindowsGceReady || mWindowsGceAttempted)return 56;
 mWindowsGceAttempted=TRUE;
 for(I=0;I<mWindowsGceCount;I++)if(Packet[I]!=mWindowsGceWords[I])return 57;
 WriteBackDataCacheRange((VOID *)(UINTN)0xfcc2f000,4992U*2712U);
 ArmDataSynchronizationBarrier();
 Result=GceRunProgram(mWindowsGceWords,mWindowsGceCount,DEGAS_GCE_FRAME_SENTINEL,Packet,GceFrameGuard,TRUE,NULL,TRUE);
 mWindowsGceActive=(Result==0);
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS GCE START result=%u active=%u\n",Result,mWindowsGceActive));
 return Result;
}
STATIC VOID GceWindowsStop(VOID){
 if(mWindowsGceActive){
  UINT32 Result=GceStopProgram();
  BOOLEAN Idle=GceSmokePoll(0x1400d00c,0x80000000U,0,100000);
  DEBUG((DEBUG_ERROR,"DEGAS WINDOWS GCE STOP result=%u drain=%u\n",Result,Idle));
  mWindowsGceActive=FALSE;
 }
}
