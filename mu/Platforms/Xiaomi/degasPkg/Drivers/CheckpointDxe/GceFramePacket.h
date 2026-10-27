#ifndef DEGAS_GCE_FRAME_PACKET_H
#define DEGAS_GCE_FRAME_PACKET_H
#define DEGAS_GCE_FRAME_WORDS 62U
#define DEGAS_GCE_FRAME_SENTINEL 0x76543210U
// Port of the independently decoded offline one-frame packet. Fixed-size
// Three idle polls, fresh FRAME_DONE clear/set handshake, 18 retained transfer
// writes plus one W0C acknowledgment. A latched completion cannot be missed
// like a short BUSY assertion; V18's BUSY handshake timed out. Never enable CPU interrupts here.
STATIC VOID GceFramePoll(UINT64 *P,UINTN *N,UINT32 Mask,UINT32 Want){
 P[(*N)++]=0x0200000000000000ULL|(~Mask&0xffffffffU);
 // Vendor cmdq-test uses R14 for indirect POLL addresses. R0 is the mask
 // register; cmdq.h explicitly warns about using its R1/P0 alias alongside it.
 // V19's R1 was zero while a positive POLL stalled despite CPU INTSTA=0x16.
 P[(*N)++]=0x028e00001400d00cULL;
 P[(*N)++]=0x088e000100000000ULL|Want;
}
STATIC VOID GceFrameWrite(UINT64 *P,UINTN *N,UINT32 Address,UINT32 Value,UINT32 Mask){
 P[(*N)++]=0xa080000000000000ULL|(Address>>16);
 if(Mask!=0xffffffffU)P[(*N)++]=0x0200000000000000ULL|(~Mask&0xffffffffU);
 P[(*N)++]=((Mask==0xffffffffU?0x9000000000000000ULL:0x9100000000000000ULL) |
            ((UINT64)((Address&0xffffU)|2U)<<32)|Value);
}
STATIC BOOLEAN GceBuildFrame(UINT64 *P,UINTN Capacity){
 UINTN N=0;
 if(P==NULL || Capacity<DEGAS_GCE_FRAME_WORDS)return FALSE;
 GceFramePoll(P,&N,0x80000000U,0);
 GceFrameWrite(P,&N,0x1400d190,2,2);GceFrameWrite(P,&N,0x1400d190,4,4);
 GceFrameWrite(P,&N,0x1400d008,0,0xffffffffU);GceFrameWrite(P,&N,0x1400d014,0,3);
 GceFrameWrite(P,&N,0x1400d100,0x55,0xffffffffU);GceFrameWrite(P,&N,0x1400d104,1,1);
 GceFrameWrite(P,&N,0x14201008,0,0xffffffffU);GceFrameWrite(P,&N,0x14201020,1,0xffffffffU);
 GceFrameWrite(P,&N,0x14601008,0,0xffffffffU);GceFrameWrite(P,&N,0x14601020,1,0xffffffffU);
 GceFrameWrite(P,&N,0x14001008,0,0xffffffffU);GceFrameWrite(P,&N,0x14001020,1,0xffffffffU);
 GceFrameWrite(P,&N,0x14401008,0,0xffffffffU);GceFrameWrite(P,&N,0x14401020,1,0xffffffffU);
 GceFramePoll(P,&N,0x80000000U,0);
 GceFrameWrite(P,&N,0x1400dd00,0x002c3909,0xffffffffU);GceFrameWrite(P,&N,0x1400d060,0x8001,0xffffffffU);
 // DSI INTSTA W0C, as in mtk_dsi_irq_status: writing one preserves other flags.
 GceFrameWrite(P,&N,0x1400d00c,0xffffffefU,0xffffffffU);
 GceFramePoll(P,&N,0x10U,0);
 GceFrameWrite(P,&N,0x1400d000,0,1);GceFrameWrite(P,&N,0x1400d000,1,1);
 GceFramePoll(P,&N,0x10U,0x10U);
 GceFramePoll(P,&N,0x80000000U,0);
 P[N++]=0xa080000000000000ULL|DEGAS_GCE_FRAME_SENTINEL;
 P[N++]=0x4000000000000000ULL;P[N++]=0x1000000000000001ULL;
 return N==DEGAS_GCE_FRAME_WORDS;
}
#endif
