#include "GceFramePacket.h"
#include "GceFrameGuard.h"
// Reset-only autonomous frame-loop probe. CPU changes pixels, never submits
// frames. The immutable GCE packet owns all DSI transfer/acknowledgment writes.
STATIC VOID GceLoopPaint(UINT32 Color){
 volatile UINT32 *Fb=(volatile UINT32 *)(UINTN)0xfcc2f000;
 UINTN X,Y;
 for(Y=1900;Y<2020;Y++)for(X=360;X<860;X++)Fb[Y*1248+X]=Color;
 WriteBackDataCacheRange((VOID *)(UINTN)Fb,4992U*2712U);
 ArmDataSynchronizationBarrier();
}
STATIC VOID GceLoopProgress(UINT32 Tick,UINT32 Count){
 UINT32 Color=Tick==5000U?0xffff0000U:(Tick==10000U?0xff00ff00U:0xff0000ffU);
 GceLoopPaint(Color);
 DEBUG((DEBUG_ERROR,"DEGAS GCELOOP pixels tick=%u count=%08x color=%08x NO CPU FLUSH\n",Tick,Count,Color));
}
STATIC UINT32 GceLoopTest(volatile UINT64 *Packet){
 UINT64 Words[DEGAS_GCE_FRAME_WORDS];UINT32 Result;BOOLEAN Idle;
 if((UINTN)Packet!=DEGAS_GCE_PACKET_PA || !GceFrameGuard())return 50;
 if(!GceBuildFrame(Words,ARRAY_SIZE(Words)))return 51;
 // cmdq_pkt_finalize_loop: EOC then absolute branch to first instruction.
 Words[ARRAY_SIZE(Words)-1]=0x1000000100000000ULL|((DEGAS_GCE_IOVA+0x40000000ULL)>>3);
 DEBUG((DEBUG_ERROR,"DEGAS GCELOOP begin 2s bounded CYAN RED GREEN BLUE NO GOP\n"));
 GceLoopPaint(0xff00ffffU);
 Result=GceRunProgram(Words,ARRAY_SIZE(Words),DEGAS_GCE_FRAME_SENTINEL,Packet,GceFrameGuard,TRUE,GceLoopProgress,FALSE);
 // GCE reset may stop between START and FRAME_DONE. Let the last DSI frame
 // drain without any writes. Do not resume GOP or release the mapped page.
 Idle=GceSmokePoll(0x1400d00c,0x80000000U,0,100000);
 DEBUG((DEBUG_ERROR,"DEGAS GCELOOP end result=%u drain=%u dsi=%08x KEEP UNTIL RESET\n",Result,Idle,MmioRead32(0x1400d00c)));
 if(!Result && !Idle)return 55;
 return Result;
}
