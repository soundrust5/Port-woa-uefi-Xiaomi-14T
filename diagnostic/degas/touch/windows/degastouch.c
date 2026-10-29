/* MT6897 FT3683: qualified FIFO-only SPI2 sequence. No DMA or IRQ writes.
 * All I/O serialized at PASSIVE_LEVEL. Own only touch rail enable, dedicated
 * GPIO bits and SPI2 clock field. Voltage selection is read-only.
 * First D0 qualification only; no speculative resume initialization. */
#include <ntddk.h>
#include <wdf.h>
#include "degas_vhf.h"
#include "degastouch_diag.h"
#include "../mtk_spi_fifo.h"
#include "../fts_protocol.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#include "../../../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/PmicRead.h"
#pragma clang diagnostic pop

#define BASE_CMD ((3u<<12)|(1u<<18)|(1u<<19)|(3u<<22))
enum { Spi, Gpio, Top, Peri, Pmif, PageCount };
static const ULONGLONG Addresses[PageCount]={0x11012000,0x10005000,0x10000000,0x11036000,0x1cc04000};
static const ULONG Offsets[6]={0,4,0x18,0x24,0x28,0x40};
typedef struct {
 WDFDEVICE Device; WDFWAITLOCK Lock; WDFTIMER Poll;
 PUCHAR Pages[PageCount]; DEGAS_TOUCH_VHF Hid;
 DEGAS_TOUCH_DIAGNOSTIC Diag;
 ULONG Mux,Gate,Saved[6]; unsigned int Vtp;
 UCHAR RawMode; BOOLEAN PowerOwned,ClockOwned,HostOwned,ModeOwned,Entered,Surprised;
 volatile LONG Stopping,Active;
 ULONGLONG NextLog; ULONG ConsecutiveErrors;
} DEVICE;
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DEVICE,GetDevice)
DRIVER_INITIALIZE DriverEntry;
static EVT_WDF_DRIVER_DEVICE_ADD Add;
static EVT_WDF_DEVICE_PREPARE_HARDWARE Prepare;
static EVT_WDF_DEVICE_RELEASE_HARDWARE Release;
static EVT_WDF_DEVICE_D0_ENTRY Enter;
static EVT_WDF_DEVICE_D0_EXIT Leave;
static EVT_WDF_DEVICE_SURPRISE_REMOVAL Surprise;
static EVT_WDF_TIMER Poll;
static ULONG Rd(DEVICE *d,unsigned p,ULONG o){return READ_REGISTER_ULONG((PULONG)(d->Pages[p]+o));}
static VOID Wr(DEVICE *d,unsigned p,ULONG o,ULONG v){WRITE_REGISTER_ULONG((PULONG)(d->Pages[p]+o),v);KeMemoryBarrier();}
static uint32_t SpiRead(void *c,uint32_t o){return Rd(c,Spi,o);}
static void SpiWrite(void *c,uint32_t o,uint32_t v){Wr(c,Spi,o,v);}
static void Barrier(void *c){UNREFERENCED_PARAMETER(c);KeMemoryBarrier();}
static void Micro(void *c,uint32_t us){UNREFERENCED_PARAMETER(c);KeStallExecutionProcessor(us);}
static unsigned int PRead(void *c,unsigned int o){return Rd(c,Pmif,o);}
static void PWrite(void *c,unsigned int o,unsigned int v){Wr(c,Pmif,o,v);}
static BOOLEAN Delay(DEVICE *d,ULONG ms){
 while(ms){LARGE_INTEGER t;ULONG n=ms>10?10:ms;
  if(InterlockedCompareExchange(&d->Stopping,0,0))return FALSE;
  t.QuadPart=-(LONGLONG)n*10000;KeDelayExecutionThread(KernelMode,FALSE,&t);ms-=n;
 }return TRUE;
}
static VOID Publish(DEVICE *d,ULONG stage){
 WDFKEY k;UNICODE_STRING n;d->Diag.Magic=DEGAS_TOUCH_DIAG_MAGIC;
 d->Diag.Size=sizeof d->Diag;d->Diag.Version=1;d->Diag.Stage=stage;d->Diag.Active=d->Active;
 if(!NT_SUCCESS(WdfDeviceOpenRegistryKey(d->Device,PLUGPLAY_REGKEY_DEVICE,KEY_SET_VALUE,WDF_NO_OBJECT_ATTRIBUTES,&k)))return;
 RtlInitUnicodeString(&n,L"TouchDiagnostic");
 (void)WdfRegistryAssignValue(k,&n,REG_BINARY,sizeof d->Diag,&d->Diag);WdfRegistryClose(k);
}
static ULONG Message(DEVICE *d,UCHAR *tx,UCHAR *rx,SIZE_T n){
 DEGAS_SPI_IO io={d,SpiRead,SpiWrite,Barrier,Micro};ULONG e;
 Wr(d,Spi,0x18,BASE_CMD|(1u<<7));
 e=DegasSpiFifoOnce(&io,tx,rx,n,20000);
 Wr(d,Spi,0x18,BASE_CMD);Wr(d,Spi,0x18,BASE_CMD|4);Wr(d,Spi,0x18,BASE_CMD);
 Micro(d,150);d->Diag.IoError=e;return e;
}
static BOOLEAN ReadData(DEVICE *d,UCHAR reg,UCHAR *out,SIZE_T n){
 UCHAR tx[32],rx[32]={0};SIZE_T len=DegasFtsReadFrame(reg,n,tx,sizeof tx);d->Diag.Reads++;
 if(!len || Message(d,tx,rx,len))return FALSE;
 if(!DegasFtsReadReply(rx,len,n,out,n)){d->Diag.IoError=40;return FALSE;}return TRUE;
}
static BOOLEAN SetMode(DEVICE *d,UCHAR v){
 UCHAR tx[8]={0x9e,0,0,1,0,0,0,0},rx[8]={0},back=0xff;
 if(v!=0 && v!=0x41)return FALSE;tx[7]=v;
 if(Message(d,tx,rx,sizeof tx) || (rx[3]&0xa0))return FALSE;
 /* Cleanup also uses this wait after cancellation. */
 {LARGE_INTEGER t;t.QuadPart=-10000;KeDelayExecutionThread(KernelMode,FALSE,&t);}
 return ReadData(d,0x9e,&back,1) && back==v;
}
static ULONG StopHardware(DEVICE *d){
 ULONG e=0,i,primary=d->Diag.IoError;DEGAS_PMIF_IO pm={d,PRead,PWrite,Micro};
 if(d->Surprised)return 60; /* Never access surprise-removed hardware. */
 if(d->ModeOwned){if(!SetMode(d,d->RawMode))e=61;d->ModeOwned=FALSE;}
 if(d->HostOwned){
  Wr(d,Spi,0x18,4);Wr(d,Spi,0x18,0);
  for(i=0;i<6;i++)if(Offsets[i]!=0x18)Wr(d,Spi,Offsets[i],d->Saved[i]);
  Wr(d,Spi,0x18,d->Saved[2]);d->HostOwned=FALSE;
 }
 if(d->ClockOwned){
  Wr(d,Peri,0x24,1u<<14);Wr(d,Top,0x138,0x87000000);Wr(d,Top,0x134,d->Mux&0x87000000);
  Wr(d,Top,0xc,1u<<13);Micro(d,10);
  if(!(d->Gate&(1u<<14)))Wr(d,Peri,0x28,1u<<14);
  if((Rd(d,Top,0x130)&0x87000000)!=(d->Mux&0x87000000) ||
     (Rd(d,Peri,0x10)&(1u<<14))!=(d->Gate&(1u<<14)))e=62;
  d->ClockOwned=FALSE;
 }
 if(d->PowerOwned){
  Wr(d,Gpio,0x118,1u<<28);Micro(d,2100);Wr(d,Gpio,0x128,1u<<9);Micro(d,1000);
  if(DegasPmicVtpEnable(&pm,d->Vtp&1))e=63;
  if((Rd(d,Gpio,0x110)&(1u<<28)) || (Rd(d,Gpio,0x120)&(1u<<9)))e=64;
  d->PowerOwned=FALSE;
 }d->Diag.IoError=primary;d->Diag.CleanupError=e;return e;
}
static NTSTATUS StartHardware(DEVICE *d){
 DEGAS_PMIF_IO pm={d,PRead,PWrite,Micro};unsigned int sel,cal;ULONG e,i,j;UCHAR id[2]={0};
 Publish(d,1);
 if(DegasPmicReadByte(&pm,5,0x1c87,&d->Vtp) || DegasPmicReadByte(&pm,5,0x1da4,&cal) ||
    DegasPmicReadByte(&pm,5,0x1da5,&sel)){d->Diag.IoError=10;return STATUS_IO_DEVICE_ERROR;}
 if((sel&15)!=4 || (cal&15)!=0 || ((Rd(d,Gpio,0x370)>>16)&15)!=0 ||
    ((Rd(d,Gpio,0x390)>>4)&15)!=0 || !(Rd(d,Gpio,0x10)&(1u<<28)) ||
    !(Rd(d,Gpio,0x20)&(1u<<9)) || (Rd(d,Gpio,0x110)&(1u<<28)) ||
    (Rd(d,Gpio,0x120)&(1u<<9)) || (Rd(d,Gpio,0x310)&0x0ffff000)!=0x01111000){
  d->Diag.IoError=11;return STATUS_DEVICE_CONFIGURATION_ERROR;
 }
 d->PowerOwned=TRUE;Micro(d,2100);e=DegasPmicVtpEnable(&pm,1);
 if(e){d->Diag.IoError=12+e;return STATUS_IO_DEVICE_ERROR;}
 Micro(d,1000);Wr(d,Gpio,0x124,1u<<9);Micro(d,510);
 if(!(Rd(d,Gpio,0x120)&(1u<<9))){d->Diag.IoError=19;return STATUS_IO_DEVICE_ERROR;}
 Wr(d,Gpio,0x114,1u<<28);if(!Delay(d,100))return STATUS_CANCELLED;
 if(!(Rd(d,Gpio,0x110)&(1u<<28))){d->Diag.IoError=20;return STATUS_IO_DEVICE_ERROR;}
 Publish(d,2);
 d->Mux=Rd(d,Top,0x130);d->Gate=Rd(d,Peri,0x10);d->ClockOwned=TRUE;
 Wr(d,Peri,0x24,1u<<14);Wr(d,Top,0x138,0x87000000);Wr(d,Top,0xc,1u<<13);
 Micro(d,10);Wr(d,Peri,0x28,1u<<14);Micro(d,10);
 if((Rd(d,Top,0x130)&0x87000000) || (Rd(d,Peri,0x10)&(1u<<14))){d->Diag.IoError=21;return STATUS_IO_DEVICE_ERROR;}
 for(i=0;i<6;i++)d->Saved[i]=Rd(d,Spi,Offsets[i]);
 if(d->Saved[2]&((3u<<10)|3u)){d->Diag.IoError=22;return STATUS_DEVICE_BUSY;}
 d->HostOwned=TRUE;Wr(d,Spi,0x18,4);Wr(d,Spi,0x18,0);
 Wr(d,Spi,0,0x000c000c);Wr(d,Spi,4,12);Wr(d,Spi,0x28,0x000c000c);
 Wr(d,Spi,0x24,0);Wr(d,Spi,0x40,0);Wr(d,Spi,0x18,BASE_CMD);(void)Rd(d,Spi,0x1c);
 Publish(d,3);
 for(i=0;i<2;i++){
  for(j=0;j<5;j++){
   if(ReadData(d,i?0x9f:0xa3,&id[i],1))break;
   if(!Delay(d,200))return STATUS_CANCELLED;
  }if(j==5)return STATUS_DEVICE_PROTOCOL_ERROR;
 }
 d->Diag.Id=((ULONG)id[0]<<8)|id[1];
 if(d->Diag.Id!=0x3683){d->Diag.IoError=30;return STATUS_DEVICE_PROTOCOL_ERROR;}
 Publish(d,4);
 if(!ReadData(d,0x9e,&d->RawMode,1) || (d->RawMode!=0 && d->RawMode!=0x41))return STATUS_DEVICE_PROTOCOL_ERROR;
 d->Diag.RawMode=d->RawMode;
 if(d->RawMode==0x41){d->ModeOwned=TRUE;if(!SetMode(d,0))return STATUS_DEVICE_PROTOCOL_ERROR;}
 d->Diag.RawMode=0;return STATUS_SUCCESS;
}
static NTSTATUS Prepare(WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated){
 DEVICE *d=GetDevice(device);ULONG i,n=0;UNREFERENCED_PARAMETER(raw);
 for(i=0;i<WdfCmResourceListGetCount(translated);i++){
  PCM_PARTIAL_RESOURCE_DESCRIPTOR r=WdfCmResourceListGetDescriptor(translated,i);
  if(!r || r->Type!=CmResourceTypeMemory)continue;
  if(n>=PageCount || r->u.Memory.Start.QuadPart!=(LONGLONG)Addresses[n] || r->u.Memory.Length!=0x1000)goto Bad;
  d->Pages[n]=MmMapIoSpaceEx(r->u.Memory.Start,0x1000,PAGE_READWRITE|PAGE_NOCACHE);
  if(!d->Pages[n])goto Bad;n++;
 }if(n==PageCount)return STATUS_SUCCESS;
Bad:
 for(i=0;i<PageCount;i++)if(d->Pages[i]){MmUnmapIoSpace(d->Pages[i],0x1000);d->Pages[i]=NULL;}
 return STATUS_DEVICE_CONFIGURATION_ERROR;
}
static NTSTATUS Enter(WDFDEVICE device,WDF_POWER_DEVICE_STATE previous){
 DEVICE *d=GetDevice(device);NTSTATUS s;UNREFERENCED_PARAMETER(previous);
 (void)WdfWaitLockAcquire(d->Lock,NULL);
 if(d->Entered){s=STATUS_NOT_SUPPORTED;goto Failed;}d->Entered=TRUE;
 InterlockedExchange(&d->Stopping,0);s=StartHardware(d);if(!NT_SUCCESS(s))goto Failed;
 s=DegasTouchVhfCreate(&d->Hid,device);if(!NT_SUCCESS(s))goto Failed;
 if(!DegasTouchVhfRearm(&d->Hid)){s=STATUS_DEVICE_NOT_READY;goto Failed;}
 d->Diag.StartStatus=STATUS_SUCCESS;InterlockedExchange(&d->Active,1);Publish(d,6);
 d->NextLog=KeQueryInterruptTime()+10000000;WdfTimerStart(d->Poll,WDF_REL_TIMEOUT_IN_MS(10));
 WdfWaitLockRelease(d->Lock);return STATUS_SUCCESS;
Failed:
 d->Diag.StartStatus=s;InterlockedExchange(&d->Active,0);InterlockedExchange(&d->Stopping,1);
 (void)DegasTouchVhfFault(&d->Hid);(void)StopHardware(d);Publish(d,90);
 WdfWaitLockRelease(d->Lock);return s;
}
static VOID Poll(WDFTIMER timer){
 DEVICE *d=GetDevice((WDFDEVICE)WdfTimerGetParentObject(timer));UCHAR data[20];DEGAS_FTS_REPORT report;
 (void)WdfWaitLockAcquire(d->Lock,NULL);
 if(d->Active && !InterlockedCompareExchange(&d->Stopping,0,0)){
  d->Diag.Polls++;
  if(ReadData(d,1,data,sizeof data) && DegasFtsReport2(data,sizeof data,&report) && report.count<=2){
   d->Diag.Good++;d->ConsecutiveErrors=0;
   d->Diag.LastX=report.contacts[0].x;d->Diag.LastY=report.contacts[0].y;d->Diag.LastEvent=report.contacts[0].event;
   {
    DEGAS_HID_RESULT h=DegasTouchVhfSubmit(&d->Hid,&report);
    /* A stale timeout latches all-up. A fresh CRC-valid type2 packet proves
     * the established report mode is still active. Retry pending release
     * before rearming; a failed release must never become a new DOWN. */
    if(h==DegasHidFaulted){
     (void)DegasTouchVhfFault(&d->Hid);
     if(DegasTouchVhfRearm(&d->Hid))h=DegasTouchVhfSubmit(&d->Hid,&report);
    }
    if(h==DegasHidOk)d->Diag.Reports++;
    else if(h!=DegasHidIdle)d->Diag.IoError=100+(ULONG)h;
   }
  }else{
   d->Diag.Bad++;d->ConsecutiveErrors++;
   /* VHF stale deadline guarantees all-up even for missing/invalid packets.
    * Bad initial or unsupported multi-contact packets never become input. */
  }
  if(KeQueryInterruptTime()>=d->NextLog){Publish(d,6);d->NextLog=KeQueryInterruptTime()+10000000;}
  WdfTimerStart(timer,WDF_REL_TIMEOUT_IN_MS(10));
 }WdfWaitLockRelease(d->Lock);
}
static VOID Surprise(WDFDEVICE device){
 DEVICE *d=GetDevice(device);
 InterlockedExchange(&d->Stopping,1);InterlockedExchange(&d->Active,0);
 (void)WdfTimerStop(d->Poll,TRUE);
 (void)WdfWaitLockAcquire(d->Lock,NULL);d->Surprised=TRUE;
 (void)DegasTouchVhfFault(&d->Hid);WdfWaitLockRelease(d->Lock);
}
static NTSTATUS Leave(WDFDEVICE device,WDF_POWER_DEVICE_STATE target){
 DEVICE *d=GetDevice(device);ULONG e;UNREFERENCED_PARAMETER(target);
 InterlockedExchange(&d->Stopping,1);InterlockedExchange(&d->Active,0);WdfTimerStop(d->Poll,TRUE);
 (void)WdfWaitLockAcquire(d->Lock,NULL);(void)DegasTouchVhfFault(&d->Hid);e=StopHardware(d);Publish(d,80);
 WdfWaitLockRelease(d->Lock);return e?STATUS_IO_DEVICE_ERROR:STATUS_SUCCESS;
}
static NTSTATUS Release(WDFDEVICE device,WDFCMRESLIST translated){
 DEVICE *d=GetDevice(device);ULONG i;UNREFERENCED_PARAMETER(translated);
 InterlockedExchange(&d->Stopping,1);InterlockedExchange(&d->Active,0);WdfTimerStop(d->Poll,TRUE);
 DegasTouchVhfDestroy(&d->Hid);(void)StopHardware(d);
 for(i=0;i<PageCount;i++)if(d->Pages[i]){MmUnmapIoSpace(d->Pages[i],0x1000);d->Pages[i]=NULL;}
 return STATUS_SUCCESS;
}
static NTSTATUS Add(WDFDRIVER driver,PWDFDEVICE_INIT init){
 WDF_PNPPOWER_EVENT_CALLBACKS p;WDF_OBJECT_ATTRIBUTES a;WDF_TIMER_CONFIG t;WDFDEVICE device;DEVICE *d;NTSTATUS s;
 UNREFERENCED_PARAMETER(driver);WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&p);
 p.EvtDevicePrepareHardware=Prepare;p.EvtDeviceReleaseHardware=Release;p.EvtDeviceD0Entry=Enter;
 p.EvtDeviceD0Exit=Leave;p.EvtDeviceSurpriseRemoval=Surprise;WdfDeviceInitSetPnpPowerEventCallbacks(init,&p);
 WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&a,DEVICE);a.ExecutionLevel=WdfExecutionLevelPassive;a.SynchronizationScope=WdfSynchronizationScopeNone;
 s=WdfDeviceCreate(&init,&a,&device);if(!NT_SUCCESS(s))return s;d=GetDevice(device);d->Device=device;d->Stopping=1;
 WDF_OBJECT_ATTRIBUTES_INIT(&a);a.ParentObject=device;s=WdfWaitLockCreate(&a,&d->Lock);if(!NT_SUCCESS(s))return s;
 WDF_TIMER_CONFIG_INIT(&t,Poll);t.AutomaticSerialization=FALSE;
 a.ExecutionLevel=WdfExecutionLevelPassive;a.SynchronizationScope=WdfSynchronizationScopeNone;
 return WdfTimerCreate(&t,&a,&d->Poll);
}
NTSTATUS DriverEntry(PDRIVER_OBJECT object,PUNICODE_STRING path){WDF_DRIVER_CONFIG c;WDF_DRIVER_CONFIG_INIT(&c,Add);return WdfDriverCreate(object,path,WDF_NO_OBJECT_ATTRIBUTES,&c,WDF_NO_HANDLE);}
