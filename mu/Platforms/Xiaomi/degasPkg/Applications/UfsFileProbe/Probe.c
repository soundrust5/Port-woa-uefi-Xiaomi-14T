#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/DebugLib.h>
#include <Protocol/LoadedImage.h>

// The parent hands us a 16-byte request/reply mailbox in LoadOptions.
// No device writes, boot variables or persistent state are changed.
EFI_STATUS EFIAPI ProbeEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System){
 EFI_LOADED_IMAGE_PROTOCOL *Loaded;EFI_STATUS S;volatile UINT64 *Box;
 S=gBS->HandleProtocol(Image,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded);
 if(EFI_ERROR(S))return S;
 if(Loaded->LoadOptionsSize!=16 || Loaded->LoadOptions==NULL)return EFI_INVALID_PARAMETER;
 Box=Loaded->LoadOptions;
 if(Box[0]!=0x3151455246474544ULL || Box[1]!=0)return EFI_INVALID_PARAMETER;
 DEBUG((DEBUG_ERROR,"DEGAS EFI FILE APPLICATION ENTRY FROM UFS\n"));
 Box[1]=0x31504c5246474544ULL;
 return EFI_SUCCESS;
}
