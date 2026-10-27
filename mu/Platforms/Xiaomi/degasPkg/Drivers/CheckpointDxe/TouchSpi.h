/* First bounded chip-ID query, FIFO only. No sensor firmware writes or DMA.
 * Register definitions: captured MiCode spi-mt65xx.c / clk-mt6897*.c.
 * Uses independent 26MHz clock parent at 1MHz SCK (13+13 clocks).
 */
#define DEGAS_TOUCH_UEFI_TYPES
typedef UINT8 uint8_t;
typedef UINT16 uint16_t;
typedef UINT32 uint32_t;
typedef UINTN size_t;
#include "../../../../../../diagnostic/degas/touch/mtk_spi_fifo.h"
#include "../../../../../../diagnostic/degas/touch/fts_protocol.h"
#include "../../../../../../diagnostic/degas/touch/fts_contact.h"
STATIC UINT32 TouchSpiRead(VOID *C,UINT32 O){return MmioRead32(0x11012000+O);}
STATIC VOID TouchSpiWrite(VOID *C,UINT32 O,UINT32 V){MmioWrite32(0x11012000+O,V);}
STATIC VOID TouchSpiBarrier(VOID *C){ArmDataSynchronizationBarrier();}
STATIC VOID TouchSpiDelay(VOID *C,UINT32 U){MicroSecondDelay(U);}
STATIC UINT32 TouchSpiMessage(DEGAS_SPI_IO *Io,UINT32 Cmd,UINT8 *Tx,UINT8 *Rx,UINTN N){
 UINT32 E;
 TouchSpiWrite(NULL,0x18,Cmd|(1u<<7));TouchSpiBarrier(NULL);
 E=DegasSpiFifoOnce(Io,Tx,Rx,N,20000);
 TouchSpiWrite(NULL,0x18,Cmd);TouchSpiBarrier(NULL);
 TouchSpiWrite(NULL,0x18,Cmd|4);TouchSpiBarrier(NULL);
 TouchSpiWrite(NULL,0x18,Cmd);TouchSpiBarrier(NULL);MicroSecondDelay(150);
 return E;
}
STATIC BOOLEAN TouchRawModeRead(DEGAS_SPI_IO *Io,UINT32 Cmd,UINT8 *Value){
 UINT8 Tx[10],Rx[10]={0};
 DegasFtsReadFrame(0x9e,1,Tx,sizeof(Tx));
 return TouchSpiMessage(Io,Cmd,Tx,Rx,sizeof(Tx))==0 && DegasFtsReadReply(Rx,sizeof(Rx),1,Value,1);
}
STATIC BOOLEAN TouchRawModeWrite(DEGAS_SPI_IO *Io,UINT32 Cmd,UINT8 Value){
 /* fts_enable_touch_raw: 9E=41 enable, 9E=00 disable, followed by readback.
  * Single-register frame from captured fts_write: addr,00,BE length,dummy3,data. */
 UINT8 Tx[8]={0x9e,0,0,1,0,0,0,0},Rx[8]={0},Readback=0xff;
 UINT32 E;
 if(Value!=0 && Value!=0x41)return FALSE;
 Tx[7]=Value;E=TouchSpiMessage(Io,Cmd,Tx,Rx,sizeof(Tx));MicroSecondDelay(1000);
 return !E && !(Rx[3]&0xa0) && TouchRawModeRead(Io,Cmd,&Readback) && Readback==Value;
}
STATIC VOID TouchSpiCapture(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,DEGAS_SPI_IO *Io,UINT32 BaseCmd){
 UINT8 Tx[25],Rx[25],Data[16],Last[16];
 UINT32 Good=0,Bad=0,Changes=0,Logged=0,IrqLow=0,Transfer;UINTN K;
 UINT64 Start=GetPerformanceCounter();BOOLEAN HaveLast=FALSE;
 UINT8 RawMode=0xff;BOOLEAN ModeSaved,Changed=FALSE;
 UINT32 Contacts=0,Releases=0,Invalid=0;
 EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black={0,0,0,0},Green={0,255,0,0};
 ModeSaved=TouchRawModeRead(Io,BaseCmd,&RawMode);
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH RAW MODE read_ok=%u value=%02x\n",ModeSaved,RawMode));
 if(!ModeSaved || (RawMode!=0 && RawMode!=0x41)){
  Label(Gop,1840,"RAW MODE UNKNOWN SEE LOG");return;
 }
 if(RawMode==0x41){
  Changed=TRUE;
  if(!TouchRawModeWrite(Io,BaseCmd,0)){
   BOOLEAN Restored=TouchRawModeWrite(Io,BaseCmd,RawMode);
   DEBUG((DEBUG_ERROR,"DEGAS TOUCH RAW MODE disable failed restore=%u\n",Restored));
   Label(Gop,1840,"RAW MODE CHANGE FAILED");return;
  }
 }
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH RAW MODE capture value=00 changed=%u\n",Changed));
 Start=GetPerformanceCounter();
 Gop->Blt(Gop,&Black,EfiBltVideoFill,0,0,0,0,Gop->Mode->Info->HorizontalResolution,Gop->Mode->Info->VerticalResolution,0);
 Label(Gop,100,"DRAW WITH ONE FINGER 20 SECONDS");
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH PACKET BEGIN addr01 bytes16 TYPE2 ONE FINGER\n"));
 while(GetTimeInNanoSecond(GetPerformanceCounter()-Start)<20000000000ULL){
  BootWatchdogFeed();
  if(!(MmioRead32(0x10005200)&(1u<<9)))IrqLow++;
  DegasFtsReadFrame(1,16,Tx,sizeof(Tx));SetMem(Rx,sizeof(Rx),0);
  TouchSpiWrite(NULL,0x18,BaseCmd|(1u<<7));TouchSpiBarrier(NULL);
  Transfer=DegasSpiFifoOnce(Io,Tx,Rx,sizeof(Tx),20000);
  TouchSpiWrite(NULL,0x18,BaseCmd);TouchSpiBarrier(NULL);
  TouchSpiWrite(NULL,0x18,BaseCmd|4);TouchSpiBarrier(NULL);
  TouchSpiWrite(NULL,0x18,BaseCmd);TouchSpiBarrier(NULL);
  if(!Transfer && DegasFtsReadReply(Rx,sizeof(Rx),16,Data,sizeof(Data))){
   DEGAS_FTS_REPORT Report;
   Good++;
   if(DegasFtsReport2(Data,sizeof(Data),&Report) && Report.count==1){
    DEGAS_FTS_CONTACT *C=&Report.contacts[0];
    if(C->event==1)Releases++;
    else{
     UINTN X=(UINTN)C->x*Gop->Mode->Info->HorizontalResolution/122000;
     UINTN Y=(UINTN)C->y*Gop->Mode->Info->VerticalResolution/271200;
     UINTN W=MIN(9,Gop->Mode->Info->HorizontalResolution-X);
     UINTN H=MIN(9,Gop->Mode->Info->VerticalResolution-Y);
     Contacts++;Gop->Blt(Gop,&Green,EfiBltVideoFill,0,0,X,Y,W,H,0);
    }
   }else Invalid++;
   if(!HaveLast || CompareMem(Last,Data,sizeof(Data))){
    Changes++;
    if(Logged<64){
     DEBUG((DEBUG_ERROR,"DEGAS TOUCH PACKET n=%u data=",Good));
     for(K=0;K<sizeof(Data);K++)DEBUG((DEBUG_ERROR,"%02x",Data[K]));
     DEBUG((DEBUG_ERROR,"\n"));Logged++;
    }
    CopyMem(Last,Data,sizeof(Data));HaveLast=TRUE;
   }
  }else Bad++;
  MicroSecondDelay(20000);
 }
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH PACKET END good=%u bad=%u changes=%u irq_low=%u\n",Good,Bad,Changes,IrqLow));
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH COORDS contacts=%u releases=%u rejected=%u\n",Contacts,Releases,Invalid));
 if(Changed){
  BOOLEAN Restored=TouchRawModeWrite(Io,BaseCmd,RawMode);
  DEBUG((DEBUG_ERROR,"DEGAS TOUCH RAW MODE restore=%u value=%02x\n",Restored,RawMode));
  if(!Restored){Label(Gop,1840,"RAW MODE RESTORE FAILED");return;}
 }
 Label(Gop,1840,Good?"PACKETS SAVED CHECK LOG":"PACKET READ FAILED");
}
STATIC UINT32 TouchSpiIdentify(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 DEGAS_SPI_IO Io={NULL,TouchSpiRead,TouchSpiWrite,TouchSpiBarrier,TouchSpiDelay};
 UINT32 Mux=MmioRead32(0x10000130),Gate=MmioRead32(0x11036010),Error=0;
 UINT32 Saved[6],Offsets[]={0,4,0x18,0x24,0x28,0x40};
 CONST UINT32 BaseCmd=(3u<<12)|(1u<<18)|(1u<<19)|(3u<<22);
 UINT8 Tx[10],Rx[10],Id[2]={0};UINTN I,J;BOOLEAN Host=FALSE;
 // Require the four dedicated SPI pins already assigned by LK.
 if((MmioRead32(0x10005310)&0x0ffff000)!=0x01111000)return 20;
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH SPI clock_before mux=%08x gate=%08x\n",Mux,Gate));
 // Gate SPI2 while changing only its parent/power-down field.
 MmioWrite32(0x11036024,1u<<14);ArmDataSynchronizationBarrier();
 MmioWrite32(0x10000138,0x87000000);MmioWrite32(0x1000000c,1u<<13);
 ArmDataSynchronizationBarrier();MicroSecondDelay(10);
 MmioWrite32(0x11036028,1u<<14);ArmDataSynchronizationBarrier();MicroSecondDelay(10);
 if((MmioRead32(0x10000130)&0x87000000) || (MmioRead32(0x11036010)&(1u<<14))){Error=21;goto RestoreClock;}
 for(I=0;I<ARRAY_SIZE(Offsets);I++)Saved[I]=TouchSpiRead(NULL,Offsets[I]);
 Host=TRUE;
 // Never reset a DMA-enabled or already active host.
 if(Saved[2]&((3u<<10)|3u)){Error=22;goto RestoreHost;}
 TouchSpiWrite(NULL,0x18,4);TouchSpiBarrier(NULL);TouchSpiWrite(NULL,0x18,0);
 TouchSpiWrite(NULL,0,12u|(12u<<16)); // Enhanced CS hold/setup.
 TouchSpiWrite(NULL,4,12);TouchSpiWrite(NULL,0x28,12u|(12u<<16));
 TouchSpiWrite(NULL,0x24,0); // DT pad-select0, CS0.
 TouchSpiWrite(NULL,0x40,0); // IPM full duplex, as mtk_spi_transfer_one.
 TouchSpiWrite(NULL,0x18,BaseCmd);
 (VOID)TouchSpiRead(NULL,0x1c);
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH SPI prepared cmd=%08x status1=%08x cfg0=%08x cfg1=%08x cfg2=%08x pad=%08x\n",
  TouchSpiRead(NULL,0x18),TouchSpiRead(NULL,0x20),TouchSpiRead(NULL,0),TouchSpiRead(NULL,4),TouchSpiRead(NULL,0x28),TouchSpiRead(NULL,0x24)));
 for(I=0;I<2;I++){
  Error=23;
  for(J=0;J<5;J++){
   UINT32 Transfer;UINTN K;
   DegasFtsReadFrame(I?0x9f:0xa3,1,Tx,sizeof(Tx));SetMem(Rx,sizeof(Rx),0);
   // mt6985 sw_cs: mtk_spi_set_cs(false) asserts CS_POL before ACT.
   TouchSpiWrite(NULL,0x18,BaseCmd|(1u<<7));TouchSpiBarrier(NULL);
   Transfer=DegasSpiFifoOnce(&Io,Tx,Rx,sizeof(Tx),20000);
   if(Transfer)DEBUG((DEBUG_ERROR,"DEGAS TOUCH SPI rejected cmd=%08x status1=%08x status0=%08x\n",TouchSpiRead(NULL,0x18),TouchSpiRead(NULL,0x20),TouchSpiRead(NULL,0x1c)));
   // Deassert and reset between messages, mirroring set_cs(true).
   TouchSpiWrite(NULL,0x18,BaseCmd);TouchSpiBarrier(NULL);
   TouchSpiWrite(NULL,0x18,BaseCmd|4);TouchSpiBarrier(NULL);
   TouchSpiWrite(NULL,0x18,BaseCmd);TouchSpiBarrier(NULL);
   DEBUG((DEBUG_ERROR,"DEGAS TOUCH SPI reg=%02x attempt=%u transfer=%u rx=",I?0x9f:0xa3,(UINT32)J,Transfer));
   for(K=0;K<sizeof(Rx);K++)DEBUG((DEBUG_ERROR,"%02x",Rx[K]));
   DEBUG((DEBUG_ERROR,"\n"));
   if(!Transfer && DegasFtsReadReply(Rx,sizeof(Rx),1,&Id[I],1)){Error=0;break;}
   if(Transfer){Error=24+Transfer;break;}
   MicroSecondDelay(200000);
  }
  if(Error)break;
  MicroSecondDelay(150);
 }
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH SPI ID=%02x%02x error=%u\n",Id[0],Id[1],Error));
 if(!Error && (Id[0]!=0x36 || Id[1]!=0x83))Error=30;
 if(!Error)TouchSpiCapture(Gop,&Io,BaseCmd);
RestoreHost:
 if(Host){
  // No reset if rejected as active/DMA above; no host state was modified.
  if(Error!=22){
   TouchSpiWrite(NULL,0x18,4);TouchSpiBarrier(NULL);TouchSpiWrite(NULL,0x18,0);
   for(I=0;I<ARRAY_SIZE(Offsets);I++)if(Offsets[I]!=0x18)TouchSpiWrite(NULL,Offsets[I],Saved[I]);
   TouchSpiWrite(NULL,0x18,Saved[2]);TouchSpiBarrier(NULL);
  }
 }
RestoreClock:
 MmioWrite32(0x11036024,1u<<14);ArmDataSynchronizationBarrier();
 MmioWrite32(0x10000138,0x87000000);MmioWrite32(0x10000134,Mux&0x87000000);
 MmioWrite32(0x1000000c,1u<<13);ArmDataSynchronizationBarrier();MicroSecondDelay(10);
 if(!(Gate&(1u<<14)))MmioWrite32(0x11036028,1u<<14);
 ArmDataSynchronizationBarrier();
 if((MmioRead32(0x10000130)&0x87000000)!=(Mux&0x87000000) ||
    (MmioRead32(0x11036010)&(1u<<14))!=(Gate&(1u<<14)))Error=31;
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH SPI end error=%u mux=%08x gate=%08x\n",Error,MmioRead32(0x10000130),MmioRead32(0x11036010)));
 return Error;
}
