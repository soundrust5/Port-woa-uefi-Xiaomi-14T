// First read-only WinPE boot through the real GPT ESP child handle.
#include "BootObservation.h"
#include <Guid/EventExitBootServiceFailed.h>
#include "../../Include/Protocol/DegasDisplayRefresh.h"
#include <Library/PrintLib.h>
#include "FrameThumbnail.h"
STATIC DEGAS_DISPLAY_REFRESH_PROTOCOL *mWindowsDisplayRefresh;
STATIC BOOLEAN mWindowsEbsEntered;
STATIC EFI_STATUS WindowsConsole(VOID){
 EFI_HANDLE *Handles=NULL;UINTN Count=0,I;EFI_STATUS S;
 EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *Text=NULL;
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiGraphicsOutputProtocolGuid,NULL,&Count,&Handles);
 if(EFI_ERROR(S))return S;
 S=EFI_NOT_FOUND;
 for(I=0;I<Count;I++){
  gBS->ConnectController(Handles[I],NULL,NULL,FALSE);
  S=gBS->HandleProtocol(Handles[I],&gEfiSimpleTextOutProtocolGuid,(VOID **)&Text);
  if(!EFI_ERROR(S)){
   gST->ConsoleOutHandle=Handles[I];gST->ConOut=Text;
   gST->StandardErrorHandle=Handles[I];gST->StdErr=Text;
   gST->Hdr.CRC32=0;
   S=gBS->CalculateCrc32(gST,gST->Hdr.HeaderSize,&gST->Hdr.CRC32);
   break;
  }
 }
 FreePool(Handles);
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS console=%r input_present=%u\n",S,(UINT32)(gST->ConIn!=NULL)));
 return S;
}
STATIC EFI_STATUS WindowsConsoleMessage(CONST CHAR16 *Message){
 EFI_STATUS S=WindowsConsole();
 if(EFI_ERROR(S))return S;
 S=gST->ConOut->SetAttribute(gST->ConOut,EFI_WHITE|EFI_BACKGROUND_BLACK);
 if(!EFI_ERROR(S))S=gST->ConOut->ClearScreen(gST->ConOut);
 if(!EFI_ERROR(S))S=gST->ConOut->SetCursorPosition(gST->ConOut,6,12);
 if(!EFI_ERROR(S))S=gST->ConOut->OutputString(gST->ConOut,(CHAR16 *)Message);
 DEBUG((DEBUG_ERROR,"DEGAS STANDARD CONSOLE OUTPUT=%r\n",S));
 return S;
}
STATIC VOID EFIAPI WindowsFeed(EFI_EVENT Event,VOID *Context){
 BootWatchdogFeed();
 BootObservation("timer",FALSE);
}
STATIC VOID EFIAPI WindowsBeforeExit(EFI_EVENT Event,VOID *Context){
 SerialPortWrite((UINT8 *)"DEGAS WINDOWS BEFORE EXIT BOOT SERVICES\n",sizeof("DEGAS WINDOWS BEFORE EXIT BOOT SERVICES\n")-1);
}
STATIC VOID EFIAPI WindowsExitFailed(EFI_EVENT Event,VOID *Context){
 GceWindowsStop();
 SerialPortWrite((UINT8 *)"DEGAS WINDOWS EXIT BOOT SERVICES FAILED\n",sizeof("DEGAS WINDOWS EXIT BOOT SERVICES FAILED\n")-1);
}
STATIC VOID EFIAPI WindowsExitBoot(EFI_EVENT Event,VOID *Context){
 EFI_STATUS DisplayStatus=EFI_UNSUPPORTED;
 UINT32 GceStatus=56;
 if(mWindowsEbsEntered)return;
 mWindowsEbsEntered=TRUE;
 // No allocation or disk access: shared EDK2 owns the core EBS transition.
 SerialPortWrite((UINT8 *)"DEGAS WINDOWS EXIT BOOT SERVICES\n",sizeof("DEGAS WINDOWS EXIT BOOT SERVICES\n")-1);
 BootObservation("ebs_event_before_handoff",TRUE);
 mBootObservationActive=FALSE;
 DegasOsHandoff();
 FrameThumbnail();
 // Last CPU frame before exclusive autonomous ownership. Both methods were
 // cached before StartImage; neither calls Boot Services or allocates.
 if(mWindowsDisplayRefresh && mWindowsDisplayRefresh->Flush)
  DisplayStatus=mWindowsDisplayRefresh->Flush(mWindowsDisplayRefresh);
 if(!EFI_ERROR(DisplayStatus)){
  DisplayStatus=mWindowsDisplayRefresh->LockCpuRefresh(mWindowsDisplayRefresh);
  if(!EFI_ERROR(DisplayStatus))GceStatus=GceWindowsStart();
 }
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS FINAL FRAME flush_lock=%r gce=%u autonomous=%u\n",DisplayStatus,GceStatus,mWindowsGceActive));
}
STATIC EFI_STATUS WindowsBoot(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_GUID EspGuid={0x3dce8afa,0x741e,0x4cc5,{0xba,0x5f,0xa2,0x04,0x4d,0xb3,0xba,0x74}};
 EFI_HANDLE *Handles=NULL,Esp=NULL;UINTN Count=0,I;
 EFI_PARTITION_INFO_PROTOCOL *Pi;EFI_DEVICE_PATH_PROTOCOL *Dp,*Path=NULL;
 EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;EFI_BOOT_MANAGER_LOAD_OPTION Option;
 BOOLEAN OptionReady=FALSE;
 EFI_EVENT Feed=NULL,ExitEvent=NULL,BeforeExit=NULL,ExitFailed=NULL;EFI_STATUS S;UINTN Matches=0;
 EFI_GUID DisplayRefreshGuid=DEGAS_DISPLAY_REFRESH_PROTOCOL_GUID;
 mBootBudgetStart=GetPerformanceCounter();mBootBudgetActive=TRUE;
 BootWatchdogArmTenSeconds();
 mWindowsDisplayRefresh=NULL;
 S=gBS->LocateProtocol(&DisplayRefreshGuid,NULL,(VOID **)&mWindowsDisplayRefresh);
 if(EFI_ERROR(S) || !mWindowsDisplayRefresh || !mWindowsDisplayRefresh->Flush || !mWindowsDisplayRefresh->LockCpuRefresh){
  mWindowsDisplayRefresh=NULL;
  if(!EFI_ERROR(S))S=EFI_UNSUPPORTED;
 }
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS DISPLAY flush_protocol=%r\n",S));
 if(EFI_ERROR(S))return S;
 Label(Gop,1690,"CONNECT WINDOWS CONSOLE");
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS before console\n"));
 S=WindowsConsole();if(EFI_ERROR(S))return S;
 Label(Gop,1690,"FIND WINDOWS ESP");
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiPartitionInfoProtocolGuid,NULL,&Count,&Handles);
 if(EFI_ERROR(S))return S;
 for(I=0;I<Count;I++){
  if(EFI_ERROR(gBS->HandleProtocol(Handles[I],&gEfiDevicePathProtocolGuid,(VOID **)&Dp)))continue;
  if(Dp->Type!=HARDWARE_DEVICE_PATH || Dp->SubType!=HW_VENDOR_DP ||
     DevicePathNodeLength(Dp)!=sizeof(VENDOR_DEVICE_PATH) ||
     CompareMem(Dp,&mUfsPath.Vendor,sizeof(VENDOR_DEVICE_PATH)))continue;
  if(EFI_ERROR(gBS->HandleProtocol(Handles[I],&gEfiPartitionInfoProtocolGuid,(VOID **)&Pi)))continue;
  if(Pi->Type!=PARTITION_TYPE_GPT || !CompareGuid(&Pi->Info.Gpt.UniquePartitionGUID,&EspGuid) ||
     Pi->Info.Gpt.StartingLBA!=29267968ULL || Pi->Info.Gpt.EndingLBA!=29792255ULL)continue;
  Esp=Handles[I];Matches++;
 }
 FreePool(Handles);if(Matches!=1)return EFI_NOT_FOUND;
 Label(Gop,1690,"CONNECT ESP FILESYSTEM");
 S=gBS->ConnectController(Esp,NULL,NULL,FALSE);
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS ESP connect=%r\n",S));
 S=gBS->HandleProtocol(Esp,&gEfiSimpleFileSystemProtocolGuid,(VOID **)&Fs);
 if(EFI_ERROR(S))return S;
 Path=FileDevicePath(Esp,L"\\EFI\\Microsoft\\Boot\\bootmgfw.efi");
 if(!Path)return EFI_OUT_OF_RESOURCES;
 S=gBS->CreateEvent(EVT_TIMER|EVT_NOTIFY_SIGNAL,TPL_CALLBACK,WindowsFeed,NULL,&Feed);
 if(EFI_ERROR(S))goto Done;
 S=gBS->SetTimer(Feed,TimerPeriodic,10000000);if(EFI_ERROR(S))goto Done;
 S=gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_NOTIFY,WindowsExitBoot,NULL,
     &gEfiEventExitBootServicesGuid,&ExitEvent);if(EFI_ERROR(S))goto Done;
 S=gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_NOTIFY,WindowsBeforeExit,NULL,
     &gEfiEventBeforeExitBootServicesGuid,&BeforeExit);if(EFI_ERROR(S))goto Done;
 S=gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_NOTIFY,WindowsExitFailed,NULL,
     &gEventExitBootServicesFailedGuid,&ExitFailed);if(EFI_ERROR(S))goto Done;
 ZeroMem(&Option,sizeof(Option));
 S=EfiBootManagerInitializeLoadOption(&Option,LoadOptionNumberUnassigned,
     LoadOptionTypeBoot,LOAD_OPTION_ACTIVE,L"Windows Boot Manager",Path,NULL,0);
 if(EFI_ERROR(S))goto Done;
 OptionReady=TRUE;
 S=WindowsConsoleMessage(L"STARTING WINDOWS BOOT MANAGER\r\n");
 if(EFI_ERROR(S))goto Done;
 BootPreflight();
 {
  UINT32 GceStatus=GceWindowsPrepare();
  DEBUG((DEBUG_ERROR,"DEGAS WINDOWS GCE PREPARE result=%u\n",GceStatus));
  if(GceStatus){S=EFI_DEVICE_ERROR;goto Done;}
 }
 {
  EFI_TPL Old=gBS->RaiseTPL(TPL_NOTIFY);
  mBootObservationCount=0;mBootObservationActive=TRUE;
  BootObservation("before_bootmgfw",TRUE);
  gBS->RestoreTPL(Old);
 }
 // Same Boot Manager library used by lancelot's standard BdsDxe:
 // BootCurrent, PreReadyToBoot/ReadyToBoot, image loading and StartImage.
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS EfiBootManagerBoot begin\n"));
 EfiBootManagerBoot(&Option);S=Option.Status;
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS EfiBootManagerBoot returned=%r reads=%u writes_blocked=%u\n",S,mUfsReads,mUfsWriteAttempts));
Done:
 // A successful EBS cannot legally return to a GOP/Boot Services menu.
 // Preserve RAM evidence. Recovery is manual when the EBS watchdog is disabled.
 if(mWindowsEbsEntered){
  GceWindowsStop();
  DEBUG((DEBUG_ERROR,"DEGAS WINDOWS RETURNED AFTER EBS RESET ONLY\n"));
  CpuDeadLoop();
 }
 {
  EFI_TPL Old=gBS->RaiseTPL(TPL_NOTIFY);
  BootObservation("boot_return",TRUE);mBootObservationActive=FALSE;
  gBS->RestoreTPL(Old);
 }
 if(OptionReady){
  if(Option.ExitData)FreePool(Option.ExitData);
  EfiBootManagerFreeLoadOption(&Option);
 }
 if(ExitEvent)gBS->CloseEvent(ExitEvent);
 if(BeforeExit)gBS->CloseEvent(BeforeExit);
 if(ExitFailed)gBS->CloseEvent(ExitFailed);
 if(Feed)gBS->CloseEvent(Feed);
 if(Path)FreePool(Path);
 DEBUG((DEBUG_ERROR,"DEGAS WINDOWS result=%r\n",S));return S;
}
