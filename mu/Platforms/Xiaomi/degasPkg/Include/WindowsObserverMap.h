// Immutable private TTBR0 tables for the qualified, read-only AP observer.
#ifndef DEGAS_WINDOWS_OBSERVER_MAP_H
#define DEGAS_WINDOWS_OBSERVER_MAP_H
#define DEGAS_WINDOWS_OBSERVER_MAX_TABLES 32U
#define DEGAS_WINDOWS_OBSERVER_TCR 0x100803019ULL
#define DEGAS_WINDOWS_OBSERVER_MAIR 0xff0044ULL
#define DEGAS_WINDOWS_OBSERVER_JOURNAL 0x480ae000ULL
#define DEGAS_WOM_PAGE 4096ULL
#define DEGAS_WOM_BLOCK 0x200000ULL
#define DEGAS_WOM_WB ((1ULL<<10)|(3ULL<<8)|(2ULL<<2)|(1ULL<<7)|(1ULL<<53)|(1ULL<<54))
#define DEGAS_WOM_CODE ((1ULL<<10)|(3ULL<<8)|(1ULL<<7)|(1ULL<<54))
#define DEGAS_WOM_DEVICE ((1ULL<<10)|(3ULL<<8)|(1ULL<<2)|(1ULL<<53)|(1ULL<<54))
typedef struct { UINT64 Root,Tcr,Mair;UINTN TablesUsed; } DEGAS_WINDOWS_OBSERVER_MAP;
typedef struct { UINT64 *Words,Pa;UINTN Capacity,Used; } DEGAS_WOM_BUILD;
STATIC BOOLEAN DegasWindowsObserverRam(UINT64 Pa,UINT64 Bytes){
 CONST UINT64 Begin[3]={0x40480000ULL,0x140000000ULL,0x1c0000000ULL};
 CONST UINT64 End[3]={0x44440000ULL,0x1bfffe000ULL,0x32d400000ULL};UINTN I;
 if(!Bytes)return FALSE;
 for(I=0;I<3;I++)if(Pa>=Begin[I] && Pa<End[I] && Bytes<=End[I]-Pa)return TRUE;
 return FALSE;
}
STATIC UINT64 *DegasWomNewTable(DEGAS_WOM_BUILD *B,UINT64 *Descriptor){
 UINT64 *T;if(B->Used>=B->Capacity)return NULL;
 T=B->Words+B->Used*512;*Descriptor=(B->Pa+(UINT64)B->Used*4096)|3ULL;B->Used++;return T;
}
STATIC UINT64 *DegasWomTable(DEGAS_WOM_BUILD *B,UINT64 D){
 UINT64 Pa=D&0x0000fffffffff000ULL;UINTN N;
 if((D&4095)!=3 || Pa<B->Pa)return NULL;
 N=(UINTN)((Pa-B->Pa)/4096);if(N>=B->Used)return NULL;return B->Words+N*512;
}
STATIC BOOLEAN DegasWomMap(DEGAS_WOM_BUILD *B,UINT64 Pa,UINT64 Attr,BOOLEAN Block,BOOLEAN Replace){
 UINT64 *L2,*L3,D,Base;UINTN I,J,K;
 I=(UINTN)((Pa>>30)&511);D=B->Words[I];
 L2=D?DegasWomTable(B,D):DegasWomNewTable(B,&B->Words[I]);if(!L2)return FALSE;
 J=(UINTN)((Pa>>21)&511);D=L2[J];
 if(Block){if(D)return FALSE;L2[J]=Pa|Attr|1ULL;return TRUE;}
 if(!D)L3=DegasWomNewTable(B,&L2[J]);
 else if((D&3)==1){
  // Split an existing WB block; preserve every original page permission.
  if(!Replace)return FALSE;
  L3=DegasWomNewTable(B,&L2[J]);if(!L3)return FALSE;
  Base=D&0x0000ffffffe00000ULL;
  for(K=0;K<512;K++)L3[K]=(Base+(UINT64)K*4096)|(D&~0x0000ffffffe00003ULL)|3ULL;
 }else L3=DegasWomTable(B,D);
 if(!L3)return FALSE;
 K=(UINTN)((Pa>>12)&511);
 if(L3[K] && !Replace)return FALSE;
 L3[K]=Pa|Attr|3ULL;return TRUE;
}
// Caller supplies allocated zeroed NVS pool and separately allocated code/mail
// pages. No UEFI calls, MMIO, architectural writes, or cache operations here.
// Caller must clean immutable tables/code to PoC before CPU_ON; BSP attributes
// must match code NC, mailbox/journal Device, and RAM WB Inner Shareable.
// On failure discard the partially built pool; output remains zero.
STATIC BOOLEAN DegasWindowsObserverBuild(VOID *Pool,UINT64 PoolPa,UINTN PoolPages,
 UINT64 CodePa,CONST UINT64 MailPa[8],DEGAS_WINDOWS_OBSERVER_MAP *Out){
 CONST UINT64 Begin[3]={0x40480000ULL,0x140000000ULL,0x1c0000000ULL};
 CONST UINT64 End[3]={0x44440000ULL,0x1bfffe000ULL,0x32d400000ULL};
 DEGAS_WOM_BUILD B;UINT64 Bytes,Pa;UINTN I,J;BOOLEAN Block;
 if(!Out)return FALSE;
 Out->Root=Out->Tcr=Out->Mair=0;Out->TablesUsed=0;
 if(!Pool || ((UINTN)Pool&7) || !MailPa || !PoolPages || PoolPages>32)return FALSE;
 Bytes=(UINT64)PoolPages*4096;
 if((PoolPa&4095) || !DegasWindowsObserverRam(PoolPa,Bytes) ||
    (CodePa&4095) || !DegasWindowsObserverRam(CodePa,4096) ||
    (CodePa>=PoolPa && CodePa-PoolPa<Bytes))return FALSE;
 for(I=0;I<8;I++){
  if((MailPa[I]&4095) || !DegasWindowsObserverRam(MailPa[I],4096) || MailPa[I]==CodePa ||
     (MailPa[I]>=PoolPa && MailPa[I]-PoolPa<Bytes))return FALSE;
  for(J=0;J<I;J++)if(MailPa[I]==MailPa[J])return FALSE;
 }
 B.Words=(UINT64 *)Pool;B.Pa=PoolPa;B.Capacity=PoolPages;B.Used=1;
 for(I=0;I<(UINTN)(Bytes/8);I++)if(B.Words[I])return FALSE;
 for(I=0;I<3;I++)for(Pa=Begin[I];Pa<End[I];){
  Block=((Pa&(DEGAS_WOM_BLOCK-1))==0 && End[I]-Pa>=DEGAS_WOM_BLOCK);
  if(!DegasWomMap(&B,Pa,DEGAS_WOM_WB,Block,FALSE))return FALSE;
  Pa+=Block?DEGAS_WOM_BLOCK:DEGAS_WOM_PAGE;
 }
 if(!DegasWomMap(&B,CodePa,DEGAS_WOM_CODE,FALSE,TRUE))return FALSE;
 for(I=0;I<8;I++)if(!DegasWomMap(&B,MailPa[I],DEGAS_WOM_DEVICE,FALSE,TRUE))return FALSE;
 if(!DegasWomMap(&B,DEGAS_WINDOWS_OBSERVER_JOURNAL,DEGAS_WOM_DEVICE,FALSE,FALSE))return FALSE;
 Out->Root=PoolPa;Out->Tcr=DEGAS_WINDOWS_OBSERVER_TCR;Out->Mair=DEGAS_WINDOWS_OBSERVER_MAIR;Out->TablesUsed=B.Used;return TRUE;
}
#endif
