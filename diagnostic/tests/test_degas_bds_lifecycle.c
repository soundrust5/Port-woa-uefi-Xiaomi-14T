#include <Uefi.h>
#include <Guid/EventGroup.h>
#include <Protocol/DxeSmmReadyToLock.h>
#include <assert.h>
#define DEBUG(x) ((void)0)
EFI_GUID gEfiEndOfDxeEventGroupGuid,gEfiDxeSmmReadyToLockProtocolGuid;
static unsigned step,scenario;
static EFI_STATUS EFIAPI Create(UINT32 Type,EFI_TPL Tpl,EFI_EVENT_NOTIFY Fn,CONST VOID *Ctx,CONST EFI_GUID *Group,EFI_EVENT *Event){
 assert(step++==0&&Type==EVT_NOTIFY_SIGNAL&&Tpl==TPL_CALLBACK&&Fn&&Group==&gEfiEndOfDxeEventGroupGuid);
 if(scenario==1)return EFI_OUT_OF_RESOURCES;
 *Event=(EFI_EVENT)(UINTN)1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI Signal(EFI_EVENT Event){assert(step++==1&&Event==(EFI_EVENT)(UINTN)1);return scenario==2?EFI_DEVICE_ERROR:EFI_SUCCESS;}
static EFI_STATUS EFIAPI Close(EFI_EVENT Event){assert(step++==2);return EFI_SUCCESS;}
static EFI_STATUS EFIAPI Install(EFI_HANDLE *Handle,EFI_GUID *Guid,EFI_INTERFACE_TYPE Type,VOID *Interface){
 assert(step++==3&&Guid==&gEfiDxeSmmReadyToLockProtocolGuid&&Type==EFI_NATIVE_INTERFACE&&!Interface);
 return scenario==3?EFI_OUT_OF_RESOURCES:EFI_SUCCESS;
}
static EFI_BOOT_SERVICES bs={.CreateEventEx=Create,.SignalEvent=Signal,.CloseEvent=Close,.InstallProtocolInterface=Install};
static EFI_BOOT_SERVICES *gBS=&bs;
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/BdsLifecycle.h"
int main(void){
 assert(BdsFinishDxe()==EFI_SUCCESS&&step==4);
 step=0;scenario=1;assert(BdsFinishDxe()==EFI_OUT_OF_RESOURCES&&step==1);
 step=0;scenario=2;assert(BdsFinishDxe()==EFI_DEVICE_ERROR&&step==3);
 step=0;scenario=3;assert(BdsFinishDxe()==EFI_OUT_OF_RESOURCES&&step==4);
 return 0;
}
