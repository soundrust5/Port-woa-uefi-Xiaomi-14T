// BSP-only retained-journal preparation. Must run before any CPU_ON/release.
// Reserved UC physical mapping is a platform prerequisite, not established here.
#ifndef DEGAS_AP_JOURNAL_H
#define DEGAS_AP_JOURNAL_H
#include <Library/SerialPortLib.h>
#include "ApJournalIdentity.h"
#define DEGAS_APJ_BASE 0x480ae000ULL
#define DEGAS_APJ_WORDS 1024U
#define DEGAS_APJ_PAYLOAD 0x7f4U
#define DEGAS_APJ_SOURCE_HASH 1U
STATIC CONST UINT8 mApjHeaderMagic[8]={'D','G','A','P','J','0','0','1'};
STATIC CONST UINT8 mApjSlotMagic[8]={'D','G','A','P','S','0','0','1'};
STATIC CONST UINT8 mApjKmsg[15]={'=','=','=','=', '0','.', '0','0','0','0','0','0','-','D','\n'};
STATIC UINT32 ApjGet32(CONST UINT8 *P){return (UINT32)P[0]|((UINT32)P[1]<<8)|((UINT32)P[2]<<16)|((UINT32)P[3]<<24);}
STATIC UINT64 ApjGet64(CONST UINT8 *P){return (UINT64)ApjGet32(P)|((UINT64)ApjGet32(P+4)<<32);}
STATIC VOID ApjPut32(UINT8 *P,UINT32 V){UINTN I;for(I=0;I<4;I++)P[I]=(UINT8)(V>>(8*I));}
STATIC VOID ApjPut64(UINT8 *P,UINT64 V){UINTN I;for(I=0;I<8;I++)P[I]=(UINT8)(V>>(8*I));}
STATIC BOOLEAN ApjEqual(CONST UINT8 *A,CONST UINT8 *B,UINTN N){UINTN I;for(I=0;I<N;I++)if(A[I]!=B[I])return FALSE;return TRUE;}
STATIC BOOLEAN ApjZero(CONST UINT8 *P,UINTN N){UINTN I;for(I=0;I<N;I++)if(P[I])return FALSE;return TRUE;}
STATIC UINT32 ApjCrc(CONST UINT8 *P,UINTN N){
 UINT32 C=0xffffffffU;UINTN I;while(N--){C^=*P++;for(I=0;I<8;I++)C=(C>>1)^((0U-(C&1U))&0xedb88320U);}return C^0xffffffffU;
}
STATIC BOOLEAN ApjCrcValid(CONST UINT8 *P,UINTN Offset){
 UINT32 C=ApjGet32(P+Offset);return (C^ApjGet32(P+Offset+4))==0xffffffffU && ApjCrc(P+8,Offset-8)==C;
}
STATIC BOOLEAN DegasApJournalHeaderValid(CONST UINT32 *Words){
 CONST UINT8 *P=(CONST UINT8 *)Words,*H=P+0x40;
 if(ApjGet32(P)!=0x43474244U || ApjGet32(P+4)!=DEGAS_APJ_PAYLOAD || ApjGet32(P+8)!=DEGAS_APJ_PAYLOAD)return FALSE;
 if(!ApjEqual(P+12,mApjKmsg,15) || !ApjEqual(H,mApjHeaderMagic,8) || !ApjCrcValid(H,0x78))return FALSE;
 if(ApjGet32(H+8)!=1 || ApjGet32(H+12)!=128 || ApjZero(H+16,16))return FALSE;
 if((ApjGet64(H+0x40)&0xfff) || ApjGet32(H+0x48)!=7 || ApjGet32(H+0x4c)!=256 ||
    ApjGet64(H+0x50)!=DEGAS_APJ_BASE || ApjGet32(H+0x58)!=DEGAS_APJ_PAYLOAD ||
    ApjGet32(H+0x5c)>DEGAS_APJ_SOURCE_HASH || !ApjZero(H+0x68,16))return FALSE;
 return TRUE;
}
STATIC BOOLEAN DegasApJournalSlotValid(CONST UINT32 *Words,UINTN Cpu){
 CONST UINT8 *P=(CONST UINT8 *)Words,*H=P+0x40,*S;UINT64 Aff;
 if(Cpu<1 || Cpu>7 || !DegasApJournalHeaderValid(Words))return FALSE;
 S=P+Cpu*256;Aff=(UINT64)Cpu<<8;
 if(!ApjEqual(S,mApjSlotMagic,8) || !ApjCrcValid(S,0xf0) || ApjGet32(S+8)!=1 || ApjGet32(S+12)!=256)return FALSE;
 if(!ApjEqual(S+16,H+16,16) || ApjGet64(S+0x20)!=Aff || ApjGet64(S+0x28)!=Aff ||
    ApjGet64(S+0x30)!=ApjGet64(H+0x40)+Cpu*0x1000 || ApjGet64(S+0x38))return FALSE;
 if((ApjGet64(S+0x88)&0xff00ffffffULL)!=Aff || ApjGet64(S+0xa0)!=Aff || !ApjGet64(S+0xa8) ||
    !ApjZero(S+0xe8,8) || !ApjZero(S+0xf8,8))return FALSE;
 return TRUE;
}
STATIC VOID ApjHex(UINT8 *P,UINT64 V,UINTN Digits){
 STATIC CONST UINT8 Hex[]="0123456789abcdef";UINTN I;
 for(I=0;I<Digits;I++)P[I]=Hex[(V>>(4*(Digits-1-I)))&15];
}
STATIC UINTN ApjText(UINT8 *P,CONST CHAR8 *S){UINTN N=0;while(*S)P[N++]=(UINT8)*S++;return N;}
STATIC BOOLEAN ApjWrite(UINT8 *P,UINTN N){return SerialPortWrite(P,N)==N;}
// Exact raw byte dump; abort on a short serial write before changing the zone.
STATIC BOOLEAN DegasApJournalDumpRaw(CONST UINT32 *Words){
 CONST UINT8 *P=(CONST UINT8 *)Words;UINT8 Line[100];UINTN N,I,J;UINT32 C=ApjCrc(P,4096);
 N=ApjText(Line,"DEGAS APJ RAW BEGIN crc=");ApjHex(Line+N,C,8);N+=8;N+=ApjText(Line+N," bytes=4096\n");
 if(!ApjWrite(Line,N))return FALSE;
 for(I=0;I<4096;I+=16){
  N=ApjText(Line,"DEGAS APJ RAW ");ApjHex(Line+N,I,4);N+=4;Line[N++]=' ';
  for(J=0;J<16;J++){ApjHex(Line+N,P[I+J],2);N+=2;}Line[N++]='\n';
  if(!ApjWrite(Line,N))return FALSE;
 }
 N=ApjText(Line,"DEGAS APJ RAW END crc=");ApjHex(Line+N,C,8);N+=8;Line[N++]='\n';return ApjWrite(Line,N);
}
STATIC VOID DegasApJournalRead(UINT32 *Words){
 volatile CONST UINT32 *Zone=(volatile CONST UINT32 *)(UINTN)DEGAS_APJ_BASE;UINTN I;
 MemoryFence();for(I=0;I<DEGAS_APJ_WORDS;I++)Words[I]=Zone[I];MemoryFence();
}
STATIC BOOLEAN DegasApJournalReportAndDump(CONST UINT32 *Words){
 UINT8 Line[160];CONST UINT8 *P=(CONST UINT8 *)Words;UINTN N,I;UINT32 Mask=0;
 BOOLEAN Valid=DegasApJournalHeaderValid(Words);
 if(Valid)for(I=1;I<8;I++)if(DegasApJournalSlotValid(Words,I))Mask|=1U<<I;
 N=ApjText(Line,"DEGAS APJ PREVIOUS valid=");Line[N++]=Valid?'1':'0';N+=ApjText(Line+N," committed_mask=");
 ApjHex(Line+N,Mask,2);N+=2;
 if(Valid){N+=ApjText(Line+N," attempt=");for(I=0;I<16;I++){ApjHex(Line+N,P[0x50+I],2);N+=2;}}
 Line[N++]='\n';if(!ApjWrite(Line,N))return FALSE;
 return DegasApJournalDumpRaw(Words);
}
// Sequence uniqueness is scoped to a retained warm-reset chain and build seed.
// Regenerate the host seed for each test image, especially after RAM loss.
STATIC BOOLEAN ApjBuildCurrent(UINT32 *Words,CONST UINT32 *Previous,UINT64 MailboxBase){
 UINT8 *P=(UINT8 *)Words,*H=P+0x40;CONST UINT8 *Old=(CONST UINT8 *)Previous;UINTN I,J;
 UINT64 Sequence=1;UINT32 C;
 if(MailboxBase&0xfff)return FALSE;
 if(DegasApJournalHeaderValid(Previous) && ApjGet32(Old+0x9c)==DEGAS_APJ_SOURCE_HASH &&
    ApjEqual(Old+0x50,mDegasApJournalBuildSeed,8)){
  Sequence=ApjGet64(Old+0xa0)+1;if(!Sequence || Sequence>=0x4000000000000000ULL)return FALSE;
 }
 for(I=0;I<4096;I++)P[I]=0;
 for(I=0;I<15;I++)P[12+I]=mApjKmsg[I];
 ApjPut32(H+8,1);ApjPut32(H+12,128);
 for(I=0;I<8;I++)H[16+I]=mDegasApJournalBuildSeed[I];
 for(I=0;I<8;I++)H[24+I]=(UINT8)((Sequence|0x8000000000000000ULL)>>(8*(7-I)));
 for(I=0;I<32;I++)H[32+I]=mDegasApJournalSourceHash[I];
 ApjPut64(H+0x40,MailboxBase);ApjPut32(H+0x48,7);ApjPut32(H+0x4c,256);
 ApjPut64(H+0x50,DEGAS_APJ_BASE);ApjPut32(H+0x58,DEGAS_APJ_PAYLOAD);
 ApjPut32(H+0x5c,DEGAS_APJ_SOURCE_HASH);ApjPut64(H+0x60,Sequence);
 C=ApjCrc(H+8,0x70);ApjPut32(H+0x78,C);ApjPut32(H+0x7c,~C);
 for(I=0;I<8;I++)H[I]=mApjHeaderMagic[I];
 for(I=1;I<8;I++){
  UINT8 *S=P+I*256;ApjPut32(S+8,1);ApjPut32(S+12,256);
  for(J=0;J<16;J++)S[16+J]=H[16+J];
  ApjPut64(S+0x20,(UINT64)I<<8);ApjPut64(S+0x28,(UINT64)I<<8);ApjPut64(S+0x30,MailboxBase+I*0x1000);
 }
 ApjPut32(P+4,DEGAS_APJ_PAYLOAD);ApjPut32(P+8,DEGAS_APJ_PAYLOAD);ApjPut32(P,0x43474244U);
 return DegasApJournalHeaderValid(Words);
}
STATIC BOOLEAN DegasApJournalPublish(CONST UINT32 *Words){
 volatile UINT32 *Zone=(volatile UINT32 *)(UINTN)DEGAS_APJ_BASE;UINTN I;
 Zone[0]=0;MemoryFence();
 for(I=3;I<DEGAS_APJ_WORDS;I++)Zone[I]=Words[I];
 Zone[1]=Words[1];Zone[2]=Words[2];MemoryFence();Zone[0]=Words[0];MemoryFence();
 for(I=0;I<DEGAS_APJ_WORDS;I++)if(Zone[I]!=Words[I])return FALSE;return TRUE;
}
STATIC inline BOOLEAN DegasApJournalInitialize(UINT64 MailboxBase){
 STATIC UINT32 Previous[DEGAS_APJ_WORDS],Current[DEGAS_APJ_WORDS];
 // No read/write if the caller supplied an invalid mailbox region.
 if((MailboxBase&0xfff) || MailboxBase>0xffffffffffff7fffULL)return FALSE;
 DegasApJournalRead(Previous);
 if(!DegasApJournalReportAndDump(Previous) || !ApjBuildCurrent(Current,Previous,MailboxBase))return FALSE;
 return DegasApJournalPublish(Current);
}
#endif
