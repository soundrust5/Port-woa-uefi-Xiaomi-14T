#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#define STATIC static
#define CONST const
#define VOID void
#define MAX_UINT64 UINT64_MAX
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef size_t UINTN;
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/CpuTopologyCore.h"
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#x);exit(1);}}while(0)
static UINT64 typers[16],addresses[16];static unsigned reads,n;
static UINT64 Read(VOID *ctx,UINT64 address){(void)ctx;CHECK(address>=0xc440008 && address<=0xc640000-8);reads++;
 for(unsigned i=0;i<n;i++)if(address==addresses[i]+8)return typers[i];return UINT64_MAX;
}
static void Setup(unsigned vlpi,unsigned rotation){UINT64 a=0xc440000;reads=0;n=8;
 for(unsigned i=0;i<8;i++){unsigned cpu=(i+rotation)%8;addresses[i]=a;typers[i]=((UINT64)cpu*0x100<<32)|((vlpi&(1u<<i))?2:0)|(i==7?0x10:0);a+=(typers[i]&2)?0x40000:0x20000;}
}
int main(void){UINT64 mpidrs[8],bases[8];for(unsigned i=0;i<8;i++)mpidrs[i]=i*0x100;
 for(unsigned flags=0;flags<256;flags++)for(unsigned rotation=0;rotation<8;rotation++){
  Setup(flags,rotation);CHECK(DegasGicrDiscover(Read,0,0xc440000,0x200000,mpidrs,8,bases)==0);CHECK(reads==8);
  for(unsigned i=0;i<8;i++)CHECK(bases[(i+rotation)%8]==addresses[i]);
 }
 Setup(0,0);typers[3]=(typers[3]&UINT32_MAX)|(typers[2]&0xffffffff00000000ULL);CHECK(DegasGicrDiscover(Read,0,0xc440000,0x200000,mpidrs,8,bases)!=0);
 Setup(0,0);typers[4]|=0x10;CHECK(DegasGicrDiscover(Read,0,0xc440000,0x200000,mpidrs,8,bases)!=0);
 Setup(0,0);typers[7]&=~0x10ULL;CHECK(DegasGicrDiscover(Read,0,0xc440000,0x100000,mpidrs,8,bases)!=0);CHECK(reads==8);
 Setup(0,0);CHECK(DegasGicrDiscover(Read,0,0xc440000,0x80000,mpidrs,8,bases)!=0);CHECK(reads==4);
 CHECK(DegasGicrDiscover(Read,0,UINT64_MAX-0xffff,0x200000,mpidrs,8,bases)!=0);
 mpidrs[7]=mpidrs[6];CHECK(DegasGicrDiscover(Read,0,0xc440000,0x200000,mpidrs,8,bases)!=0);
 CHECK(DegasGicrDiscover(0,0,0xc440000,0x200000,mpidrs,8,bases)!=0);
 CHECK(DegasGicrDiscover(Read,0,0xc440000,0x200000,mpidrs,0,bases)!=0);
 printf("PASS: %u GICR checks, actual bounded discovery function\n",checks);return 0;
}
