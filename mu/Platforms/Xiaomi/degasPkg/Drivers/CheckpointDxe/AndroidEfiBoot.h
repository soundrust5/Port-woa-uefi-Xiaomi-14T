// Direct stock GKI EFI-stub boot. No partition/slot writes or reset markers.
#include <Guid/Fdt.h>
#include <Guid/LinuxEfiInitrdMedia.h>
#include <Protocol/LoadFile2.h>
#include <Library/FdtLib.h>
#include "AndroidMemoryView.h"

#define ANDROID_IMAGE_BASE 0x42200000ULL
#define ANDROID_IMAGE_SIZE 0x21caa00U
#define ANDROID_IMAGE_CRC  0xd83afbc1U
#define ANDROID_FDT_BASE   0x47c80000ULL
#define ANDROID_FDT_LIMIT  0x80000U
#define ANDROID_INITRD_MIN 0x66f00000ULL
#define ANDROID_INITRD_MAX 0x6af00000ULL

STATIC VOID *mAndroidInitrd;
STATIC UINTN mAndroidInitrdSize;
STATIC BOOLEAN mAndroidEbsObserved;
STATIC BOOLEAN mAndroidEfiActive;
STATIC struct {
 VENDOR_DEVICE_PATH Vendor;
 EFI_DEVICE_PATH_PROTOCOL End;
} mAndroidInitrdPath={
 {{MEDIA_DEVICE_PATH,MEDIA_VENDOR_DP,{sizeof(VENDOR_DEVICE_PATH),0}},LINUX_EFI_INITRD_MEDIA_GUID},
 {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{sizeof(EFI_DEVICE_PATH_PROTOCOL),0}}
};
STATIC EFI_STATUS EFIAPI AndroidInitrdLoad(
 EFI_LOAD_FILE2_PROTOCOL *This,EFI_DEVICE_PATH_PROTOCOL *Path,BOOLEAN BootPolicy,
 UINTN *Size,VOID *Buffer){
 (VOID)This;
 if(!Path || !Size)return EFI_INVALID_PARAMETER;
 if(BootPolicy)return EFI_UNSUPPORTED;
 if(!IsDevicePathEnd(Path))return EFI_NOT_FOUND;
 if(!mAndroidInitrd || !mAndroidInitrdSize)return EFI_NOT_FOUND;
 if(!Buffer || *Size<mAndroidInitrdSize){*Size=mAndroidInitrdSize;return EFI_BUFFER_TOO_SMALL;}
 CopyMem(Buffer,mAndroidInitrd,mAndroidInitrdSize);*Size=mAndroidInitrdSize;
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI initrd copied bytes=%Lu\n",(UINT64)*Size));
 return EFI_SUCCESS;
}
STATIC EFI_LOAD_FILE2_PROTOCOL mAndroidInitrdProtocol={AndroidInitrdLoad};
STATIC BOOLEAN AndroidFdtAddress(CONST VOID *Fdt,INT32 Node,CONST CHAR8 *Name,UINT64 *Value){
 INT32 Length;CONST UINT8 *P=FdtGetProp(Fdt,Node,Name,&Length);UINT64 V=0;
 if(!P || (Length!=4 && Length!=8))return FALSE;
 for(INT32 I=0;I<Length;I++)V=(V<<8)|P[I];*Value=V;return TRUE;
}
STATIC VOID AndroidWatchdogStop(VOID){
 UINT32 Mode=MmioRead32(0x1c00a000);
 MmioWrite32(0x1c00a000,0x22000000u|((Mode&0x00ffffffu)&~1u));MemoryFence();
}
STATIC VOID EFIAPI AndroidEfiExit(EFI_EVENT Event,VOID *Context){
 (VOID)Event;(VOID)Context;if(!mAndroidEfiActive)return;mAndroidEbsObserved=TRUE;
 AndroidWatchdogStop();
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI EXIT BOOT SERVICES; no Windows context patch or autonomous GCE start\n"));
}
STATIC EFI_STATUS AndroidEfiBoot(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 STATIC CONST CHAR8 Extra[]=" acpi=off efi=noruntime";
 VOID *Image=(VOID *)(UINTN)ANDROID_IMAGE_BASE,*Fdt=(VOID *)(UINTN)ANDROID_FDT_BASE;
 VOID *Copy=NULL,*OldFdt=NULL;CONST CHAR8 *Args;CHAR16 *Options=NULL;
 EFI_HANDLE InitrdHandle=NULL,Kernel=NULL;EFI_LOADED_IMAGE_PROTOCOL *Loaded;
 EFI_EVENT ExitEvent=NULL;EFI_STATUS S;INT32 Chosen,Length;UINT32 Crc,Total,Stage=0;
 UINT64 Start,End;UINTN I,N;BOOLEAN TableInstalled=FALSE,KeepOptions=FALSE,KeepFdt=FALSE;CHAR8 Text[64];EFI_STATUS Cleanup;
 struct {MEMMAP_DEVICE_PATH Memory;EFI_DEVICE_PATH_PROTOCOL End;} Path={
  {{HARDWARE_DEVICE_PATH,HW_MEMMAP_DP,{sizeof(MEMMAP_DEVICE_PATH),0}},EfiBootServicesData,
   ANDROID_IMAGE_BASE,ANDROID_IMAGE_BASE+ANDROID_IMAGE_SIZE-1},
  {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{sizeof(EFI_DEVICE_PATH_PROTOCOL),0}}
 };
 Label(Gop,1690,"ANDROID EFI VALIDATING");BootWatchdogFeed();
 // The kernel's EFI stub uses the standard system-table console. The custom
 // GOP menu alone does not establish ConOut; reuse the qualified connector.
 S=WindowsConsole();if(EFI_ERROR(S))goto Done;
 S=EFI_COMPROMISED_DATA;
 if(ReadUnaligned16(Image)!=0x5a4d || ReadUnaligned32((UINT32 *)((UINT8 *)Image+56))!=0x644d5241)goto Done;
 S=gBS->CalculateCrc32(Image,ANDROID_IMAGE_SIZE,&Crc);if(EFI_ERROR(S))goto Done;
 if(Crc!=ANDROID_IMAGE_CRC){S=EFI_CRC_ERROR;goto Done;}
 Stage=1;S=EFI_COMPROMISED_DATA;
 if(FdtCheckHeader(Fdt))goto Done;
 Total=FdtTotalSize(Fdt);if(Total<40 || Total>ANDROID_FDT_LIMIT)goto Done;
 Chosen=FdtPathOffset(Fdt,"/chosen");if(Chosen<0)goto Done;
 Stage=2;
 // Refuse a different LK display allocation: the scoped EFI view is qualified
 // only for the captured complete videolfb reservation.
 {UINT64 Fb,Bytes;
  if(!AndroidFdtAddress(Fdt,Chosen,"atag,videolfb-fb_base_l",&Fb) || Fb!=ANDROID_FB_BASE ||
     !AndroidFdtAddress(Fdt,Chosen,"atag,videolfb-vramSize",&Bytes) || Bytes!=ANDROID_FB_END-ANDROID_FB_BASE)goto Done;
 }
 if(!AndroidFdtAddress(Fdt,Chosen,"linux,initrd-start",&Start) ||
    !AndroidFdtAddress(Fdt,Chosen,"linux,initrd-end",&End) ||
    Start<ANDROID_INITRD_MIN || Start>=End || End>ANDROID_INITRD_MAX)goto Done;
 mAndroidInitrd=(VOID *)(UINTN)Start;mAndroidInitrdSize=(UINTN)(End-Start);
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI source CRC=%08x fdt_bytes=%u initrd=%lx..%lx\n",Crc,Total,Start,End));
 Stage=3;Copy=AllocatePages(EFI_SIZE_TO_PAGES(Total+4096));S=EFI_OUT_OF_RESOURCES;if(!Copy)goto Done;
 if(FdtOpenInto(Fdt,Copy,(INT32)(Total+4096))){S=EFI_COMPROMISED_DATA;goto Done;}
 Stage=4;Args=FdtGetProp(Fdt,Chosen,"bootargs",&Length);S=EFI_COMPROMISED_DATA;
 if(!Args || Length<1 || Length>8192 || Args[Length-1]!=0 || AsciiStrnLenS(Args,Length)!=(UINTN)Length-1)goto Done;
 N=(UINTN)Length-1+sizeof(Extra);Options=AllocateZeroPool(N*sizeof(CHAR16));S=EFI_OUT_OF_RESOURCES;if(!Options)goto Done;
 for(I=0;I<(UINTN)Length-1;I++){if((UINT8)Args[I]>127){S=EFI_UNSUPPORTED;goto Done;}Options[I]=(CHAR16)Args[I];}
 for(UINTN J=0;J<sizeof(Extra);J++)Options[I+J]=(CHAR16)Extra[J];
 for(I=0;I<gST->NumberOfTableEntries;I++)if(CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gFdtTableGuid))OldFdt=gST->ConfigurationTable[I].VendorTable;
 Stage=5;S=gBS->InstallConfigurationTable(&gFdtTableGuid,Copy);if(EFI_ERROR(S))goto Done;TableInstalled=TRUE;
 Stage=6;S=gBS->InstallMultipleProtocolInterfaces(&InitrdHandle,
  &gEfiDevicePathProtocolGuid,&mAndroidInitrdPath,&gEfiLoadFile2ProtocolGuid,&mAndroidInitrdProtocol,NULL);if(EFI_ERROR(S))goto Done;
 Stage=7;Label(Gop,1690,"ANDROID EFI LOAD KERNEL");BootWatchdogFeed();
 S=gBS->LoadImage(FALSE,mCheckpointImage,(EFI_DEVICE_PATH_PROTOCOL *)&Path,Image,ANDROID_IMAGE_SIZE,&Kernel);if(EFI_ERROR(S))goto Done;
 Stage=8;S=gBS->HandleProtocol(Kernel,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded);if(EFI_ERROR(S))goto Done;
 Loaded->LoadOptions=Options;Loaded->LoadOptionsSize=(UINT32)(N*sizeof(CHAR16));
 Stage=9;mAndroidEbsObserved=FALSE;mAndroidEfiActive=TRUE;
 S=gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_NOTIFY,AndroidEfiExit,NULL,&gEfiEventExitBootServicesGuid,&ExitEvent);if(EFI_ERROR(S))goto Done;
 Stage=10;Label(Gop,1690,"ANDROID EFI START");
 S=AndroidMemoryViewStart(Gop);if(EFI_ERROR(S))goto Done;
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI START IMAGE base=%p bytes=%Lu\n",Loaded->ImageBase,(UINT64)Loaded->ImageSize));
 gBS->SetWatchdogTimer(0,0,0,NULL);AndroidWatchdogStop();
 S=gBS->StartImage(Kernel,NULL,NULL);
 if(mAndroidEbsObserved){DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI unexpected post-EBS return\n"));CpuDeadLoop();}
 if(!EFI_ERROR(S))S=EFI_ABORTED;
Done:
 mAndroidEfiActive=FALSE;
 AndroidMemoryViewStop();
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI return stage=%u status=%r\n",Stage,S));
 if(ExitEvent)gBS->CloseEvent(ExitEvent);
 if(Kernel){Cleanup=gBS->UnloadImage(Kernel);if(EFI_ERROR(Cleanup)){KeepOptions=TRUE;DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI unload=%r retaining load options\n",Cleanup));}}
 if(InitrdHandle)gBS->UninstallMultipleProtocolInterfaces(InitrdHandle,
  &gEfiDevicePathProtocolGuid,&mAndroidInitrdPath,&gEfiLoadFile2ProtocolGuid,&mAndroidInitrdProtocol,NULL);
 if(TableInstalled){Cleanup=gBS->InstallConfigurationTable(&gFdtTableGuid,OldFdt);if(EFI_ERROR(Cleanup)){KeepFdt=TRUE;DEBUG((DEBUG_ERROR,"DEGAS ANDROID EFI table restore=%r retaining FDT allocation\n",Cleanup));}}
 if(Options && !KeepOptions)FreePool(Options);if(Copy && !KeepFdt)FreePages(Copy,EFI_SIZE_TO_PAGES(Total+4096));
 mAndroidInitrd=NULL;mAndroidInitrdSize=0;
 AsciiSPrint(Text,sizeof(Text),"ANDROID EFI S %u E %lx",Stage,(UINT64)S);Label(Gop,1690,Text);
 BootWatchdogArmTenSeconds();return S;
}
