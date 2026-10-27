// Mapping/publication before StartImage; hardware-only activation at successful
// EBS, after the last CPU frame. No code or CPU callback is needed by GCE later.
STATIC UINT64 mWindowsGceWords[DEGAS_GCE_FRAME_WORDS];
STATIC BOOLEAN mWindowsGceReady,mWindowsGceAttempted,mWindowsGceActive;
STATIC UINT32 GceWindowsPrepare(VOID){
 UINT32 Result;UINTN I;
 volatile UINT64 *Packet=(volatile UINT64 *)(UINTN)DEGAS_GCE_PACKET_PA;
 if(mWindowsGceReady || mWindowsGceAttempted)return 56;
 Result=GceMappingInstall((UINT32 *)(UINTN)DEGAS_GCE_ROOT_PA,(UINT32 *)(UINTN)DEGAS_GCE_LEAF_PA);
 if(Result)return Result;
 if(!GceBuildFrame(mWindowsGceWords,ARRAY_SIZE(mWindowsGceWords)))return 51;
 mWindowsGceWords[DEGAS_GCE_FRAME_WORDS-1]=0x1000000100000000ULL|((DEGAS_GCE_IOVA+0x40000000ULL)>>3);
 for(I=0;I<ARRAY_SIZE(mWindowsGceWords);I++)Packet[I]=mWindowsGceWords[I];
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
 for(I=0;I<ARRAY_SIZE(mWindowsGceWords);I++)if(Packet[I]!=mWindowsGceWords[I])return 57;
 WriteBackDataCacheRange((VOID *)(UINTN)0xfcc2f000,4992U*2712U);
 ArmDataSynchronizationBarrier();
 Result=GceRunProgram(mWindowsGceWords,ARRAY_SIZE(mWindowsGceWords),DEGAS_GCE_FRAME_SENTINEL,Packet,GceFrameGuard,TRUE,NULL,TRUE);
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
