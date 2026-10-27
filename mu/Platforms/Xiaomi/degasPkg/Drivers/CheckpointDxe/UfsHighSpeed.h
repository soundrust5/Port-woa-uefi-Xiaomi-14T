// MT6897: negotiate HS through G1 FASTAUTO (DT ufs-pmc-via-fastauto).
// Reference: evidence/sources/degas-ufs/{ufs-mediatek.c,ufshcd.c,unipro.h}.
// UIC operations only; no block writes. Fail closed after any partial transition.
STATIC BOOLEAN UfsDme(UINT32 Op,UINT16 Attr,UINT32 *Value){
 UINTN N;UINT32 Is=0,Result;
 if(!(MmioRead32(0x112b0030)&8) || MmioRead32(0x112b0058) || MmioRead32(0x112b0060))return FALSE;
 MicroSecondDelay(1000);
 MmioWrite32(0x112b0020,0x400);
 MmioWrite32(0x112b0094,(UINT32)Attr<<16);MmioWrite32(0x112b0098,0);
 MmioWrite32(0x112b009c,(Op==2 || Op==4)?*Value:0);
 MemoryFence();MmioWrite32(0x112b0090,Op);
 for(N=0;N<5000;N++){
  Is=MmioRead32(0x112b0020);if(Is&0x70c80)break;MicroSecondDelay(100);
 }
 Result=MmioRead32(0x112b0098)&255;
 if(Op==1 || Op==3)*Value=MmioRead32(0x112b009c);
 DEBUG((DEBUG_ERROR,"DEGAS UFS DME op=%u attr=%04x value=%08x result=%u is=%08x polls=%u\n",Op,Attr,*Value,Result,Is,(UINT32)N));
 MmioWrite32(0x112b0020,0x400);
 return (Is&0x400) && !(Is&0x70880) && !Result;
}
STATIC BOOLEAN UfsSet(UINT16 Attr,UINT32 Value){return UfsDme(2,Attr,&Value);}
STATIC BOOLEAN UfsPowerChange(UINT32 Mode){
 UINTN N;UINT32 Is=0,Hcs;
 MmioWrite32(0x112b0020,0x10);
 if(!UfsSet(0x1571,Mode))return FALSE;
 for(N=0;N<5000;N++){
  Is=MmioRead32(0x112b0020);if(Is&0x70890)break;MicroSecondDelay(100);
 }
 Hcs=MmioRead32(0x112b0030);
 DEBUG((DEBUG_ERROR,"DEGAS UFS POWER mode=%02x is=%08x hcs=%08x polls=%u\n",Mode,Is,Hcs,(UINT32)N));
 MmioWrite32(0x112b0020,0x10);
 return (Is&0x10) && !(Is&0x70880) && ((Hcs>>8)&7)==1;
}
STATIC BOOLEAN UfsHsParameters(UINT32 Rx,UINT32 Tx,UINT32 RxLanes,UINT32 TxLanes,UINT32 Adapt){
 STATIC CONST UINT16 A[]={0x15b0,0x15b1,0x15b2,0x15b3,0x15b4,0x15b5,0xd041,0xd042,0xd043};
 STATIC CONST UINT32 V[]={8191,65535,32767,8191,65535,32767,8191,65535,32767};UINTN I;
 if(!UfsSet(0x15d4,Adapt) || !UfsSet(0x1583,Rx) || !UfsSet(0x1580,RxLanes) ||
    !UfsSet(0x1584,1) || !UfsSet(0x1568,Tx) || !UfsSet(0x1560,TxLanes) ||
    !UfsSet(0x1569,1) || !UfsSet(0x156a,2))return FALSE;
 for(I=0;I<ARRAY_SIZE(A);I++)if(!UfsSet(A[I],V[I]))return FALSE;
 return TRUE;
}
STATIC BOOLEAN UfsHighSpeed(VOID){
 STATIC CONST UINT16 Snapshot[]={0x1571,0x1568,0x1583,0x1560,0x1580,0x156a,0x15a9};
 STATIC CONST UINT16 Sync[]={0x1552,0x1554,0x1556,0x15d0,0x15d6};
 UINT32 Rx=0,Tx=0,RxLanes=0,TxLanes=0,Version=0,V;UINTN I,Peer;
 for(I=0;I<ARRAY_SIZE(Snapshot);I++){V=0;if(!UfsDme(1,Snapshot[I],&V))return FALSE;}
 if(!UfsDme(1,0x1587,&Rx) || !UfsDme(3,0x1587,&Tx) ||
    !UfsDme(1,0x1581,&RxLanes) || !UfsDme(1,0x1561,&TxLanes) ||
    !UfsDme(1,0x15a9,&Version))return FALSE;
 if(!Rx || !Tx || !RxLanes || !TxLanes || RxLanes>2 || TxLanes>2)return FALSE;
 // ufshcd_get_pwr_dev_param chooses one common gear for both directions.
 Rx=MIN(MIN(Rx,Tx),5);Tx=Rx;
 DEBUG((DEBUG_ERROR,"DEGAS UFS HS target rx=%u tx=%u lanes=%u/%u series=B version=%x\n",Rx,Tx,RxLanes,TxLanes,Version));
 // MediaTek requires minimum sync length on BOTH ends before changing series.
 for(I=0;I<ARRAY_SIZE(Sync);I++)for(Peer=0;Peer<2;Peer++){
  V=0;if(!UfsDme(Peer?3:1,Sync[I],&V))return FALSE;
  if(V<0x48){V=0x48;if(!UfsDme(Peer?4:2,Sync[I],&V))return FALSE;}
 }
 if(!UfsHsParameters(1,1,RxLanes,TxLanes,3) || !UfsPowerChange(0x44))return FALSE;
 // UniPro 1.8+ needs initial adaptation for HS G4/G5, no adaptation below G4.
 if(!UfsHsParameters(Rx,Tx,RxLanes,TxLanes,(Tx>=4 && Version>=5)?1:3) || !UfsPowerChange(0x11))return FALSE;
 for(I=0;I<ARRAY_SIZE(Snapshot);I++){
  V=0;if(!UfsDme(1,Snapshot[I],&V))return FALSE;
  if((Snapshot[I]==0x1571 && V!=0x11) || (Snapshot[I]==0x1568 && V!=Tx) ||
     (Snapshot[I]==0x1583 && V!=Rx) || (Snapshot[I]==0x1560 && V!=TxLanes) ||
     (Snapshot[I]==0x1580 && V!=RxLanes) || (Snapshot[I]==0x156a && V!=2))return FALSE;
 }
 DEBUG((DEBUG_ERROR,"DEGAS UFS HS NEGOTIATED; DATA CRC STILL REQUIRED\n"));return TRUE;
}
