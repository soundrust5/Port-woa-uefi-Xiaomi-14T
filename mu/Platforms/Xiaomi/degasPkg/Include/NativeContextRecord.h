// BSP-owned record in the existing retained ramoops slot30. The Android
// one-shot flag at +F00 is outside these 512 bytes and is never touched.
#ifndef DEGAS_NATIVE_CONTEXT_RECORD_H
#define DEGAS_NATIVE_CONTEXT_RECORD_H
#include "NativeContextIdentity.h"
STATIC UINT64 mNativeSequence;
STATIC UINT64 mNativeAccessPage,mNativeAccessPar;
#ifndef DEGAS_NATIVE_RECORD_ADDRESS
#define DEGAS_NATIVE_RECORD_ADDRESS 0x480ae000ULL
#endif
STATIC CONST UINT8 mNativeRecordMagic[8]={'N','C','T','X','0','0','0','1'};
STATIC UINT32 DctxCrc(CONST UINT8 *P,UINTN N){
 UINT32 C=0xffffffffU;UINTN I;while(N--){C^=*P++;for(I=0;I<8;I++)C=(C>>1)^((0U-(C&1U))&0xedb88320U);}return C^0xffffffffU;
}
STATIC VOID DctxPut32(UINT8 *P,UINT32 V){UINTN I;for(I=0;I<4;I++)P[I]=(UINT8)(V>>(I*8));}
STATIC VOID DctxPut64(UINT8 *P,UINT64 V){UINTN I;for(I=0;I<8;I++)P[I]=(UINT8)(V>>(I*8));}
STATIC BOOLEAN DctxRecordValid(CONST UINT8 *P){
 CONST UINT8 *B=P+0x40;
 return Dctx32(P)==0x43474244U && Dctx32(P+4)==0x1f4 && Dctx32(P+8)==0x1f4 &&
  DctxEqual(B,mNativeRecordMagic,8) && Dctx32(B+8)==1 && Dctx32(B+12)==256 &&
  Dctx32(B+240)==DctxCrc(B+8,232) && (Dctx32(B+240)^Dctx32(B+244))==0xffffffffU;
}
STATIC VOID DegasNativeRecord(UINT32 Stage,CONST DEGAS_NATIVE_CONTEXT_PROTOCOL *P,
 CONST DEGAS_CONTEXT_RESULT *R,UINT32 Descriptors,UINT64 MapStatus){
 UINT32 Words[128],Previous[128];UINT8 *Bytes=(UINT8 *)Words,*B=Bytes+0x40;
 volatile UINT32 *Zone=(volatile UINT32 *)(UINTN)DEGAS_NATIVE_RECORD_ADDRESS;UINTN I;UINT32 C;
 if(!mNativeSequence){
  MemoryFence();for(I=0;I<128;I++)Previous[I]=Zone[I];MemoryFence();
  mNativeSequence=1;
  if(DctxRecordValid((CONST UINT8 *)Previous) &&
     DctxEqual((CONST UINT8 *)Previous+0x50,mDegasNativeBuildId,16)){
   mNativeSequence=Dctx64((CONST UINT8 *)Previous+0x80)+1;
   if(!mNativeSequence)mNativeSequence=1;
  }
 }
 ZeroMem(Words,sizeof(Words));
 CopyMem(Bytes+12,"====0.000000-D\n",15);
 CopyMem(B+16,mDegasNativeBuildId,16);CopyMem(B+32,mDegasNativeSourceHash,32);
 DctxPut32(B+8,1);DctxPut32(B+12,256);DctxPut64(B+64,mNativeSequence);
 DctxPut32(B+72,Stage);DctxPut32(B+76,P->Mode);DctxPut64(B+80,P->MailboxBase);
 if(R){DctxPut64(B+88,R->KernelBase);DctxPut64(B+96,R->KernelVirtualBase);
  DctxPut32(B+104,R->Status);DctxPut32(B+108,R->Pages);DctxPut32(B+112,R->MzCandidates);
  DctxPut32(B+116,R->Matches);DctxPut32(B+120,R->WriteAttempted);DctxPut32(B+124,R->Patched);}
 DctxPut32(B+128,P->CpuCount);DctxPut32(B+132,Descriptors);DctxPut64(B+136,MapStatus);
 DctxPut64(B+144,GetPerformanceCounter());
 DctxPut64(B+152,mNativeAccessPage);DctxPut64(B+160,mNativeAccessPar);
 C=DctxCrc(B+8,232);DctxPut32(B+240,C);DctxPut32(B+244,~C);CopyMem(B,mNativeRecordMagic,8);
 Words[1]=Words[2]=0x1f4;Words[0]=0x43474244U;
 Zone[0]=0;MemoryFence();for(I=3;I<128;I++)Zone[I]=Words[I];
 Zone[1]=Words[1];Zone[2]=Words[2];MemoryFence();Zone[0]=Words[0];MemoryFence();
}
#endif
