// FAT16 test volume inside the experimental boot_b image at +16 MiB.
// boot_b LBA444416..460799 is pinned by UfsReadProbe's Android GPT CRCs.
// No partition table edits: the slice is a temporary UEFI handle only.
#define UFS_FILE_BASE (444416ULL+4096ULL)
#define UFS_FILE_BLOCKS 8192ULL
STATIC EFI_BLOCK_IO_MEDIA mFileMedia;
STATIC EFI_BLOCK_IO_PROTOCOL mFileBio;
STATIC EFI_HANDLE mFileHandle;
STATIC struct { VENDOR_DEVICE_PATH Vendor;EFI_DEVICE_PATH_PROTOCOL End; } mFilePath={
 {{HARDWARE_DEVICE_PATH,HW_VENDOR_DP,{sizeof(VENDOR_DEVICE_PATH),0}},
  {0x905ee222,0x6e88,0x4cc9,{0xa7,0xdd,0x36,0x64,0x83,0xe1,0x2a,0x02}}},
 {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{4,0}}
};
STATIC EFI_STATUS EFIAPI FileRead(EFI_BLOCK_IO_PROTOCOL *This,UINT32 Id,EFI_LBA Lba,UINTN Bytes,VOID *Buffer){
 if(This!=&mFileBio)return EFI_INVALID_PARAMETER;
 if(Id!=3)return EFI_MEDIA_CHANGED;
 if(!Bytes)return EFI_SUCCESS;
 if(Bytes%4096)return EFI_BAD_BUFFER_SIZE;
 if(Lba>=UFS_FILE_BLOCKS || Bytes/4096-1>=UFS_FILE_BLOCKS-Lba)return EFI_INVALID_PARAMETER;
 return UfsBioRead(&mUfsBio,2,UFS_FILE_BASE+Lba,Bytes,Buffer);
}
STATIC EFI_STATUS EFIAPI FileReset(EFI_BLOCK_IO_PROTOCOL *This,BOOLEAN Extended){return EFI_UNSUPPORTED;}
STATIC EFI_STATUS EFIAPI FileFlush(EFI_BLOCK_IO_PROTOCOL *This){return EFI_SUCCESS;}
STATIC BOOLEAN UfsFileTest(VOID){
 EFI_STATUS S,D,U;BOOLEAN Good=FALSE;EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;
 EFI_FILE_PROTOCOL *Root=NULL,*File=NULL;EFI_HANDLE App=NULL;EFI_LOADED_IMAGE_PROTOCOL *Loaded;
 EFI_DEVICE_PATH_PROTOCOL *Path=NULL;UINTN Size;UINT8 Mz[2];UINT64 Box[2]={0x3151455246474544ULL,0};
 mFileMedia=mUfsMedia;mFileMedia.MediaId=3;mFileMedia.LastBlock=UFS_FILE_BLOCKS-1;
 mFileBio=mUfsBio;mFileBio.Media=&mFileMedia;mFileBio.ReadBlocks=FileRead;
 mFileBio.Reset=FileReset;mFileBio.FlushBlocks=FileFlush;
 mUfsActive=TRUE;
 S=gBS->InstallMultipleProtocolInterfaces(&mFileHandle,&gEfiBlockIoProtocolGuid,&mFileBio,
    &gEfiDevicePathProtocolGuid,&mFilePath,NULL);
 DEBUG((DEBUG_ERROR,"DEGAS FILE slice install=%r base=%Lx blocks=%Lx\n",S,UFS_FILE_BASE,UFS_FILE_BLOCKS));
 if(EFI_ERROR(S))goto Done;
 S=gBS->ConnectController(mFileHandle,NULL,NULL,FALSE);
 DEBUG((DEBUG_ERROR,"DEGAS FILE connect=%r\n",S));if(EFI_ERROR(S))goto Detach;
 S=gBS->HandleProtocol(mFileHandle,&gEfiSimpleFileSystemProtocolGuid,(VOID **)&Fs);
 DEBUG((DEBUG_ERROR,"DEGAS FILE filesystem=%r\n",S));if(EFI_ERROR(S))goto Detach;
 S=Fs->OpenVolume(Fs,&Root);if(EFI_ERROR(S))goto Detach;
 S=Root->Open(Root,&File,L"\\PROBE.EFI",EFI_FILE_MODE_READ,0);if(EFI_ERROR(S))goto Detach;
 Size=2;S=File->Read(File,&Size,Mz);
 DEBUG((DEBUG_ERROR,"DEGAS FILE open/read=%r bytes=%u\n",S,(UINT32)Size));
 if(EFI_ERROR(S) || Size!=2 || Mz[0]!='M' || Mz[1]!='Z')goto Detach;
 File->Close(File);File=NULL;Root->Close(Root);Root=NULL;
 Path=FileDevicePath(mFileHandle,L"\\PROBE.EFI");if(!Path)goto Detach;
 S=gBS->LoadImage(FALSE,mCheckpointImage,Path,NULL,0,&App);
 DEBUG((DEBUG_ERROR,"DEGAS FILE LoadImage=%r\n",S));if(EFI_ERROR(S))goto Detach;
 S=gBS->HandleProtocol(App,&gEfiLoadedImageProtocolGuid,(VOID **)&Loaded);
 if(EFI_ERROR(S) || Loaded->DeviceHandle!=mFileHandle)goto Detach;
 Loaded->LoadOptions=Box;Loaded->LoadOptionsSize=sizeof(Box);
 S=gBS->StartImage(App,NULL,NULL);
 // A returning application is unloaded by UEFI Exit(); do not dereference it.
 App=NULL;
 DEBUG((DEBUG_ERROR,"DEGAS FILE StartImage=%r reply=%Lx\n",S,Box[1]));
 Good=!EFI_ERROR(S) && Box[1]==0x31504c5246474544ULL && mUfsActive && !mUfsWriteAttempts;
Detach:
 if(App)gBS->UnloadImage(App);
 if(Path)FreePool(Path);
 if(File)File->Close(File);
 if(Root)Root->Close(Root);
 D=gBS->DisconnectController(mFileHandle,NULL,NULL);
 U=gBS->UninstallMultipleProtocolInterfaces(mFileHandle,&gEfiBlockIoProtocolGuid,&mFileBio,
    &gEfiDevicePathProtocolGuid,&mFilePath,NULL);
 DEBUG((DEBUG_ERROR,"DEGAS FILE detach=%r uninstall=%r\n",D,U));
 if(EFI_ERROR(D) || EFI_ERROR(U))Good=FALSE;
Done:
 mUfsActive=FALSE;
 DEBUG((DEBUG_ERROR,"DEGAS FILE result=%u blocked_writes=%u\n",Good,mUfsWriteAttempts));return Good;
}
