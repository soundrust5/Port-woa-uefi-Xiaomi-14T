// Host test of the actual firmware helper, including fail-closed backup.
#define _GNU_SOURCE
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdatomic.h>
#include <sys/mman.h>
#define STATIC static
#define CONST const
#define VOID void
#define TRUE true
#define FALSE false
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef char CHAR8;
typedef bool BOOLEAN;
static void MemoryFence(void){atomic_thread_fence(memory_order_seq_cst);}
static unsigned char Captured[65536];static size_t CapturedBytes,Calls,FailAt=SIZE_MAX;
static UINTN SerialPortWrite(UINT8 *P,UINTN N){
 if(Calls++==FailAt)return 0;
 assert(CapturedBytes+N<=sizeof(Captured));memcpy(Captured+CapturedBytes,P,N);CapturedBytes+=N;return N;
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Include/ApJournal.h"
static void reset_log(size_t fail){CapturedBytes=Calls=0;FailAt=fail;}
static void save(const char *path,const void *data,size_t bytes){FILE *f=fopen(path,"wb");assert(f);assert(fwrite(data,1,bytes,f)==bytes);assert(fclose(f)==0);}
int main(int argc,char **argv){
 UINT32 Old[1024],First[1024],Next[1024],Check[1024];UINT8 *F=(UINT8 *)First,*N=(UINT8 *)Next;
 UINT32 *Zone;FILE *f;UINT64 Sequence;unsigned cpu;
 assert(argc==4);f=fopen(argv[3],"rb");assert(f);assert(fread(Old,1,4096,f)==4096);fclose(f);
 assert(DegasApJournalHeaderValid(Old));assert(!DegasApJournalSlotValid(Old,0));assert(!DegasApJournalSlotValid(Old,8));
 if(ApjEqual((UINT8 *)Old+0x100,mApjSlotMagic,8)){
  assert(DegasApJournalSlotValid(Old,1));memcpy(Check,Old,sizeof(Check));Check[(0x100+0x40)/4]^=1;
  assert(DegasApJournalHeaderValid(Check));assert(!DegasApJournalSlotValid(Check,1));
 }
 assert(ApjBuildCurrent(First,Old,0xd0000000ULL));assert(ApjGet32(F+0x9c)==1);assert(ApjGet64(F+0xa0)==1);
 assert(ApjEqual(F+0x50,mDegasApJournalBuildSeed,8));assert(ApjEqual(F+0x60,mDegasApJournalSourceHash,32));
 assert(ApjBuildCurrent(Next,First,0xd0000000ULL));assert(ApjGet64(N+0xa0)==2);assert(!ApjEqual(F+0x50,N+0x50,16));
 for(cpu=1;cpu<8;cpu++){
  UINT8 *S=F+cpu*256;
  assert(ApjZero(S,8));assert(ApjEqual(S+16,F+0x50,16));assert(ApjGet64(S+0x20)==(UINT64)cpu*256);
  assert(ApjGet64(S+0x30)==0xd0000000ULL+cpu*4096);assert(ApjZero(S+0x38,256-0x38));
 }
 // A malformed predecessor does not acquire journal validity from its bytes.
 Check[0]=0;memcpy(Check,First,sizeof(Check));Check[17]^=1;assert(!DegasApJournalHeaderValid(Check));
 assert(!ApjBuildCurrent(Check,Old,1));
 // Saturation is checked before increment; do not reuse an attempt UUID.
 memcpy(Check,First,sizeof(Check));ApjPut64((UINT8 *)Check+0xa0,0x3fffffffffffffffULL);
 {UINT8 *H=(UINT8 *)Check+0x40;UINT32 C=ApjCrc(H+8,0x70);ApjPut32(H+0x78,C);ApjPut32(H+0x7c,~C);}
 assert(DegasApJournalHeaderValid(Check));assert(!ApjBuildCurrent(Next,Check,0xd0000000ULL));
 Zone=mmap((void *)(uintptr_t)DEGAS_APJ_BASE,4096,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 assert(Zone==(void *)(uintptr_t)DEGAS_APJ_BASE);memcpy(Zone,Old,4096);
 reset_log(0);assert(!DegasApJournalInitialize(0xd0000000ULL));assert(memcmp(Zone,Old,4096)==0);
 reset_log(3);assert(!DegasApJournalInitialize(0xd0000000ULL));assert(memcmp(Zone,Old,4096)==0);
 reset_log(SIZE_MAX);assert(!DegasApJournalInitialize(1));assert(Calls==0);assert(memcmp(Zone,Old,4096)==0);
 reset_log(SIZE_MAX);assert(!DegasApJournalInitialize(0xffffffffffff8000ULL));assert(Calls==0);
 reset_log(SIZE_MAX);assert(DegasApJournalInitialize(0xd0000000ULL));assert(memcmp(Zone,First,4096)==0);
 assert(CapturedBytes>4096);save(argv[2],Captured,CapturedBytes);
 Sequence=ApjGet64((UINT8 *)Zone+0xa0);assert(Sequence==1);
 reset_log(SIZE_MAX);assert(DegasApJournalInitialize(0xd0000000ULL));assert(ApjGet64((UINT8 *)Zone+0xa0)==2);
 DegasApJournalRead(Check);assert(memcmp(Check,Zone,4096)==0);save(argv[1],Zone,4096);
 assert(munmap(Zone,4096)==0);puts("BSP journal helper checks passed");return 0;
}
