// Pure page-table construction for the explicit UEFI-only AP observer test.
// No MMIO, sysreg access or cache maintenance occurs here.
#ifndef DEGAS_CPU_OBSERVER_MAP_H
#define DEGAS_CPU_OBSERVER_MAP_H
#define DEGAS_OBSERVER_PAGE 4096ULL
#define DEGAS_OBSERVER_MAX_TABLES 8U
#define DEGAS_OBSERVER_MAIR 0x0000000000ff0044ULL
// T0SZ=25, TG0=4K, SH0=Inner, IRGN0/ORGN0=NC, EPD1=1, IPS=36-bit.
#define DEGAS_OBSERVER_TCR (25ULL | (3ULL<<12) | (1ULL<<23) | (1ULL<<32))
#define DEGAS_OBSERVER_PA_LIMIT (1ULL<<36)
typedef struct {
 UINT64 Root, Tcr, Mair;
 UINTN TablesUsed;
} DEGAS_OBSERVER_MAP;
STATIC BOOLEAN DegasObserverSpan(UINT64 Base,UINT64 Bytes){
 return Bytes!=0 && Base<DEGAS_OBSERVER_PA_LIMIT && Bytes<=DEGAS_OBSERVER_PA_LIMIT-Base;
}
STATIC BOOLEAN DegasObserverOverlap(UINT64 A,UINT64 ASize,UINT64 B,UINT64 BSize){
 return A<B+BSize && B<A+ASize;
}
// Pool must be caller-allocated, zeroed NVS RAM. All three pages must be
// separately allocated RAM, not arbitrary addresses. Code must fit one page.
// Before CPU_ON caller must clean the table pool/code to PoC and invalidate
// code I-cache; make BSP mailbox Device/UC and test data Normal WB shareable.
// These publication and allocation requirements cannot be verified by C here.
// Failure leaves output zero; partially constructed pool must not be installed.
STATIC BOOLEAN DegasCpuObserverBuild(VOID *Pool,UINT64 PoolPa,UINTN PoolPages,
 UINT64 CodePa,UINT64 MailPa,UINT64 DataPa,DEGAS_OBSERVER_MAP *Out){
 UINT64 Pages[3],Attrs[3],PoolBytes,*Words,*Table,Entry,NextPa;
 UINTN I,J,K,Used,Index,NextIndex;UINT32 Shifts[2]={30,21};
 if(!Out)return FALSE;
 Out->Root=Out->Tcr=Out->Mair=0;Out->TablesUsed=0;
 if(!Pool || ((UINTN)Pool&7) || !PoolPages || PoolPages>DEGAS_OBSERVER_MAX_TABLES)return FALSE;
 PoolBytes=(UINT64)PoolPages*DEGAS_OBSERVER_PAGE;
 if((PoolPa&4095) || !DegasObserverSpan(PoolPa,PoolBytes))return FALSE;
 Pages[0]=CodePa;Pages[1]=MailPa;Pages[2]=DataPa;
 for(I=0;I<3;I++){
  if((Pages[I]&4095) || !DegasObserverSpan(Pages[I],DEGAS_OBSERVER_PAGE) ||
     DegasObserverOverlap(Pages[I],DEGAS_OBSERVER_PAGE,PoolPa,PoolBytes))return FALSE;
  for(J=0;J<I;J++)if(Pages[I]==Pages[J])return FALSE;
 }
 Words=(UINT64 *)Pool;
 for(I=0;I<(UINTN)(PoolBytes/8);I++)if(Words[I])return FALSE;
 // All leaves: AF, Inner Shareable, EL0 inaccessible. Code is EL1 RX;
 // mailbox EL1 RW Device-nGnRnE; data EL1 RO WBWA, both data pages XN.
 Attrs[0]=(1ULL<<10)|(3ULL<<8)|(1ULL<<7)|(1ULL<<54);
 Attrs[1]=(1ULL<<10)|(3ULL<<8)|(1ULL<<2)|(1ULL<<53)|(1ULL<<54);
 Attrs[2]=(1ULL<<10)|(3ULL<<8)|(2ULL<<2)|(1ULL<<7)|(1ULL<<53)|(1ULL<<54);
 Used=1;
 for(I=0;I<3;I++){
  Table=Words;
  for(J=0;J<2;J++){
   Index=(UINTN)((Pages[I]>>Shifts[J])&511);Entry=Table[Index];
   if(!Entry){
    if(Used>=PoolPages)return FALSE;
    NextIndex=Used++;NextPa=PoolPa+(UINT64)NextIndex*DEGAS_OBSERVER_PAGE;
    Table[Index]=NextPa|3ULL;
   }else{
    if((Entry&4095)!=3 || Entry<PoolPa || (Entry&~4095ULL)>=PoolPa+PoolBytes)return FALSE;
    NextIndex=(UINTN)(((Entry&~4095ULL)-PoolPa)/DEGAS_OBSERVER_PAGE);
    if(NextIndex>=Used)return FALSE;
   }
   Table=Words+NextIndex*512;
  }
  K=(UINTN)((Pages[I]>>12)&511);
  if(Table[K])return FALSE;
  Table[K]=Pages[I]|Attrs[I]|3ULL;
 }
 Out->Root=PoolPa;Out->Tcr=DEGAS_OBSERVER_TCR;Out->Mair=DEGAS_OBSERVER_MAIR;Out->TablesUsed=Used;
 return TRUE;
}
#endif
