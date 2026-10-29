#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef size_t UINTN;
typedef int BOOLEAN;typedef void VOID;
#define STATIC static
#define TRUE 1
#define FALSE 0
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CpuObserverMap.h"
static UINT64 pool[8*512];
static UINT64 leaf(UINT64 pa,UINT64 root){
 UINT64 *t=pool,e;unsigned shifts[2]={30,21};
 for(unsigned i=0;i<2;i++){
  e=t[(pa>>shifts[i])&511];if(!e)return 0;
  assert((e&4095)==3);assert((e&~4095ULL)>=root);
  size_t n=((e&~4095ULL)-root)/4096;assert(n<8);t=pool+n*512;
 }
 return t[(pa>>12)&511];
}
static unsigned countLeaves(unsigned ti,unsigned depth,UINT64 root){
 unsigned n=0;
 for(unsigned i=0;i<512;i++){
  UINT64 e=pool[ti*512+i];if(!e)continue;
  if(depth==2){assert((e&3)==3);n++;}
  else {assert((e&4095)==3);unsigned ni=(unsigned)(((e&~4095ULL)-root)/4096);assert(ni<8);n+=countLeaves(ni,depth+1,root);}
 }
 return n;
}
int main(void){
 DEGAS_OBSERVER_MAP r;unsigned cases=0;
 const UINT64 layouts[][3]={{0x43000000,0x43001000,0x43002000},{0x43000000,0x43200000,0x43400000},{0x43000000,0x143000000,0x243000000},{0,0x1000,0xfffff0000ULL}};
 for(unsigned l=0;l<4;l++){
  UINT64 root=0x44000000;memset(pool,0,sizeof(pool));
  assert(DegasCpuObserverBuild(pool,root,8,layouts[l][0],layouts[l][1],layouts[l][2],&r));
  assert(r.Root==root && r.Tcr==0x100803019ULL && r.Mair==0xff0044);
  assert(r.TablesUsed<=7 && countLeaves(0,0,root)==3);
  for(unsigned j=0;j<3;j++){
   UINT64 e=leaf(layouts[l][j],root);assert((e&0x0000fffffffff000ULL)==layouts[l][j]);
   assert(e&(1ULL<<10));assert(e&(1ULL<<54));
   assert(((e>>2)&7)==j);assert(((e>>7)&1)==(j!=1));assert(((e>>53)&1)==(j!=0));
   cases++;
  }
 }
 #define BAD(p,pa,n,c,m,d) do {memset(pool,0,sizeof(pool));r.Root=123;assert(!DegasCpuObserverBuild(p,pa,n,c,m,d,&r));assert(!r.Root);cases++;}while(0)
 BAD(NULL,0x44000000,8,0x43000000,0x43001000,0x43002000);
 BAD(pool,0x44000001,8,0x43000000,0x43001000,0x43002000);
 BAD(pool,0x44000000,0,0x43000000,0x43001000,0x43002000);
 BAD(pool,0x44000000,9,0x43000000,0x43001000,0x43002000);
 BAD(pool,0x44000000,2,0x43000000,0x43001000,0x43002000);
 BAD(pool,0x44000000,8,0x43000000,0x43000000,0x43002000);
 BAD(pool,0x44000000,8,0x44007000,0x43001000,0x43002000);
 BAD(pool,0x44000000,8,0x43000001,0x43001000,0x43002000);
 BAD(pool,0xfffffff000ULL,8,0x43000000,0x43001000,0x43002000);
 BAD(pool,0x44000000,8,0x1000000000ULL,0x43001000,0x43002000);
 memset(pool,0,sizeof(pool));pool[4000]=1;
 assert(!DegasCpuObserverBuild(pool,0x44000000,8,0x43000000,0x43001000,0x43002000,&r));cases++;
 assert(!DegasCpuObserverBuild(pool,0x44000000,8,0x43000000,0x43001000,0x43002000,NULL));cases++;
 printf("observer map: %u checks PASS; only three leaves reachable\n",cases);return 0;
}
