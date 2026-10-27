#include "GceFramePacket.h"
#include "GceFrameGuard.h"
STATIC UINT32 GceFrameTest(volatile UINT32 *Fb,volatile UINT64 *Packet){
 UINT64 Words[DEGAS_GCE_FRAME_WORDS];UINT32 Result;UINTN X,Y;
 if((UINTN)Fb!=0xfcc2f000 || (UINTN)Packet!=DEGAS_GCE_PACKET_PA || !GceFrameGuard())return 50;
 if(!GceBuildFrame(Words,ARRAY_SIZE(Words)))return 51;
 // Deliberate raw framebuffer modification: no GOP/Flush call may follow
 // before reset. The user's visible rectangle must come from GCE submission.
 DEBUG((DEBUG_ERROR,"DEGAS GCEFRAME raw rectangle x=360 y=1900 w=500 h=120 color=CYAN FRESH FRAME DONE NO CPU FLUSH\n"));
 for(Y=1900;Y<2020;Y++)for(X=360;X<860;X++)Fb[Y*1248+X]=0xff00ffffU;
 WriteBackDataCacheRange((VOID *)(UINTN)Fb,4992U*2712U);ArmDataSynchronizationBarrier();
 Result=GceRunProgram(Words,ARRAY_SIZE(Words),DEGAS_GCE_FRAME_SENTINEL,Packet,GceFrameGuard,FALSE,NULL,FALSE);
 DEBUG((DEBUG_ERROR,"DEGAS GCEFRAME end result=%u dsi=%08x NO GOP AFTER TEST\n",Result,MmioRead32(0x1400d00c)));
 if(!Result && (MmioRead32(0x1400d00c)&0x80000010U)!=0x10U)return 52;
 return Result;
}
