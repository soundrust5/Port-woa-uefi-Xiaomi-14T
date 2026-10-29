// Actual production packet, independent instruction decoder and temporal DSI.
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef size_t UINTN;typedef int BOOLEAN;
#define STATIC static
#define VOID void
#define TRUE 1
#define FALSE 0
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/GceFramePacket.h"
static const uint32_t addr[]={0x1400d190,0x1400d190,0x1400d008,0x1400d014,0x1400d100,0x1400d104,
 0x14201008,0x14201020,0x14601008,0x14601020,0x14001008,0x14001020,0x14401008,0x14401020,
 0x1400dd00,0x1400d060,0x1400d00c,0x1400d000,0x1400d000};
static const uint32_t values[]={2,4,0,0,0x55,1,0,1,0,1,0,1,0,1,0x002c3909,0x8001,0xffffffef,0,1};
static const uint32_t masks[]={2,4,~0u,3,~0u,1,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,~0u,1,1};
// Fault1 no fresh done, fault2 stale done cannot clear, fault3 very short BUSY,
// fault4 stuck BUSY after fresh done. All failed cases must stop before EOC.
static int decode(const uint64_t *p,unsigned fault){
 uint32_t inv=0,gpr=0,spr=0,status=0x16;unsigned writes=0,polls=0,tick=0,start=0;
 int launched=0,cleared=0,eoc=0;
 for(unsigned pc=0;pc<62;pc++,tick++){
  uint64_t w=p[pc];unsigned op=w>>56,a=(w>>32)&0xffff,sop=(w>>48)&31,at=(w>>55)&1;uint32_t v=w;
  assert(((w>>53)&3)==0);
  if(op==2){if(at){assert(!a && sop==14);gpr=v;}else{assert(!a&&!sop);inv=v;}}
  else if(op==0xa0){assert(at && !a && !sop);spr=v;}
  else if(op==0x90 || op==0x91){
   assert(!at && !sop && (a&3)==2 && writes<19);
   uint32_t target=(spr<<16)|(a&~3u),mask=op==0x90?~0u:~inv;
   assert(target==addr[writes] && v==values[writes] && mask==masks[writes]);
   if(target==0x1400d00c){assert(op==0x90 && v==0xffffffef && !launched);if(fault!=2)status&=v;}
   if(target==0x1400d000 && v==1){assert(cleared && !(status&16));launched=1;start=tick;}
   writes++;
  }else if(op==8){
   assert(at && sop==14 && a==1 && gpr==0x1400d00c);polls++;
   assert((~inv==0x80000000 && v==0) || (~inv==16 && (v==0||v==16)));
   for(;;){
    if(launched){
     unsigned age=tick-start;
     int busy=fault==3?(age>=1&&age<2):(age>=15&&(fault==4||age<70));
     status=(status&~0x80000000u)|(busy?0x80000000u:0);
     if(fault!=1 && age>=(fault==3?2u:50u))status|=16;
    }
    if((status&~inv)==v)break;
    if(++tick>1000){assert(!eoc);return 0;}
   }
   if(~inv==16 && v==0){assert(!launched && writes==17 && (status&6)==6);cleared=1;}
   if(~inv==16 && v==16)assert(launched && cleared);
  }else if(op==0x40){
   assert(w==0x4000000000000000ULL && cleared && launched && spr==0x76543210 && (status&0x80000010)==16);
   assert(writes==19 && polls==5 && pc==60);eoc=1;
  }else if(op==0x10){assert(w==0x1000000000000001ULL && pc==61 && eoc);}
  else assert(0);
 }
 return eoc;
}
int main(void){
 struct {uint64_t pre,p[62],post;} b={.pre=0x123,.post=0x456};
 assert(!GceBuildFrame(NULL,62) && !GceBuildFrame(b.p,61));
 assert(GceBuildFrame(b.p,62) && b.pre==0x123 && b.post==0x456);
 assert(b.p[42]==0xa080000000001400ULL && b.p[43]==0x9000d00effffffefULL);
 assert(b.p[45]==0x028e00001400d00cULL && b.p[54]==0x028e00001400d00cULL);
 assert(b.p[46]==0x088e000100000000ULL && b.p[55]==0x088e000100000010ULL);
 assert(b.p[59]==0xa080000076543210ULL && b.p[61]==0x1000000000000001ULL);
 assert(decode(b.p,0));assert(decode(b.p,3));
 assert(!decode(b.p,1));assert(!decode(b.p,2));assert(!decode(b.p,4));
 puts("PASS: actual62-word decode,19 exact writes/masks including W0C,5 polls,stale-clear/new-done/idle ordering,short BUSY success,missingdone/failedack/stuckbusy noEOC,bounds");
}
