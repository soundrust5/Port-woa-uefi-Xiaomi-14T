#include <Uefi.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/RamProbeCore.h"

typedef struct {UINT64 *Memory;UINT32 Phase,Mode;UINT64 Calls;} MODEL;
static UINT32 Slot(UINT64 Address){
 assert(Address>=DEGAS_RAM_PROBE_BASE && Address+4096<=DEGAS_RAM_PROBE_BASE+DEGAS_RAM_PROBE_SIZE);
 assert((Address&4095)==0);
 if(Address==RamPageAddress(DEGAS_RAM_PROBE_COUNT-1))return DEGAS_RAM_PROBE_COUNT-1;
 assert((Address-DEGAS_RAM_PROBE_BASE)%DEGAS_RAM_PROBE_STRIDE==0);
 return (Address-DEGAS_RAM_PROBE_BASE)/DEGAS_RAM_PROBE_STRIDE;
}
static VOID Read(VOID *Context,UINT64 Address,UINT64 *Buffer){
 MODEL *M=Context;UINT32 Index=Slot(Address);M->Calls++;
 if(M->Mode==2 && Index==1)Index=0; // simulated physical address alias
 memcpy(Buffer,M->Memory+Index*512,4096);
 if(M->Mode==1 && M->Phase==3 && Index==2)Buffer[19]^=1;
}
static VOID Write(VOID *Context,UINT64 Address,CONST UINT64 *Buffer){
 MODEL *M=Context;UINT32 Index=Slot(Address);M->Calls++;
 if(M->Mode==2 && Index==1)Index=0;
 memcpy(M->Memory+Index*512,Buffer,4096);
 if(M->Mode==3 && M->Phase==5 && Index==2)M->Memory[Index*512+4]^=1;
}
static VOID Progress(VOID *Context,UINT32 Phase,UINT32 Page){
 MODEL *M=Context;M->Phase=Phase;assert(Page<DEGAS_RAM_PROBE_COUNT);
}
int main(void){
 const size_t Bytes=DEGAS_RAM_PROBE_COUNT*4096;
 UINT64 *Memory=malloc(Bytes),*Original=malloc(Bytes),*Backup=malloc(Bytes);
 UINT64 Work[512],Readback[512];DEGAS_RAM_RESULT R;UINT32 Mode;size_t I;
 assert(Memory && Original && Backup);
 for(I=0;I<Bytes/8;I++)Original[I]=0xd65fa23bULL*I+7;
 for(Mode=0;Mode<4;Mode++){
  MODEL M={Memory,0,Mode,0};DEGAS_RAM_IO Io={&M,Read,Write,Progress};
  memcpy(Memory,Original,Bytes);RamProbeRun(&Io,Backup,Work,Readback,&R);
  if(Mode==0)assert(R.Error==0 && R.Restored==1);
  if(Mode==1 || Mode==2)assert(R.Error==1 && R.Restored==1);
  if(Mode==3)assert(R.Error==2 && R.Restored==0);
  if(Mode!=3)assert(memcmp(Memory,Original,Bytes)==0);
  assert(M.Calls>=DEGAS_RAM_PROBE_COUNT*3);
 }
 free(Memory);free(Original);free(Backup);
 puts("RAM sparse bounds, patterns, alias/failure detection and restore PASS");return 0;
}
