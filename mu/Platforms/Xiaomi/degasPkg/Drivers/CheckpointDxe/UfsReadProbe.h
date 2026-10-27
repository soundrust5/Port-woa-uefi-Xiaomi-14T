#include "UfsReadExpected.h"
STATIC UINT32 UfsCrc32(volatile UINT8 *P,UINTN Bytes){
 UINT32 C=0xffffffff;UINTN I,J;
 for(I=0;I<Bytes;I++){C^=P[I];for(J=0;J<8;J++)C=(C>>1)^((0u-(C&1))&0xedb88320);}
 return ~C;
}
STATIC UINT32 UfsLe32(volatile UINT8 *P){return P[0]|((UINT32)P[1]<<8)|((UINT32)P[2]<<16)|((UINT32)P[3]<<24);}
STATIC BOOLEAN UfsReadProbe(VOID){
 UINT8 Q[32]={0};UINTN L,I;UINT32 C,HSize,Stored,Calc;
 volatile UINT8 *R=(volatile UINT8 *)(UINTN)0x44471080,*D=(volatile UINT8 *)(UINTN)0x44472000;
 Q[0]=1;Q[1]=0x40;Q[2]=2;Q[14]=0x10;Q[16]=0x28;Q[24]=1;
 for(L=0;L<16;L++){
  Q[3]=(UINT8)(0xa0+L);Q[21]=(UINT8)L;
  if(!UfsProbePacket(Q,4096))return FALSE;
  if(R[0]!=0x21 || R[2]!=2 || R[3]!=Q[3] || R[6] || R[7] || R[8] ||
     R[10] || R[11] || (R[1]&0x60) || R[12] || R[13] || R[14] || R[15]){
   DEBUG((DEBUG_ERROR,"DEGAS UFS READ rejected lba=%u type=%02x resp=%02x scsi=%02x flags=%02x\n",(UINT32)L,R[0],R[6],R[7],R[1]));return FALSE;
  }
  C=UfsCrc32(D,4096);
  DEBUG((DEBUG_ERROR,"DEGAS UFS READ lba=%u crc=%08x expected=%08x\n",(UINT32)L,C,UfsReadExpected[L]));
  if(C!=UfsReadExpected[L])return FALSE;
  if(L==1){
   CONST UINT8 Sig[8]={'E','F','I',' ','P','A','R','T'};
   for(I=0;I<8;I++)if(D[I]!=Sig[I])return FALSE;
   HSize=UfsLe32(D+12);Stored=UfsLe32(D+16);
   if(HSize<92 || HSize>4096)return FALSE;
   for(I=16;I<20;I++)D[I]=0;Calc=UfsCrc32(D,HSize);
   for(I=0;I<4;I++)D[16+I]=(UINT8)(Stored>>(8*I));
   DEBUG((DEBUG_ERROR,"DEGAS UFS GPT primary_header bytes=%u stored=%08x calculated=%08x\n",HSize,Stored,Calc));
   if(Calc!=Stored)return FALSE;
  }
 }
 DEBUG((DEBUG_ERROR,"DEGAS UFS READ PASS blocks=16 bytes=65536 LUN=2 NO WRITES\n"));return TRUE;
}
