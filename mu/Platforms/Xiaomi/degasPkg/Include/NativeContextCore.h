// Exact-build native AP bootstrap experiment. No privileged firmware patch.
// All accesses pass through the caller's owned loader-memory domain.
#ifndef DEGAS_NATIVE_CONTEXT_CORE_H
#define DEGAS_NATIVE_CONTEXT_CORE_H
#include "NativeContextFixture.h"
typedef BOOLEAN (*DEGAS_CONTEXT_READ)(VOID *,UINT64,VOID *,UINTN);
typedef BOOLEAN (*DEGAS_CONTEXT_WRITE)(VOID *,UINT64,CONST VOID *,UINTN);
typedef struct {UINT64 Base,Pages;UINT32 Type;UINT64 Attributes;} DEGAS_CONTEXT_REGION;
typedef struct {
 DEGAS_CONTEXT_READ Read;DEGAS_CONTEXT_WRITE Write;VOID *Context;
 CONST DEGAS_CONTEXT_REGION *Regions;UINTN RegionCount;
} DEGAS_CONTEXT_DOMAIN;
typedef struct {UINT64 KernelBase,KernelVirtualBase;UINT32 Pages,MzCandidates,Matches,Status,WriteAttempted,Patched;} DEGAS_CONTEXT_RESULT;
enum {DegasContextOk=0,DegasContextBadMap=1,DegasContextBudget=2,
 DegasContextMissing=3,DegasContextAmbiguous=4,DegasContextReadFailed=5,
 DegasContextIdentityFailed=6,DegasContextBadMailbox=7,DegasContextWriteFailed=8,
 DegasContextProtected=9};
#define DEGAS_CONTEXT_SCAN_LIMIT 0x40000U
STATIC UINT16 Dctx16(CONST UINT8 *P){return (UINT16)(P[0]|((UINT16)P[1]<<8));}
STATIC UINT32 Dctx32(CONST UINT8 *P){return (UINT32)P[0]|((UINT32)P[1]<<8)|((UINT32)P[2]<<16)|((UINT32)P[3]<<24);}
STATIC UINT64 Dctx64(CONST UINT8 *P){return (UINT64)Dctx32(P)|((UINT64)Dctx32(P+4)<<32);}
STATIC BOOLEAN DctxEqual(CONST UINT8 *A,CONST UINT8 *B,UINTN N){while(N--)if(*A++!=*B++)return FALSE;return TRUE;}
STATIC BOOLEAN DctxLoader(UINT32 T){return T==1 || T==2;}
// Full coverage, including reads crossing adjacent loader descriptors. The map
// must be ordered and nonoverlapping; MMIO, free memory and reserved holes fail.
STATIC BOOLEAN DegasContextCovered(CONST DEGAS_CONTEXT_DOMAIN *D,UINT64 A,UINTN N){
 UINTN I;UINT64 End,Stop;
 if(!N || A>MAX_UINT64-N)return FALSE;End=A+N;
 for(I=0;I<D->RegionCount && A<End;I++){
  CONST DEGAS_CONTEXT_REGION *R=&D->Regions[I];
  if(!DctxLoader(R->Type) || (R->Attributes&0x2000ULL))continue;
  Stop=R->Base+R->Pages*4096;
  if(A>=R->Base && A<Stop)A=Stop<End?Stop:End;
 }
 return A==End;
}
STATIC BOOLEAN DegasContextWritable(CONST DEGAS_CONTEXT_DOMAIN *D,UINT64 A,UINTN N){
 UINTN I;UINT64 End,Stop;
 if(!DegasContextCovered(D,A,N))return FALSE;End=A+N;
 for(I=0;I<D->RegionCount && A<End;I++){
  CONST DEGAS_CONTEXT_REGION *R=&D->Regions[I];Stop=R->Base+R->Pages*4096;
  if(A>=R->Base && A<Stop){if(R->Attributes&0x22000ULL)return FALSE;A=Stop<End?Stop:End;}
 }
 return A==End;
}
STATIC BOOLEAN DctxRead(CONST DEGAS_CONTEXT_DOMAIN *D,UINT64 B,UINT32 O,VOID *P,UINTN N){
 if(B>MAX_UINT64-O || !DegasContextCovered(D,B+O,N))return FALSE;
 return D->Read(D->Context,B+O,P,N);
}
STATIC BOOLEAN DctxReadChecked(CONST DEGAS_CONTEXT_DOMAIN *D,UINT64 B,UINT32 O,VOID *P,UINTN N,BOOLEAN *IoError){
 if(B>MAX_UINT64-O || !DegasContextCovered(D,B+O,N))return FALSE;
 if(!D->Read(D->Context,B+O,P,N)){*IoError=TRUE;return FALSE;}
 return TRUE;
}
STATIC BOOLEAN DegasContextValidate(CONST DEGAS_CONTEXT_DOMAIN *D,UINT64 B,BOOLEAN *IoError){
 UINT8 H[0x800],Template[DEGAS_BRIDGE_TEMPLATE_SIZE],Cv[sizeof(mDegasBridgeNtCodeView)],Word[4],Debug[28];UINTN I;UINT64 Pointer,Virtual;
 STATIC CONST UINT8 TextSection[40]={0x2e,0x74,0x65,0x78,0x74,0,0,0,0x94,0xa7,0x46,0,0,0,0x20,0,0,0xa8,0x46,0,0,0xf6,0x0f,0,0,0,0,0,0,0,0,0,0,0,0,0,0x20,0,0,0x68};
 STATIC CONST UINT32 GuardRva[]={0x47f8f8,0x49834c,0x49835c,0x6261d8,0x4981b0,0x4981b8};
 STATIC CONST UINT32 GuardWord[]={0xf9400d02,0xaa0203e3,0xf9400022,0xd4000003,0x91088109,0x91028113};
 *IoError=FALSE;
 if(!DctxReadChecked(D,B,0,H,sizeof(H),IoError) || Dctx16(H)!=0x5a4d || Dctx32(H+0x3c)!=0x110)return FALSE;
 if(Dctx32(H+0x110)!=0x4550 || Dctx16(H+0x114)!=0xaa64 || Dctx16(H+0x116)!=34 ||
    Dctx16(H+0x124)!=0xf0 || Dctx16(H+0x128)!=0x20b || Dctx32(H+0x160)!=DEGAS_BRIDGE_NT_IMAGE_SIZE ||
    Dctx32(H+0x164)!=0x800 || !DctxEqual(H+0x128+0xf0+7*40,TextSection,40))return FALSE;
 if(Dctx32(H+0x128+160)!=0x371f8 || Dctx32(H+0x128+164)!=112)return FALSE;
 if(!DegasContextCovered(D,B,DEGAS_BRIDGE_NT_IMAGE_SIZE))return FALSE;
 if(!DctxReadChecked(D,B,0x371f8,Debug,sizeof(Debug),IoError) || Dctx32(Debug+12)!=2 ||
    Dctx32(Debug+16)!=sizeof(Cv) || Dctx32(Debug+20)!=0x4dc78)return FALSE;
 // The exact CodeView record and template are both required; an RSDS match
 // alone never authorizes a write. Physical relocation must name this image.
 if(!DctxReadChecked(D,B,0x4dc78,Cv,sizeof(Cv),IoError) || !DctxEqual(Cv,mDegasBridgeNtCodeView,sizeof(Cv)))return FALSE;
 if(!DctxReadChecked(D,B,DEGAS_BRIDGE_TEMPLATE_RVA,Template,sizeof(Template),IoError))return FALSE;
 // EFI descriptors name PHYSICAL backing. This relocated literal names the
 // kernel's separate high VIRTUAL address; never dereference that address.
 Pointer=Dctx64(Template+DEGAS_BRIDGE_RELOC_RVA-DEGAS_BRIDGE_TEMPLATE_RVA);
 if(Pointer<DEGAS_BRIDGE_RELOC_TARGET_RVA)return FALSE;
 Virtual=Pointer-DEGAS_BRIDGE_RELOC_TARGET_RVA;
 if(Virtual<0xffff800000000000ULL || (Virtual&4095) || Virtual>MAX_UINT64-DEGAS_BRIDGE_NT_IMAGE_SIZE)return FALSE;
 for(I=0;I<sizeof(Template);I++){
  if(I>=DEGAS_BRIDGE_RELOC_RVA-DEGAS_BRIDGE_TEMPLATE_RVA && I<DEGAS_BRIDGE_RELOC_RVA-DEGAS_BRIDGE_TEMPLATE_RVA+8)continue;
  if(Template[I]!=mDegasBridgeOriginalTemplate[I])return FALSE;
 }
 for(I=0;I<ARRAY_SIZE(GuardRva);I++)if(!DctxReadChecked(D,B,GuardRva[I],Word,4,IoError) || Dctx32(Word)!=GuardWord[I])return FALSE;
 return TRUE;
}
STATIC VOID DegasContextLocate(CONST DEGAS_CONTEXT_DOMAIN *D,DEGAS_CONTEXT_RESULT *Out){
 UINTN I;UINT64 Last=0,A,End;UINT8 Mz[2];BOOLEAN IoError;
 ZeroMem(Out,sizeof(*Out));Out->Status=DegasContextBadMap;
 if(!D || !D->Read || !D->Regions || !D->RegionCount || D->RegionCount>1024)return;
 for(I=0;I<D->RegionCount;I++){
  CONST DEGAS_CONTEXT_REGION *R=&D->Regions[I];
  if(!R->Pages || (R->Base&4095) || R->Pages>MAX_UINT64/4096 || R->Base>MAX_UINT64-R->Pages*4096 || (I && R->Base<Last))return;
  Last=R->Base+R->Pages*4096;
  if(DctxLoader(R->Type) && (R->Attributes&0x2000ULL)){Out->Status=DegasContextProtected;return;}
 }
 for(I=0;I<D->RegionCount;I++){
  CONST DEGAS_CONTEXT_REGION *R=&D->Regions[I];if(!DctxLoader(R->Type))continue;
  End=R->Base+R->Pages*4096;
  for(A=R->Base;A<End;A+=4096){
   if(Out->Pages==DEGAS_CONTEXT_SCAN_LIMIT){Out->Status=DegasContextBudget;Out->KernelBase=0;return;}
   Out->Pages++;
   if(!D->Read(D->Context,A,Mz,2)){Out->Status=DegasContextReadFailed;Out->KernelBase=0;return;}
   if(Dctx16(Mz)!=0x5a4d)continue;Out->MzCandidates++;
   if(DegasContextValidate(D,A,&IoError)){
    UINT8 Pointer[8];
    if(!DctxRead(D,A,DEGAS_BRIDGE_RELOC_RVA,Pointer,8)){Out->Status=DegasContextReadFailed;Out->KernelBase=0;return;}
    Out->Matches++;Out->KernelBase=A;Out->KernelVirtualBase=Dctx64(Pointer)-DEGAS_BRIDGE_RELOC_TARGET_RVA;
   }
   if(IoError){Out->Status=DegasContextReadFailed;Out->KernelBase=0;return;}
  }
 }
 Out->Status=Out->Matches==1?DegasContextOk:(Out->Matches?DegasContextAmbiguous:DegasContextMissing);
 if(Out->Matches!=1)Out->KernelBase=0;
}
STATIC BOOLEAN DegasContextMailboxValid(UINT64 B){return B && !(B&4095) && B<=0xffff8000ULL;}
STATIC VOID DegasContextPrefix(UINT64 Mailbox,UINT8 *Bytes){
 UINT32 W;UINTN I,J;
 for(I=0;I<14;I++){
  W=mDegasBridgePrefixFixedWords[I];
  if(I==DEGAS_BRIDGE_PREFIX_MOVZ_INDEX)W|=(UINT32)(Mailbox&0xffff)<<5;
  if(I==DEGAS_BRIDGE_PREFIX_MOVK_INDEX)W|=(UINT32)((Mailbox>>16)&0xffff)<<5;
  for(J=0;J<4;J++)Bytes[4*I+J]=(UINT8)(W>>(8*J));
 }
}
// Caller must first qualify native PSCI, full topology, OFF state and ACPI page
// publication. No map/type/identity failure can reach this write callback.
STATIC VOID DegasContextApply(CONST DEGAS_CONTEXT_DOMAIN *D,UINT64 Mailbox,DEGAS_CONTEXT_RESULT *Out){
 UINT8 Bytes[DEGAS_BRIDGE_PREFIX_SIZE],Check[DEGAS_BRIDGE_PREFIX_SIZE];BOOLEAN IoError;
 if(Out->Status || Out->Matches!=1 || !Out->KernelBase)return;
 if(!DegasContextMailboxValid(Mailbox)){Out->Status=DegasContextBadMailbox;return;}
 if(!DegasContextValidate(D,Out->KernelBase,&IoError)){Out->Status=IoError?DegasContextReadFailed:DegasContextIdentityFailed;return;}
 if(!DegasContextWritable(D,Out->KernelBase+DEGAS_BRIDGE_PREFIX_RVA,sizeof(Bytes))){Out->Status=DegasContextProtected;return;}
 DegasContextPrefix(Mailbox,Bytes);
 if(!D->Write){Out->Status=DegasContextWriteFailed;return;}
 // A failed write or readback can leave a partial instruction replacement.
 // WriteAttempted stays set; the firmware must stop and reset on this status.
 Out->WriteAttempted=1;
 if(!D->Write(D->Context,Out->KernelBase+DEGAS_BRIDGE_PREFIX_RVA,Bytes,sizeof(Bytes))){Out->Status=DegasContextWriteFailed;return;}
 if(!DctxRead(D,Out->KernelBase,DEGAS_BRIDGE_PREFIX_RVA,Check,sizeof(Check)) || !DctxEqual(Check,Bytes,sizeof(Check))){Out->Status=DegasContextWriteFailed;return;}
 Out->Patched=1;
}
#endif
