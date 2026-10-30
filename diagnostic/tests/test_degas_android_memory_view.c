// Exercise the actual map transformer, including extended EFI descriptor stride.
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <assert.h>
#include <stdio.h>
#define DEGAS_ANDROID_MAP_HOST_TEST
#define STATIC static
#define VOID void
typedef size_t UINTN;
typedef uint8_t UINT8;
typedef uint64_t UINT64;
typedef int EFI_STATUS;
typedef struct {uint32_t Type,Pad;uint64_t PhysicalStart,VirtualStart,NumberOfPages,Attribute;} EFI_MEMORY_DESCRIPTOR;
#define EFI_PAGE_SIZE 4096
#define MAX_UINT64 UINT64_MAX
#define MAX_UINTN SIZE_MAX
#define EFI_MEMORY_RUNTIME (1ULL<<63)
#define EFI_MEMORY_WB 8
#define EFI_COMPROMISED_DATA 1
#define EFI_OUT_OF_RESOURCES 2
#define EFI_BUFFER_TOO_SMALL 3
#define EFI_SUCCESS 0
#define EfiReservedMemoryType 0
#define EfiLoaderData 2
#define ZeroMem(p,n) memset(p,0,n)
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/AndroidMemoryView.h"
typedef struct {EFI_MEMORY_DESCRIPTOR D;uint64_t Extra;} EXT;
int main(void){
 EXT M[5],Saved[5];UINTN N;EFI_STATUS S;
 memset(M,0,sizeof(M));
 M[0].D=(EFI_MEMORY_DESCRIPTOR){.Type=7,.PhysicalStart=0x40000000,.NumberOfPages=0x100,.Attribute=8};
 M[1].D=(EFI_MEMORY_DESCRIPTOR){.PhysicalStart=ANDROID_FB_BASE,.NumberOfPages=(ANDROID_FB_EFI_END-ANDROID_FB_BASE)/4096,.Attribute=4};
 M[1].Extra=0xfeedface;
 M[2].D=(EFI_MEMORY_DESCRIPTOR){.Type=7,.PhysicalStart=0x140000000,.NumberOfPages=0x100,.Attribute=8};
 memcpy(Saved,M,sizeof(M));
 N=3*sizeof(EXT);S=AndroidProjectMemoryMap((void *)M,&N,N,sizeof(EXT));
 assert(S==EFI_BUFFER_TOO_SMALL && N==4*sizeof(EXT) && !memcmp(M,Saved,sizeof(M)));
 N=3*sizeof(EXT);S=AndroidProjectMemoryMap((void *)M,&N,sizeof(M),sizeof(EXT));
 assert(S==EFI_SUCCESS && N==4*sizeof(EXT));
 assert(!memcmp(M,Saved,sizeof(EXT)) && !memcmp(M+2,Saved+2,sizeof(EXT)));
 assert(M[1].D.Type==EfiLoaderData && M[1].D.Attribute==8 && M[1].Extra==0xfeedface);
 assert(M[3].D.PhysicalStart==ANDROID_FB_EFI_END && M[3].D.PhysicalStart+M[3].D.NumberOfPages*4096==ANDROID_FB_END);
 assert(M[3].D.Type==EfiLoaderData && M[3].D.Attribute==8 && M[3].Extra==0);
 // The four actual crash pages must now be covered by WB usable RAM.
 const uint64_t Crash[]={0xfda00000,0xfda1e000,0xfda98000,0xfdbff000};
 for(unsigned i=0;i<4;i++)assert(Crash[i]>=M[3].D.PhysicalStart && Crash[i]<ANDROID_FB_END);
 memcpy(M,Saved,sizeof(M));M[2].D.PhysicalStart=ANDROID_FB_EFI_END;
 memcpy(Saved,M,sizeof(M));N=3*sizeof(EXT);
 assert(AndroidProjectMemoryMap((void *)M,&N,sizeof(M),sizeof(EXT))==EFI_COMPROMISED_DATA && !memcmp(M,Saved,sizeof(M)));
 M[2].D.PhysicalStart=0x140000000;M[1].D.Type=7;N=3*sizeof(EXT);
 assert(AndroidProjectMemoryMap((void *)M,&N,sizeof(M),sizeof(EXT))==EFI_COMPROMISED_DATA);
 M[1].D.Type=0;M[1].D.Attribute=EFI_MEMORY_RUNTIME;N=3*sizeof(EXT);
 assert(AndroidProjectMemoryMap((void *)M,&N,sizeof(M),sizeof(EXT))==EFI_COMPROMISED_DATA);
 puts("PASS: actual crash pages mapped, retry nonmutating, stride preserved, overlap/type/runtime guards");
}
