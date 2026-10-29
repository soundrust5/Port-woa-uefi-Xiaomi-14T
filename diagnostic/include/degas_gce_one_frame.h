/* OFFLINE PROTOTYPE ONLY. Not linked by any firmware DSC/FDF.
 * MT6897 source: MiCode MTK_kernel_device_modules
 * 3c4ccd07d5624879555ec710c11bb8818e5cb5c6, cmdq mailbox/helper-ext.
 * Runs ONE copy of the previously verified CPU display transaction.
 * No clock, IOMMU, secure monitor, global reset, token or framebuffer writes.
 * Readiness and ownership MUST be established externally before MMIO use.
 */
#ifndef DEGAS_GCE_ONE_FRAME_H
#define DEGAS_GCE_ONE_FRAME_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define DEGAS_GCE_BASE UINT32_C(0x1e980000)
#define DEGAS_GCE_MAX_WORDS 96u
#define DEGAS_GCE_PROOFS 31u
#define DEGAS_GCE_CANDIDATE_THREADS UINT32_C(0x7f) /* NS display + no DDR: 0..6 */
/* cmdq-sec-mailbox.h reserves threads8..12 for secure execution. */
/* proofs bits: mapped/readable MMIO; clocks+power; DMA mapping+allocation
 * ownership; selected nonsecure thread permission; retained display contract.
 * These bits are assertions by a future validated caller, NOT discoveries. */
typedef struct { uint64_t word[DEGAS_GCE_MAX_WORDS]; size_t count; } DegasGcePacket;
typedef struct {
 void *context;
 uint32_t (*read32)(void *,uint32_t);
 void (*write32)(void *,uint32_t,uint32_t);
 void (*delay_us)(void *,unsigned);
 bool (*publish)(void *,const void *,size_t,uint64_t);
} DegasGceIo;
typedef struct {
 unsigned proofs,thread; uint64_t dma_address;
 unsigned timeout_us; /* 1..200000; no UEFI timer callback */
} DegasGceConfig;
typedef struct {
 bool started,completed,quiesced;
 uint32_t pc,end,irq,dsi_status; unsigned elapsed_us;
} DegasGceResult;

static uint64_t DegasGceInst(uint8_t op,uint16_t a,uint32_t value,
                           uint8_t sop,bool atype) {
 return ((uint64_t)op<<56)|((uint64_t)atype<<55)|
        ((uint64_t)(sop&31)<<48)|((uint64_t)a<<32)|value;
}
static void DegasGceAppend(DegasGcePacket *p,uint64_t w) { p->word[p->count++]=w; }
static void DegasGceMask(DegasGcePacket *p,uint32_t mask) {
 DegasGceAppend(p,DegasGceInst(0x02,0,~mask,0,false));
}
/* Logic ASSIGN into thread-local SPR0, then WRITE_S[_W_MASK] via SPR0.
 * Address-low bit1 denotes an indirect-address-high SPR, per helper-ext. */
static void DegasGceWrite(DegasGcePacket *p,uint32_t address,uint32_t value,uint32_t mask) {
 DegasGceAppend(p,DegasGceInst(0xa0,0,address>>16,0,true));
 if(mask!=UINT32_MAX)DegasGceMask(p,mask);
 DegasGceAppend(p,DegasGceInst(mask==UINT32_MAX?0x90:0x91,
   (uint16_t)address|2u,value,0,false));
}
/* Matches cmdq_pkt_poll_addr(..., reg_gpr=1). Uses GLOBAL GPR1 plus MASK/GPR0,
 * hence the runner requires all 32 GCE-D threads disabled before submission. */
static void DegasGceIdle(DegasGcePacket *p) {
 DegasGceMask(p,UINT32_C(0x80000000));
 DegasGceAppend(p,DegasGceInst(0x02,0,UINT32_C(0x1400d00c),1,true));
 DegasGceAppend(p,DegasGceInst(0x08,1,0,1,true));
}
static bool DegasGceBuildOneFrame(DegasGcePacket *p) {
 if(!p)return false;
 p->count=0;DegasGceIdle(p);
 DegasGceWrite(p,0x1400d190,2,2);DegasGceWrite(p,0x1400d190,4,4);
 DegasGceWrite(p,0x1400d008,0,UINT32_MAX);
 DegasGceWrite(p,0x1400d014,0,3);
 DegasGceWrite(p,0x1400d100,0x55,UINT32_MAX);
 DegasGceWrite(p,0x1400d104,1,1);
 DegasGceWrite(p,0x14201008,0,UINT32_MAX);DegasGceWrite(p,0x14201020,1,UINT32_MAX);
 DegasGceWrite(p,0x14601008,0,UINT32_MAX);DegasGceWrite(p,0x14601020,1,UINT32_MAX);
 DegasGceWrite(p,0x14001008,0,UINT32_MAX);DegasGceWrite(p,0x14001020,1,UINT32_MAX);
 DegasGceWrite(p,0x14401008,0,UINT32_MAX);DegasGceWrite(p,0x14401020,1,UINT32_MAX);
 DegasGceIdle(p);
 DegasGceWrite(p,0x1400dd00,0x002c3909,UINT32_MAX);
 DegasGceWrite(p,0x1400d060,0x8001,UINT32_MAX);
 DegasGceWrite(p,0x1400d000,0,1);DegasGceWrite(p,0x1400d000,1,1);
 DegasGceIdle(p);
 DegasGceAppend(p,DegasGceInst(0x40,0,0,0,false)); /* EOC, count++, no IRQ */
 DegasGceAppend(p,DegasGceInst(0x10,0,1,0,false)); /* pass +8, not a loop */
 return p->count<=DEGAS_GCE_MAX_WORDS;
}
/* PC/end encode both the GCE-MMINFRA aperture and 8-byte shift. This is a
 * controller DMA/IOVA address, NOT automatically the CPU physical address. */
static bool DegasGceAddress(uint64_t dma,uint32_t *encoded) {
 if(!encoded || (dma&7) || dma>=UINT64_C(0x400000000))return false; /* MT6897 IAS34 */
 *encoded=(uint32_t)((dma+UINT64_C(0x40000000))>>3);return true;
}
static bool DegasGcePoll(const DegasGceIo *io,uint32_t addr,uint32_t mask,uint32_t wanted,unsigned us) {
 for(unsigned i=0;i<us;i++){
  if((io->read32(io->context,addr)&mask)==wanted)return true;
  io->delay_us(io->context,1);
 }
 return false;
}
/* Only a selected, externally-owned thread is reset. Quiesced==false means
 * the caller MUST retain/quarantine command DMA storage until system reset. */
static bool DegasGceStop(const DegasGceIo *io,uint32_t thread_base) {
 io->write32(io->context,thread_base+8,1);
 bool suspended=DegasGcePoll(io,thread_base+0xc,2,2,1000);
 io->write32(io->context,thread_base,1);
 bool reset=DegasGcePoll(io,thread_base,1,0,100);
 io->write32(io->context,thread_base+4,0);
 return suspended && reset && !(io->read32(io->context,thread_base+4)&1);
}
static DegasGceResult DegasGceRunOneFrame(const DegasGceIo *io,const DegasGceConfig *cfg,
                                        const DegasGcePacket *packet) {
 DegasGceResult r={0};uint32_t pc,end,tb;DegasGcePacket expected;
 if(!io||!cfg||!packet||!io->read32||!io->write32||!io->delay_us||!io->publish ||
    cfg->proofs!=DEGAS_GCE_PROOFS||cfg->thread>=32||
    !(DEGAS_GCE_CANDIDATE_THREADS&(1u<<cfg->thread))||!cfg->timeout_us||cfg->timeout_us>200000 ||
    !packet->count||packet->count>DEGAS_GCE_MAX_WORDS||
    !DegasGceAddress(cfg->dma_address,&pc)||
    !DegasGceAddress(cfg->dma_address+(packet->count-1)*8,&end))return r;
 DegasGceBuildOneFrame(&expected);
 if(packet->count!=expected.count)return r;
 for(size_t i=0;i<expected.count;i++)if(packet->word[i]!=expected.word[i])return r;
 /* No global clock or power writes. Failed readiness never enables/resets GCE. */
 if((io->read32(io->context,0x1e800100)&5)||
    (io->read32(io->context,0x1e800110)&(1u<<17)))return r;
 if(io->read32(io->context,DEGAS_GCE_BASE+0x48)!=0x20002)return r;
 for(unsigned i=0;i<32;i++)if(io->read32(io->context,DEGAS_GCE_BASE+0x104+i*0x80)&1)return r;
 if(io->read32(io->context,0x1400d014)!=0 || (io->read32(io->context,0x1400d00c)&0x80000000u))return r;
 tb=DEGAS_GCE_BASE+0x100+cfg->thread*0x80;
 if(io->read32(io->context,tb+0x14)!=0 || io->read32(io->context,tb+0x10)!=0)return r;
 if(!io->publish(io->context,packet->word,packet->count*8,cfg->dma_address))return r;
 /* Publish callback is responsible for visibility/fence and verified DMA map. */
 io->write32(io->context,tb,1);
 if(!DegasGcePoll(io,tb,1,0,100))return r; /* no task enabled */
 io->write32(io->context,DEGAS_GCE_BASE+0x30,0x3200);
 io->write32(io->context,tb+0x50,0); /* kernel uses software timeout */
 io->write32(io->context,tb+0x40,0); /* priority0; no prefetch */
 io->write32(io->context,tb+0x24,end);io->write32(io->context,tb+0x20,pc);
 io->write32(io->context,tb+0x14,0); /* polling only, no CPU/GIC delivery */
 io->write32(io->context,tb+4,1);r.started=true;
 for(unsigned i=0;i<cfg->timeout_us;i++){
  r.pc=io->read32(io->context,tb+0x20);r.end=io->read32(io->context,tb+0x24);
  r.irq=io->read32(io->context,tb+0x10);r.elapsed_us=i;
  if(r.irq&0x12)break;
  if(r.pc==end){r.completed=true;break;}
  io->delay_us(io->context,1);
 }
 r.dsi_status=io->read32(io->context,0x1400d00c);
 r.quiesced=DegasGceStop(io,tb);
 return r;
}
#endif
