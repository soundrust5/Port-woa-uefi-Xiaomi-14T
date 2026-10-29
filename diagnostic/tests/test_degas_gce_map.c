// Test the actual table builder with an independent ARMv7s page-table walker.
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int BOOLEAN;
#define STATIC static
#define TRUE 1
#define FALSE 0
#include "../../mu/Platforms/Xiaomi/degasPkg/Include/GceMap.h"
static uint64_t walk(const uint32_t *root,const uint32_t *leaf,uint64_t iova){
 if(iova>=(1ULL<<34))return UINT64_MAX;
 uint32_t l1=root[iova>>20];
 if((l1&3)!=1)return UINT64_MAX;
 assert((l1&0xfffffc00U)==0x44450000U && (l1&8));
 uint32_t l2=leaf[(iova>>12)&255];
 if((l2&3)!=2)return UINT64_MAX;
 assert((l2&0xfff)==0xc42); // exact NC/S/NG, no AP extension collision
 return (l2&0xfffff000U)|(iova&0xfff);
}
int main(void){
 static struct {uint32_t pre,root[16384],post;} r;
 static struct {uint32_t pre,leaf[256],post;} l;
 memset(&r,0xa5,sizeof r);memset(&l,0xa5,sizeof l);
 assert(!DegasGceBuildMap(NULL,l.leaf));
 assert(!DegasGceBuildMap(r.root,NULL));
 assert(r.root[0]==0xa5a5a5a5 && l.leaf[0]==0xa5a5a5a5);
 assert(DegasGceBuildMap(r.root,l.leaf));
 assert(r.pre==0xa5a5a5a5 && r.post==r.pre && l.pre==r.pre && l.post==r.pre);
 unsigned valid=0;
 for(uint64_t i=0;i<16384;i++){
  if(r.root[i]){valid++;assert(i==0x444 && r.root[i]==0x44450009);}
 }
 assert(valid==1);
 for(unsigned i=0;i<256;i++)if(i!=0x51)assert(l.leaf[i]==0);
 for(uint64_t off=0;off<4096;off++)assert(walk(r.root,l.leaf,0x44451000+off)==0x44451000+off);
 assert(walk(r.root,l.leaf,0x44450fff)==UINT64_MAX);
 assert(walk(r.root,l.leaf,0x44452000)==UINT64_MAX);
 assert(walk(r.root,l.leaf,0x444fffff)==UINT64_MAX);
 assert(walk(r.root,l.leaf,0x107400000)==UINT64_MAX); // protected LK reservation excluded
 assert(walk(r.root,l.leaf,1ULL<<34)==UINT64_MAX);
 assert(DEGAS_GCE_ARENA_BASE+DEGAS_GCE_ARENA_SIZE==0x44470000);
 assert(DEGAS_GCE_ROOT_PA+65536<=DEGAS_GCE_LEAF_PA);
 assert(DEGAS_GCE_LEAF_PA+1024<=DEGAS_GCE_PACKET_PA);
 assert(DEGAS_GCE_PACKET_PA+4096<=DEGAS_GCE_ARENA_BASE+DEGAS_GCE_ARENA_SIZE);
 assert(DegasGceQueryPa(0x44451bfa)==0x244451000ULL);
 puts("PASS: ARMv7s single-page mapping, full-page independent walk, unmapped neighbors/reservations, bounds/canaries, extended query PA; NO HARDWARE");
}
