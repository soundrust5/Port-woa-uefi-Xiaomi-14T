#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef uint64_t UINT64;typedef size_t UINTN;typedef int BOOLEAN;typedef void VOID;
#define STATIC static
#define CONST const
#define TRUE 1
#define FALSE 0
#include "../../mu/Platforms/Xiaomi/degasPkg/Include/WindowsObserverMap.h"
static UINT64 pool[32*512];
static UINT64 leaf(UINT64 pa,UINT64 root){
 UINT64 d=pool[(pa>>30)&511],*t;if(!d)return 0;
 assert((d&4095)==3);size_t n=((d&0x0000fffffffff000ULL)-root)/4096;assert(n<32);t=pool+n*512;
 d=t[(pa>>21)&511];if(!d)return 0;
 if((d&3)==1)return (d&~0x0000ffffffe00003ULL)|(pa&~4095ULL)|3;
 assert((d&4095)==3);n=((d&0x0000fffffffff000ULL)-root)/4096;assert(n<32);return pool[n*512+((pa>>12)&511)];
}
static UINT64 attr(UINT64 d){return d&~0x0000fffffffff003ULL;}
static void verify(UINT64 root,UINT64 code,const UINT64 mail[8],UINTN used){
 UINT64 mapped=0,blocks=0;
 for(unsigned a=0;a<512;a++){
  UINT64 d=pool[a];if(!d)continue;assert((d&4095)==3);UINTN n=((d&0x0000fffffffff000ULL)-root)/4096;assert(n<used);
  for(unsigned b=0;b<512;b++){
   d=pool[n*512+b];if(!d)continue;
   UINT64 base=((UINT64)a<<30)|((UINT64)b<<21);
   if((d&3)==1){assert(DegasWindowsObserverRam(base,0x200000));assert(attr(d)==DEGAS_WOM_WB);blocks++;mapped+=512;continue;}
   assert((d&4095)==3);UINTN k=((d&0x0000fffffffff000ULL)-root)/4096;assert(k<used);
   for(unsigned c=0;c<512;c++){
    UINT64 e=pool[k*512+c],pa=base+((UINT64)c<<12),expected=DEGAS_WOM_WB;if(!e)continue;
    assert((e&3)==3);assert((e&0x0000fffffffff000ULL)==pa);
    int special=pa==DEGAS_WINDOWS_OBSERVER_JOURNAL;
    for(unsigned j=0;j<8;j++)if(pa==mail[j])special=1;
    if(special)expected=DEGAS_WOM_DEVICE;
    else if(pa==code)expected=DEGAS_WOM_CODE;
    assert(special || DegasWindowsObserverRam(pa,4096));assert(attr(e)==expected);mapped++;
   }
  }
 }
 UINT64 expected=(0x44440000ULL-0x40480000ULL+0x1bfffe000ULL-0x140000000ULL+0x32d400000ULL-0x1c0000000ULL)/4096+1;
 assert(mapped==expected && blocks>1000);
 const UINT64 edges[]={0,0x4047f000,0x44440000,0x13ffff000ULL,0x1bfffe000ULL,0x1bffff000ULL,0x32d400000ULL,0xfffffffff000ULL};
 for(unsigned i=0;i<sizeof(edges)/sizeof(edges[0]);i++)assert(!leaf(edges[i],root));
 assert(attr(leaf(code,root))==DEGAS_WOM_CODE);
 for(unsigned i=0;i<8;i++)assert(attr(leaf(mail[i],root))==DEGAS_WOM_DEVICE);
 printf("mapped %llu exact 4K pages, %llu 2MiB blocks, %zu tables\n",(unsigned long long)mapped,(unsigned long long)blocks,used);
}
int main(void){
 DEGAS_WINDOWS_OBSERVER_MAP r;UINT64 root=0x43000000,code=0x43100000;
 UINT64 mail[8]={0x43200000,0x43400000,0x140000000,0x180000000,0x1c0000000,0x240000000,0x280000000,0x300000000};
 memset(pool,0,sizeof(pool));assert(DegasWindowsObserverBuild(pool,root,32,code,mail,&r));
 assert(r.Root==root && r.Tcr==0x100803019ULL && r.Mair==0xff0044);verify(root,code,mail,r.TablesUsed);
 #define BAD(p,pa,n,c,m) do{memset(pool,0,sizeof(pool));assert(!DegasWindowsObserverBuild(p,pa,n,c,m,&r));assert(!r.Root);}while(0)
 BAD(NULL,root,32,code,mail);BAD(pool,root,0,code,mail);BAD(pool,root,33,code,mail);BAD(pool,root+1,32,code,mail);
 BAD(pool,root,2,code,mail);BAD(pool,root,32,root,mail);BAD(pool,root,32,code+1,mail);BAD(pool,0x4443f000,32,code,mail);
 BAD(pool,root,32,0x480ae000,mail);BAD(pool,root,32,code,NULL);
 UINT64 old=mail[1];mail[1]=mail[0];BAD(pool,root,32,code,mail);mail[1]=old;
 old=mail[0];mail[0]=0x480ae000;BAD(pool,root,32,code,mail);mail[0]=old;
 memset(pool,0,sizeof(pool));pool[16000]=1;assert(!DegasWindowsObserverBuild(pool,root,32,code,mail,&r));
 puts("Windows observer map: PASS (permissions, bounds, holes, publication inputs)");return 0;
}
