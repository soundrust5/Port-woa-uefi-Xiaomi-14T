// Read-only transport: fDeviceInit, READ CAPACITY(16), bounded READ(10) on LUN2.
// The only write-like query is transient fDeviceInit; no block-write command.
#include "UfsReadCommand.h"
STATIC BOOLEAN UfsProbePacket(CONST UINT8 *Packet,UINT32 Bytes){
 volatile UINT32 *T=(volatile UINT32 *)(UINTN)0x44470000;
 volatile UINT8 *C=(volatile UINT8 *)(UINTN)0x44471000;
 UINTN I;UINT32 Door=1,Is=0;BOOLEAN Good;STATIC UINT32 PacketCount;
 if(Bytes!=0 && Bytes!=32 && !DegasUfsReadValid(Packet,Bytes))return FALSE;
 if(Bytes==32 && (Packet[0]!=1 || Packet[1]!=0x40 || Packet[2]!=2 || Packet[16]!=0x9e || Packet[17]!=0x10))return FALSE;
 if(Bytes==0 && (Packet[0]!=0x16 || Packet[13]!=1 ||
    !((Packet[12]==6 && Packet[5]==0x81)||(Packet[12]==5 && Packet[5]==1))))return FALSE;
 if((MmioRead32(0x112b0030)&15)!=15 || MmioRead32(0x112b0024)!=0 ||
    MmioRead32(0x112b0050)!=0x44470000 || MmioRead32(0x112b0054)!=0 ||
    MmioRead32(0x112b0058)!=0 || MmioRead32(0x112b0060)!=0)return FALSE;
 // Reset only the UTRD and command/response/PRDT area. Clearing all 64 KiB
 // of uncached DMA on every request needlessly serializes the CPU. Data is
 // consumed only after a successful response with zero residual; the HS
 // preflight additionally checks every sector against the Android CRCs.
 for(I=0;I<8;I++)T[I]=0;
 for(I=0;I<0x120;I++)C[I]=0;
 for(I=0;I<32;I++)C[I]=Packet[I];
 T[0]=Bytes?0x15000000:0x11000000;T[2]=15;T[4]=0x44471000;T[6]=0x00200010;
 if(Bytes){
  volatile UINT32 *P=(volatile UINT32 *)(UINTN)0x44471100;
  T[7]=0x00400001;P[0]=0x44472000;P[1]=0;P[2]=0;P[3]=Bytes-1;
 }
 MemoryFence();MmioWrite32(0x112b0020,1);MmioWrite32(0x112b0060,1);
 if(!(MmioRead32(0x112b0060)&1))return FALSE;
 MemoryFence();MmioWrite32(0x112b0058,1);
 for(I=0;I<10000;I++){
  Door=MmioRead32(0x112b0058);Is=MmioRead32(0x112b0020);
  if(!(Door&1)||(Is&0x70880))break;MicroSecondDelay(100);
 }
 MemoryFence();Good=!(Door&1) && !(Is&0x70880) && (T[2]&255)==0;
 PacketCount++;
 if(!Good || PacketCount<=16 || (PacketCount%1024)==0)
  DEBUG((DEBUG_ERROR,"DEGAS UFS PACKET count=%u type=%02x tag=%02x bytes=%u ok=%u ocs=%08x door=%08x is=%08x polls=%u\n",PacketCount,Packet[0],Packet[3],Bytes,Good,T[2],Door,Is,(UINT32)I));
 MmioWrite32(0x112b0060,0);
 for(I=0;I<1000;I++){
  if(!(MmioRead32(0x112b0060)&1))return Good;MicroSecondDelay(100);
 }
 return FALSE;
}
STATIC BOOLEAN UfsInitFlag(UINT8 Opcode,UINT8 Tag,UINT8 *Value){
 UINT8 Q[32]={0};volatile UINT8 *R=(volatile UINT8 *)(UINTN)0x44471080;
 Q[0]=0x16;Q[3]=Tag;Q[5]=Opcode==6?0x81:1;Q[12]=Opcode;Q[13]=1;
 if(!UfsProbePacket(Q,0))return FALSE;
 DEBUG((DEBUG_ERROR,"DEGAS UFS INITFLAG opcode=%u type=%02x result=%02x value=%02x\n",Opcode,R[0],R[6],R[23]));
 if(R[0]!=0x36 || R[3]!=Tag || R[5]!=Q[5] || R[6] || R[8] || R[10] || R[11] ||
    R[12]!=Opcode || R[13]!=1 || R[14] || R[15] || R[20] || R[21] || R[22] || R[23]>1)return FALSE;
 *Value=R[23];return TRUE;
}
STATIC BOOLEAN UfsCapacityProbe(VOID){
 UINT8 V=1,Q[32]={0};UINTN I;UINT64 Last=0;UINT32 Block;
 volatile UINT8 *R=(volatile UINT8 *)(UINTN)0x44471080;
 volatile UINT8 *D=(volatile UINT8 *)(UINTN)0x44472000;
 if(!UfsInitFlag(6,0x60,&V))return FALSE;
 for(I=0;I<20;I++){
  if(!UfsInitFlag(5,(UINT8)(0x61+I),&V))return FALSE;
  if(V==0)break;MicroSecondDelay(10000);
 }
 if(V){DEBUG((DEBUG_ERROR,"DEGAS UFS INITFLAG not cleared\n"));return FALSE;}
 Q[0]=1;Q[1]=0x40;Q[2]=2;Q[15]=32;Q[16]=0x9e;Q[17]=0x10;Q[29]=32;
 for(I=0;I<3;I++){
  UINT32 SenseLen,DataLen,Residual;UINT8 Key=0xff,Asc=0xff,Ascq=0xff;
  Q[3]=(UINT8)(0x80+I);
  if(!UfsProbePacket(Q,32))return FALSE;
  DEBUG((DEBUG_ERROR,"DEGAS UFS CAPACITY response type=%02x tag=%02x resp=%02x scsi=%02x flags=%02x\n",R[0],R[3],R[6],R[7],R[1]));
  if(R[0]!=0x21 || R[2]!=2 || R[3]!=Q[3] || R[6]>1 || R[8])return FALSE;
  Residual=((UINT32)R[12]<<24)|((UINT32)R[13]<<16)|((UINT32)R[14]<<8)|R[15];
  if(R[7]==0){if(R[6] || (R[1]&0x60) || Residual)return FALSE;break;}
  if((R[1]&0x40) || Residual>32)return FALSE;
  SenseLen=((UINT32)R[32]<<8)|R[33];DataLen=((UINT32)R[10]<<8)|R[11];
  if(R[7]!=2 || SenseLen<3 || SenseLen>18 || DataLen!=SenseLen+2)return FALSE;
  if((R[34]&0x7f)==0x70 || (R[34]&0x7f)==0x71)Key=R[36]&15;
  if((R[34]&0x7f)==0x72 || (R[34]&0x7f)==0x73)Key=R[35]&15;
  if(SenseLen>=14 && ((R[34]&0x7f)==0x70 || (R[34]&0x7f)==0x71)){Asc=R[46];Ascq=R[47];}
  if(SenseLen>=4 && ((R[34]&0x7f)==0x72 || (R[34]&0x7f)==0x73)){Asc=R[36];Ascq=R[37];}
  DEBUG((DEBUG_ERROR,"DEGAS UFS CAPACITY sensekey=%02x asc=%02x ascq=%02x sensebytes=%u residual=%u\n",Key,Asc,Ascq,SenseLen,Residual));
  if(Key!=6)return FALSE; // Only consume bounded power-on Unit Attention.
 }
 if(I==3 || R[10] || R[11])return FALSE;
 for(I=0;I<8;I++)Last=(Last<<8)|D[I];
 Block=((UINT32)D[8]<<24)|((UINT32)D[9]<<16)|((UINT32)D[10]<<8)|D[11];
 DEBUG((DEBUG_ERROR,"DEGAS UFS CAPACITY lun=2 last=%Lx block=%u expected_last=03d95fff expected_block=4096\n",Last,Block));
 return Last==0x03d95fff && Block==4096;
}
