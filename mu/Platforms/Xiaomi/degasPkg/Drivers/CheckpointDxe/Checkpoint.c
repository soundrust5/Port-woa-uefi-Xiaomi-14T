#include <Uefi.h>
#include <Library/SerialPortLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/Bds.h>
#include <Library/TimerLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/BaseLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/ArmLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/BaseMemoryLib.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextOut.h>
#include <Library/DevicePathLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Guid/EventGroup.h>
#include <Protocol/DxeSmmReadyToLock.h>
#include "BdsLifecycle.h"
STATIC EFI_HANDLE mCheckpointImage;
#include "Font.h"
#include "PmicRead.h"
STATIC unsigned int PmicRead(void *C,unsigned int O){return MmioRead32(0x1cc04000+O);}
STATIC void PmicWrite(void *C,unsigned int O,unsigned int V){MmioWrite32(0x1cc04000+O,V);}
STATIC void PmicDelay(void *C,unsigned int Us){MicroSecondDelay(Us);}
STATIC EFI_STATUS Label(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,UINTN Y,CONST CHAR8 *Text){
 EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer; UINTN X,R,C,J,K,N;
 EFI_STATUS Status; CONST UINTN W=1020,H=75,Scale=5;
 Buffer=AllocatePool(W*H*sizeof(*Buffer)); if(Buffer==NULL)return EFI_OUT_OF_RESOURCES;
 for(N=0;N<W*H;N++){Buffer[N].Blue=0;Buffer[N].Green=0;Buffer[N].Red=0;Buffer[N].Reserved=0;}
 for(X=0;*Text && X+30<=W;Text++,X+=30){
  CONST UINT8 *Glyph;
  if(*Text>='A'&&*Text<='Z')Glyph=letters[*Text-'A'];
  else if(*Text>='0'&&*Text<='9')Glyph=digits[*Text-'0'];
  else continue;
  for(R=0;R<7;R++)for(C=0;C<5;C++)if(Glyph[R]&(1u<<(4-C)))
   for(J=0;J<Scale;J++)for(K=0;K<Scale;K++){
    N=(R*Scale+J)*W+X+C*Scale+K;
    Buffer[N].Red=0xff;Buffer[N].Green=0xff;Buffer[N].Blue=0xff;
   }
 }
 Status=Gop->Blt(Gop,Buffer,EfiBltBufferToVideo,0,0,100,Y,W,H,W*sizeof(*Buffer));
 FreePool(Buffer);return Status;
}
#include "BootLook.h"
// DME_GET reads a local UniPro attribute; it never starts a transfer queue.
STATIC BOOLEAN UfsReadAttributes(VOID){
 STATIC CONST UINT16 Attrs[]={0x15a9,0x155e,0xd0a6,0xd0a8}; UINTN A,N;
 UINT32 Is,Result,Value;
 for(A=0;A<ARRAY_SIZE(Attrs);A++){
  if((MmioRead32(0x112b0030)&8)==0)return FALSE;
  MmioWrite32(0x112b0020,0x400); // W1C only the stale UIC completion bit.
  if(MmioRead32(0x112b0020)&0x400)return FALSE;
  MmioWrite32(0x112b0094,(UINT32)Attrs[A]<<16);
  MmioWrite32(0x112b0098,0);MmioWrite32(0x112b009c,0);
  DEBUG((DEBUG_ERROR,"DEGAS UFS DME GET begin attr=%04x\n",Attrs[A]));
  MmioWrite32(0x112b0090,1);
  for(N=0;N<1000;N++){
   Is=MmioRead32(0x112b0020);if(Is&0x400)break;MicroSecondDelay(100);
  }
  if(N==1000){DEBUG((DEBUG_ERROR,"DEGAS UFS DME GET TIMEOUT attr=%04x is=%08x\n",Attrs[A],Is));return FALSE;}
  Result=MmioRead32(0x112b0098)&0xff;Value=MmioRead32(0x112b009c);
  DEBUG((DEBUG_ERROR,"DEGAS UFS DME GET attr=%04x result=%08x value=%08x polls=%u\n",Attrs[A],Result,Value,(UINT32)N));
  MmioWrite32(0x112b0020,0x400);
  if(Result)return FALSE;
  if((Attrs[A]==0x155e && Value!=0) || (Attrs[A]==0xd0a8 && Value!=0) ||
     (Attrs[A]==0xd0a6 && (Value&0x40)))return FALSE;
 }
 return TRUE;
}
// No DMA or SCSI commands: establish the physical/UniPro connection only.
STATIC BOOLEAN UfsStartLink(VOID){
 UINTN N;UINT32 Is=0,Result,Hcs=0;
 if((MmioRead32(0x112b0030)&8)==0)return FALSE;
 MmioWrite32(0x112b0020,0x400);
 if(MmioRead32(0x112b0020)&0x400)return FALSE;
 MmioWrite32(0x112b0094,0);MmioWrite32(0x112b0098,0);MmioWrite32(0x112b009c,0);
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK STARTUP begin\n"));
 MmioWrite32(0x112b0090,0x16);
 for(N=0;N<10000;N++){
  Is=MmioRead32(0x112b0020);if(Is&0x400)break;MicroSecondDelay(100);
 }
 Result=MmioRead32(0x112b0098)&0xff;
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK completion=%u result=%08x polls=%u is=%08x\n",N<10000,Result,(UINT32)N,Is));
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK errors hcs=%08x pa=%08x dl=%08x net=%08x trans=%08x dme=%08x\n",MmioRead32(0x112b0030),MmioRead32(0x112b0038),MmioRead32(0x112b003c),MmioRead32(0x112b0040),MmioRead32(0x112b0044),MmioRead32(0x112b0048)));
 if(N==10000 || Result)return FALSE;
 MmioWrite32(0x112b0020,0x400);
 for(N=0;N<1000;N++){
  Hcs=MmioRead32(0x112b0030);if(Hcs&1)break;MicroSecondDelay(100);
 }
 DEBUG((DEBUG_ERROR,"DEGAS UFS LINK device_present=%u hcs=%08x pa=%08x dl=%08x\n",!!(Hcs&1),Hcs,MmioRead32(0x112b0038),MmioRead32(0x112b003c)));
 return (Hcs&1)!=0;
}
STATIC BOOLEAN UfsLinkAttempts(VOID){
 UINTN Attempt,N;BOOLEAN Done;
 for(Attempt=0;Attempt<3;Attempt++){
  DEBUG((DEBUG_ERROR,"DEGAS UFS LINK attempt=%u\n",(UINT32)Attempt+1));
  if(!UfsReadAttributes())return FALSE;
  if(UfsStartLink())return TRUE;
  if(Attempt==2)break;
  // Match the generic Android retry's HBA disable/enable transition.
  // No SoC reset or device-reset line operation is implied here.
  MmioWrite32(0x112b0034,0);Done=FALSE;
  for(N=0;N<1000;N++){
   if((MmioRead32(0x112b0034)&1)==0){Done=TRUE;break;}MicroSecondDelay(100);
  }
  if(!Done){DEBUG((DEBUG_ERROR,"DEGAS UFS RETRY STOP FAILED\n"));return FALSE;}
  MmioWrite32(0x112b0020,0x404); // Both sources observed/read before acknowledge.
  MmioWrite32(0x112b0034,1);MicroSecondDelay(600);Done=FALSE;
  for(N=0;N<1000;N++){
   if((MmioRead32(0x112b0034)&1) && (MmioRead32(0x112b0030)&8)){Done=TRUE;break;}
   MicroSecondDelay(100);
  }
  if(!Done){DEBUG((DEBUG_ERROR,"DEGAS UFS RETRY ENABLE FAILED\n"));return FALSE;}
 }
 return FALSE;
}
// Android MediaTek device-reset ABI: SiP UFS_CONTROL, DEVICE_RESET bit1.
STATIC BOOLEAN UfsDeviceReset(VOID){
 ARM_SMC_ARGS Low={0},High={0};
 Low.Arg0=0xc2000276;Low.Arg1=2;Low.Arg2=0;
 DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE RESET asserting\n"));
 ArmCallSmc(&Low);
 DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE RESET low a0=%Lx a1=%Lx\n",(UINT64)Low.Arg0,(UINT64)Low.Arg1));
 // HCE was verified zero by the caller, before any reset operation.
 MicroSecondDelay(10);
 High.Arg0=0xc2000276;High.Arg1=2;High.Arg2=1;
 // Always release, including when asserting returned an error.
 ArmCallSmc(&High);
 DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE RESET high a0=%Lx a1=%Lx\n",(UINT64)High.Arg0,(UINT64)High.Arg1));
 MicroSecondDelay(10000);
 return Low.Arg0==0 && High.Arg0==0;
}
// Dedicated 64KiB uncached region removed from the DXE heap. Identity
// DMA addressing follows the live /soc dma-ranges; no IOMMU reprogramming.
STATIC BOOLEAN UfsNopTest(VOID){
 volatile UINT32 *Trd=(volatile UINT32 *)(UINTN)0x44470000;
 volatile UINT8 *Ucd=(volatile UINT8 *)(UINTN)0x44471000;
 UINTN N;UINT32 Door,Is;BOOLEAN Ok;
 if((MmioRead32(0x112b0030)&0xf)!=0xf || MmioRead32(0x112b0024)!=0 ||
    MmioRead32(0x112b0050)!=0 || MmioRead32(0x112b0054)!=0 ||
    MmioRead32(0x112b0058)!=0 || MmioRead32(0x112b0060)!=0)return FALSE;
 for(N=0;N<0x4000;N++)Trd[N]=0;
 Trd[0]=0x11000000;Trd[2]=0xf;Trd[4]=0x44471000;
 Trd[6]=0x00080008; // Response length8 and offset8, both in DWORDs.
 Ucd[3]=0x5a; // Nonzero tag to distinguish a real reply from zeroed RAM.
 MemoryFence();
 DEBUG((DEBUG_ERROR,"DEGAS UFS NOP DMA trl=44470000 ucd=44471000 bytes=65536\n"));
 MmioWrite32(0x112b0020,1);
 MmioWrite32(0x112b0050,0x44470000);MmioWrite32(0x112b0054,0);
 if(MmioRead32(0x112b0050)!=0x44470000 || MmioRead32(0x112b0054)!=0)return FALSE;
 MmioWrite32(0x112b0060,1);
 if((MmioRead32(0x112b0060)&1)==0)return FALSE;
 MemoryFence();MmioWrite32(0x112b0058,1);
 for(N=0;N<10000;N++){
  Door=MmioRead32(0x112b0058);Is=MmioRead32(0x112b0020);
  if((Door&1)==0 || (Is&0x70000))break;
  MicroSecondDelay(100);
 }
 MemoryFence();
 Ok=(Door&1)==0 && !(Is&0x70000) && (Trd[2]&0xff)==0 &&
    Ucd[32]==0x20 && Ucd[35]==0x5a && Ucd[38]==0 && Ucd[40]==0 &&
    Ucd[42]==0 && Ucd[43]==0;
 DEBUG((DEBUG_ERROR,"DEGAS UFS NOP ok=%u polls=%u door=%08x is=%08x ocs=%08x type=%02x tag=%02x response=%02x\n",Ok,(UINT32)N,Door,Is,Trd[2],Ucd[32],Ucd[35],Ucd[38]));
 // Buffer stays reserved even on timeout. Caller disables HCE before clearing base.
 MmioWrite32(0x112b0060,0);
 return Ok;
}
STATIC BOOLEAN UfsDeviceDescriptor(VOID){
 volatile UINT32 *Trd=(volatile UINT32 *)(UINTN)0x44470000;
 volatile UINT8 *Ucd=(volatile UINT8 *)(UINTN)0x44471000;
 volatile UINT8 *R=Ucd+128,*D=R+32;
 UINTN N;UINT32 Door=1,Is=0,Len,TsfLen;BOOLEAN Ok;
 if((MmioRead32(0x112b0030)&0xf)!=0xf || MmioRead32(0x112b0024)!=0 ||
    MmioRead32(0x112b0050)!=0x44470000 || MmioRead32(0x112b0054)!=0 ||
    MmioRead32(0x112b0058)!=0 || MmioRead32(0x112b0060)!=0)return FALSE;
 for(N=0;N<0x4000;N++)Trd[N]=0;
 Trd[0]=0x11000000;Trd[2]=0xf;Trd[4]=0x44471000;
 Trd[6]=0x00200048; // Response offset128, capacity288 bytes; no PRDT.
 Ucd[0]=0x16;Ucd[3]=0x5b;Ucd[5]=1;Ucd[12]=1;Ucd[19]=255;
 MemoryFence();MmioWrite32(0x112b0020,1);
 MmioWrite32(0x112b0060,1);
 if((MmioRead32(0x112b0060)&1)==0)return FALSE;
 DEBUG((DEBUG_ERROR,"DEGAS UFS QUERY READ DEVICE DESCRIPTOR begin max=255\n"));
 MemoryFence();MmioWrite32(0x112b0058,1);
 for(N=0;N<10000;N++){
  Door=MmioRead32(0x112b0058);Is=MmioRead32(0x112b0020);
  if((Door&1)==0 || (Is&0x70000))break;MicroSecondDelay(100);
 }
 MemoryFence();Len=((UINT32)R[10]<<8)|R[11];TsfLen=((UINT32)R[18]<<8)|R[19];
 Ok=(Door&1)==0 && !(Is&0x70000) && (Trd[2]&0xff)==0 &&
    R[0]==0x36 && R[3]==0x5b && R[5]==1 && R[6]==0 && R[8]==0 &&
    R[12]==1 && R[13]==0 && R[14]==0 && R[15]==0 && Len>=26 &&
    Len<=255 && TsfLen==Len && D[0]>=26 && D[0]<=Len && D[1]==0;
 DEBUG((DEBUG_ERROR,"DEGAS UFS QUERY ok=%u polls=%u door=%08x is=%08x ocs=%08x type=%02x tag=%02x result=%02x len=%u tsflen=%u\n",Ok,(UINT32)N,Door,Is,Trd[2],R[0],R[3],R[6],Len,TsfLen));
 if(Ok){
  DEBUG((DEBUG_ERROR,"DEGAS UFS DEVICE bytes=%u luns=%u wluns=%u spec=%02x%02x manufacturer=%02x%02x\n",D[0],D[6],D[7],D[16],D[17],D[24],D[25]));
  for(N=0;N<D[0];N++)DEBUG((DEBUG_ERROR,"DEGAS UFS DESC byte=%u value=%02x\n",(UINT32)N,D[N]));
 }
 MmioWrite32(0x112b0060,0);return Ok;
}
#include "UfsCapacity.h"
#include "UfsReadProbe.h"
#include "UfsHighSpeed.h"
#include "BootWatchdog.h"
#include "UfsCopy.h"
#include "UfsSpeedCheck.h"
#include "UfsBlockIo.h"
#ifndef DEGAS_DIAGNOSTIC_EBS_WATCHDOG
#define DEGAS_DIAGNOSTIC_EBS_WATCHDOG 0
#endif
#include "OsHandoff.h"
#include "GceSnapshot.h"
#include "GceMappingInstall.h"
#include "GceFetchSmoke.h"
#include "GceFramePacket.h"
#include "GceFrameGuard.h"
#include "GceWindows.h"
#include "WindowsBoot.h"
// Bounded transport diagnostic; no block writes.
STATIC VOID UfsEnableTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,BOOLEAN SpeedOnly){
 UINT32 Xo, Hce=0, Hcs=0; UINTN N; BOOLEAN Ready=FALSE,Stopped=FALSE,ReadOk=FALSE,LinkOk=FALSE,NopOk=FALSE,DescOk=FALSE,CapacityOk=FALSE,HsOk=FALSE,BlocksOk=FALSE,PartitionsOk=FALSE;
 if(MmioRead32(0x112b0008)!=0x400 || MmioRead32(0x112b2240)!=0x01440000 ||
    (MmioRead32(0x112b0144)&3)!=3 || MmioRead32(0x112b0034)!=0 ||
    MmioRead32(0x112b0024)!=0 || MmioRead32(0x112b0058)!=0 ||
    MmioRead32(0x112b0060)!=0 || MmioRead32(0x112b0078)!=0 ||
    MmioRead32(0x112b0080)!=0){
  DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE guard failed\n"));
  Label(Gop,1690,"UFS PRECONDITION FAILED");return;
 }
 Label(Gop,1690,"UFS RESET");
 if(!UfsDeviceReset()){Label(Gop,1690,"UFS DEVICE RESET FAILED");return;}
 Xo=MmioRead32(0x112b0140);
 DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE begin xo=%08x ref=%08x\n",Xo,MmioRead32(0x112b0144)));
 // MediaTek HCE PRE_CHANGE: CLK_CG bypasses abnormal ERR_CHK signal.
 MmioWrite32(0x112b0140,Xo|0x80);
 if(MmioRead32(0x112b0140)!=(Xo|0x80)){
  DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE XO readback failed\n"));
  Label(Gop,1690,"UFS CLOCK CHECK FAILED");return;
 }
 MmioWrite32(0x112b0034,1);MicroSecondDelay(600);
 for(N=0;N<1000;N++){
  Hce=MmioRead32(0x112b0034);Hcs=MmioRead32(0x112b0030);
  if((Hce&1) && (Hcs&8)){Ready=TRUE;break;}
  MicroSecondDelay(100);
 }
 DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE ready=%u hce=%08x hcs=%08x polls=%u is=%08x\n",Ready,Hce,Hcs,(UINT32)N,MmioRead32(0x112b0020)));
 if(Ready){Label(Gop,1690,"UFS ATTRIBUTES");ReadOk=UfsReadAttributes();}
 if(ReadOk){Label(Gop,1690,"UFS LINK");LinkOk=UfsLinkAttempts();}
 if(LinkOk){Label(Gop,1690,"UFS NOP");NopOk=UfsNopTest();}
 if(NopOk){Label(Gop,1690,"UFS IDENTIFY");DescOk=UfsDeviceDescriptor();}
 if(DescOk){Label(Gop,1690,"UFS CAPACITY");CapacityOk=UfsCapacityProbe();}
 if(CapacityOk){Label(Gop,1690,"UFS HIGH SPEED");HsOk=UfsHighSpeed();}
 if(HsOk){Label(Gop,1690,"UFS READ GPT");BlocksOk=UfsReadProbe();
  if(BlocksOk){Label(Gop,1690,"UFS SPEED VERIFY");BlocksOk=UfsSpeedCheck();}}
 if(BlocksOk && !SpeedOnly){Label(Gop,1690,"UFS CONNECT PARTITIONS");PartitionsOk=UfsBlockIoTest(TRUE);}
 if(PartitionsOk){
  EFI_STATUS WinStatus=WindowsBoot(Gop);
  DEBUG((DEBUG_ERROR,"DEGAS WINDOWS returned to menu=%r\n",WinStatus));
  // Disconnect before stopping the transport, even on a failed launch.
  mUfsActive=FALSE;
  gBS->DisconnectController(mUfsHandle,NULL,NULL);
  gBS->UninstallMultipleProtocolInterfaces(mUfsHandle,&gEfiBlockIoProtocolGuid,&mUfsBio,
    &gEfiDevicePathProtocolGuid,&mUfsPath,NULL);
 }
 MmioWrite32(0x112b0034,0);
 for(N=0;N<1000;N++){
  if((MmioRead32(0x112b0034)&1)==0){Stopped=TRUE;break;}
  MicroSecondDelay(100);
 }
 if(Stopped){MmioWrite32(0x112b0050,0);MmioWrite32(0x112b0054,0);MmioWrite32(0x112b0140,Xo);}
 DEBUG((DEBUG_ERROR,"DEGAS UFS ENABLE cleanup=%u xo=%08x READ ONLY BLOCKIO TEST\n",Stopped,MmioRead32(0x112b0140)));
 Label(Gop,1690,!Stopped?"UFS STOP FAILED RESET REQUIRED":(SpeedOnly && BlocksOk)?"UFS SPEED AND CRC PASS":PartitionsOk?"WINDOWS RETURNED SEE RAM LOG":BlocksOk?"UFS PARTITION TEST FAILED":HsOk?"UFS BLOCK READ FAILED":CapacityOk?"UFS HIGH SPEED FAILED":DescOk?"UFS CAPACITY FAILED":NopOk?"UFS DESCRIPTOR FAILED":LinkOk?"UFS NOP FAILED":ReadOk?"UFS LINK FAILED":Ready?"UFS ATTRIBUTE READ FAILED":"UFS ENABLE TIMEOUT");
}
#include "ApJournalRetention.h"
#include "BootAndroid.h"
#include "CpuProbe.h"
#include "CpuCacheVisibility.h"
#include "CpuDvfsSnapshot.h"
#include "TouchSpi.h"
#include "TouchPower.h"
#include "../../Include/Protocol/DegasCpuStatus.h"
#if DEGAS_WIFI_INIT_TEST
#include "WifiRfPowerProbe.h"
#endif
STATIC VOID ButtonTest(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 AndroidRouteReport();
#if DEGAS_WIFI_INIT_TEST
 BOOLEAN WifiDone=FALSE;
#endif
 UINTN Selected=0,Stable=0,RouteTick=0;UINT32 Last=0,Pressed=0,Sample,Edges;
 unsigned int Pmic=0,Error;DEGAS_PMIF_IO Io={NULL,PmicRead,PmicWrite,PmicDelay};
 Error=DegasPmicKeysRead(&Io,&Pmic);
 if(Error){Label(Gop,1690,"PMIC READ FAILED RETURN FASTBOOT");return;}
 if(EFI_ERROR(DrawBootChoice(Gop,0,TRUE)) || EFI_ERROR(DrawBootChoice(Gop,1,FALSE)))return;
#if !DEGAS_ANDROID_EFI_BOOT
 AndroidRouteScreen(Gop,0);
#endif
 for(;;){
  MmioWrite32(0x1c00a008,0x1971);MicroSecondDelay(50000);
#if !DEGAS_ANDROID_EFI_BOOT
  if(++RouteTick%20==0)AndroidRouteScreen(Gop,RouteTick/20);
#else
  (VOID)RouteTick;
#endif
  Error=DegasPmicKeysRead(&Io,&Pmic);
  if(Error){DEBUG((DEBUG_ERROR,"DEGAS PMIC error=%u\n",Error));break;}
  Sample=((MmioRead32(0x10005210)&1)==0?1:0)|((Pmic&8)==0?2:0)|((Pmic&2)==0?4:0);
  if(Sample==Last){if(Stable<2)Stable++;}else{Stable=0;Last=Sample;}
  if(Stable<2 || Sample==Pressed)continue;
  Edges=Sample&~Pressed;Pressed=Sample;
#if DEGAS_WIFI_INIT_TEST
  if(Pressed==3 && !WifiDone){WifiDone=TRUE;WifiInitProbe(Gop);continue;}
#endif
  if(Edges==4){
   if(Selected==0){BootAndroid(Gop);continue;}
   UfsEnableTest(Gop,FALSE);return;
  }
  if(Edges!=1 && Edges!=2)continue;
  if(EFI_ERROR(DrawBootChoice(Gop,Selected,FALSE)))break;
  Selected^=1;
  if(EFI_ERROR(DrawBootChoice(Gop,Selected,TRUE)))break;
 }
 Label(Gop,1690,"PMIC READ FAILED RETURN FASTBOOT");
}
STATIC EFI_STATUS RunMenu(VOID){
 EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop; EFI_STATUS S;
 SerialPortWrite((UINT8 *)"DEGAS REAL DXE ENTRY\n",21);
 S=gBS->LocateProtocol(&gEfiGraphicsOutputProtocolGuid,NULL,(VOID **)&Gop);
 if(!EFI_ERROR(S)){
 EFI_GRAPHICS_OUTPUT_BLT_PIXEL Color={0,0,0,0};
 S=Gop->Blt(Gop,&Color,EfiBltVideoFill,0,0,0,0,1220,2712,0);
 SerialPortWrite((UINT8 *)(EFI_ERROR(S)?"DXE GOP FAILED\n":"DXE GOP OK\n"),EFI_ERROR(S)?15:11);
 }
 if(EFI_ERROR(S))return S;
 S=DrawMiLogo(Gop);if(EFI_ERROR(S))return S;
 ApJournalCanaryCheck(Gop);
 ButtonTest(Gop);
 return EFI_SUCCESS;
}
STATIC VOID EFIAPI MenuBdsEntry(EFI_BDS_ARCH_PROTOCOL *This){
 EFI_STATUS Status;
 UINT64 Mpidr,Typer;
 UINT32 Affinity;
 DEBUG((DEBUG_ERROR,"DEGAS REAL BDS ENTRY\n"));
 // Read-only evidence for future ACPI. The first GICR frame is already mapped
 // and used by GIC discovery. Do not wake or probe secondary redistributors.
 Mpidr=ArmReadMpidr();
 Affinity=(UINT32)((Mpidr&0xffffff)|((Mpidr>>8)&0xff000000));
 Typer=MmioRead64(0x0c440008);
 DEBUG((DEBUG_ERROR,"DEGAS BOOT CPU mpidr=%016lx midr=%08x cntfrq=%u\n",
  Mpidr,(UINT32)ArmReadMidr(),(UINT32)ArmReadCntFrq()));
 DEBUG((DEBUG_ERROR,"DEGAS BOOT GICR first=0c440000 typer=%016lx affinity=%08x boot_match=%u\n",
  Typer,(UINT32)(Typer>>32),(UINT32)((UINT32)(Typer>>32)==Affinity)));
 Status=BdsFinishDxe();
 if(!EFI_ERROR(Status))Status=RunMenu();
 DEBUG((DEBUG_ERROR,"DEGAS MENU EXIT %r WATCHDOG RETAINED\n",Status));
 BootWatchdogArmTenSeconds();
 // BDS never returns. Stop feeding WDT after END TEST or failure.
 for(;;)MicroSecondDelay(100000);
}
STATIC EFI_BDS_ARCH_PROTOCOL mBds={MenuBdsEntry};
EFI_STATUS EFIAPI CheckpointEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System){
 EFI_HANDLE Handle=NULL;
 mCheckpointImage=Image;
 return gBS->InstallMultipleProtocolInterfaces(&Handle,&gEfiBdsArchProtocolGuid,&mBds,NULL);
}
