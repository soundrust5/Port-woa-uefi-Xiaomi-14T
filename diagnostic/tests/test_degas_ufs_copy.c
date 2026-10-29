#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define VOID void
typedef uint8_t UINT8;typedef uint64_t UINT64;typedef uintptr_t UINTN;
#define CopyMem memcpy
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsCopy.h"
int main(void){_Alignas(16) UINT8 a[65568],b[65568];UINTN i,s,d,n;UINTN sizes[]={0,1,7,8,9,15,16,4096,57344,65536};
for(i=0;i<sizeof(a);i++)a[i]=(UINT8)(i*19+i/256);
for(s=0;s<16;s++)for(d=0;d<16;d++)for(n=0;n<sizeof(sizes)/sizeof(sizes[0]);n++){
memset(b,0xa5,sizeof(b));UfsCopyRead(b+d,a+s,sizes[n]);assert(!memcmp(b+d,a+s,sizes[n]));
for(i=0;i<d;i++)assert(b[i]==0xa5);for(i=d+sizes[n];i<sizeof(b);i++)assert(b[i]==0xa5);
}puts("UFS copy alignment, tails and canaries PASS");}
