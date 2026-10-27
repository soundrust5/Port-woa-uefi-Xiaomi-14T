// Complete trusted DXE dispatch before loading any external EFI images.
// SecurityStubDxe defers disk images until EndOfDxe, and expects ReadyToLock
// immediately afterwards. Keep this ordering even on a platform without SMM.
STATIC VOID EFIAPI BdsEndNotify(EFI_EVENT Event,VOID *Context){}
STATIC EFI_STATUS BdsFinishDxe(VOID){
 EFI_EVENT Event;EFI_HANDLE LockHandle=NULL;EFI_STATUS S;
 S=gBS->CreateEventEx(EVT_NOTIFY_SIGNAL,TPL_CALLBACK,BdsEndNotify,NULL,
    &gEfiEndOfDxeEventGroupGuid,&Event);
 if(EFI_ERROR(S))return S;
 S=gBS->SignalEvent(Event);
 gBS->CloseEvent(Event);
 if(EFI_ERROR(S))return S;
 S=gBS->InstallProtocolInterface(&LockHandle,&gEfiDxeSmmReadyToLockProtocolGuid,
    EFI_NATIVE_INTERFACE,NULL);
 DEBUG((DEBUG_ERROR,"DEGAS BDS EndOfDxe signaled; ReadyToLock=%r\n",S));
 return S;
}
