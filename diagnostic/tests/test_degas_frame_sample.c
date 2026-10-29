#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define STATIC static
#define TRUE 1
#define FALSE 0
#define MAX_UINTN SIZE_MAX
typedef int BOOLEAN;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/FrameSample.h"
int main(void){
 const UINT32 w=1220,h=2712,p=1248;
 const size_t n=(size_t)p*h,bytes=n*4;
 UINT32 *mem=malloc(bytes),*before=malloc(bytes);assert(mem&&before);
 DEGAS_FRAME_SAMPLE a,b;
 for(size_t i=0;i<n;i++)mem[i]=0xff000000;
 memcpy(before,mem,bytes);
 assert(DegasFrameSample(mem,bytes,w,h,p,&a));
 assert(a.Pixels==1536&&a.OpaqueAlpha==1536&&a.ZeroAlpha==0&&a.Nonblack==0);
 assert(!memcmp(mem,before,bytes));
 mem[(size_t)(h-1)*p+w-1]=0x00010203;
 assert(DegasFrameSample(mem,bytes,w,h,p,&b));
 assert(a.RgbHash!=b.RgbHash&&a.AlphaHash!=b.AlphaHash);
 assert(b.OpaqueAlpha==1535&&b.ZeroAlpha==1&&b.Nonblack==1);
 a=b;mem[w]=0x12345678; // Row padding must not contribute.
 assert(DegasFrameSample(mem,bytes,w,h,p,&b));assert(!memcmp(&a,&b,sizeof(a)));
 mem[1]=0x11223344; // Deliberately unsampled pixel: unchanged sample is inconclusive.
 assert(DegasFrameSample(mem,bytes,w,h,p,&b));assert(!memcmp(&a,&b,sizeof(a)));
 assert(!DegasFrameSample(mem,bytes-1,w,h,p,&b));
 assert(!DegasFrameSample(mem,bytes,w,h,w-1,&b));
 assert(!DegasFrameSample(mem,bytes,0,h,p,&b));
 assert(!DegasFrameSample(mem,SIZE_MAX,UINT32_MAX,UINT32_MAX,UINT32_MAX,&b));
 assert(!DegasFrameSample(NULL,bytes,w,h,p,&b));
 assert(!DegasFrameSample((void *)((char *)mem+1),bytes,w,h,p,&b));
 free(before);free(mem);
 puts("PASS: framebuffer sampling bounds, last pixel, alpha/RGB separation, padding, no writes, spatial blind spot");
}
