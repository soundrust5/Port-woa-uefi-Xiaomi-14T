// Temporary read-only LUN2 publication for the native DiskIo/GPT stack.
// Static storage outlives protocol handles even if disconnect fails.
STATIC BOOLEAN mUfsActive;
STATIC UINT32 mUfsReads,mUfsWriteAttempts;
STATIC EFI_BLOCK_IO_MEDIA mUfsMedia;
STATIC EFI_BLOCK_IO_PROTOCOL mUfsBio;
STATIC EFI_HANDLE mUfsHandle;
STATIC struct {
 VENDOR_DEVICE_PATH Vendor;
 EFI_DEVICE_PATH_PROTOCOL End;
} mUfsPath = {
 {{HARDWARE_DEVICE_PATH,HW_VENDOR_DP,{sizeof(VENDOR_DEVICE_PATH),0}},
  {0x905ee221,0x6e88,0x4cc9,{0xa7,0xdd,0x36,0x64,0x83,0xe1,0x2a,0x02}}},
 {END_DEVICE_PATH_TYPE,END_ENTIRE_DEVICE_PATH_SUBTYPE,{4,0}}
};
STATIC EFI_STATUS EFIAPI UfsBioReset(EFI_BLOCK_IO_PROTOCOL *This,BOOLEAN Extended){
 if(This!=&mUfsBio)return EFI_INVALID_PARAMETER;
 // Reset is deliberately unsupported: it must not silently reset active DMA.
 return EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI UfsBioWrite(EFI_BLOCK_IO_PROTOCOL *This,UINT32 Id,EFI_LBA Lba,UINTN Bytes,VOID *Buffer){
 mUfsWriteAttempts++;
 return EFI_WRITE_PROTECTED;
}
STATIC EFI_STATUS EFIAPI UfsBioFlush(EFI_BLOCK_IO_PROTOCOL *This){
 return This==&mUfsBio?EFI_SUCCESS:EFI_INVALID_PARAMETER;
}
STATIC EFI_STATUS EFIAPI UfsBioRead(EFI_BLOCK_IO_PROTOCOL *This,UINT32 Id,EFI_LBA Lba,UINTN Bytes,VOID *Buffer){
 UINTN Count,I,Chunk;EFI_TPL Old;EFI_STATUS Status=EFI_SUCCESS;UINT8 Q[32]={0};
 STATIC UINT64 TotalBytes,NextReport;UINT64 Start;
 volatile UINT8 *R=(volatile UINT8 *)(UINTN)0x44471080,*D=(volatile UINT8 *)(UINTN)0x44472000;
 if(This!=&mUfsBio)return EFI_INVALID_PARAMETER;
 if(Id!=mUfsMedia.MediaId)return EFI_MEDIA_CHANGED;
 if(!Bytes)return EFI_SUCCESS;
 if(!Buffer)return EFI_INVALID_PARAMETER;
 if(Bytes%4096)return EFI_BAD_BUFFER_SIZE;
 Count=Bytes/4096;
 if(Lba>mUfsMedia.LastBlock || Count-1>mUfsMedia.LastBlock-Lba)return EFI_INVALID_PARAMETER;
 Old=gBS->RaiseTPL(TPL_NOTIFY);
 if(!mUfsActive){Status=EFI_DEVICE_ERROR;goto Done;}
 Start=GetPerformanceCounter();
 for(I=0;I<Count;I+=Chunk,Lba+=Chunk){
  Chunk=MIN(Count-I,DEGAS_UFS_MAX_BLOCKS);
  DegasUfsReadCommand(Q,(UINT32)Lba,(UINT32)Chunk,(UINT8)(++mUfsReads));
  if(!UfsProbePacket(Q,(UINT32)(Chunk*4096)) || R[0]!=0x21 || R[2]!=2 || R[3]!=Q[3] ||
     R[6] || R[7] || R[8] || R[10] || R[11] || (R[1]&0x60) ||
     R[12] || R[13] || R[14] || R[15]){
   DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO read failed lba=%Lx\n",Lba));
   mUfsActive=FALSE;Status=EFI_DEVICE_ERROR;break;
  }
  UfsCopyRead((UINT8 *)Buffer+I*4096,D,Chunk*4096);
  TotalBytes+=Chunk*4096;
  if(TotalBytes>=NextReport){
   DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO progress bytes=%Lu lba=%Lx request_remaining=%Lu us=%Lu\n",TotalBytes,Lba,(UINT64)(Count-I-Chunk)*4096,GetTimeInNanoSecond(GetPerformanceCounter()-Start)/1000));
   NextReport=TotalBytes+0x1000000;
  }
  BootWatchdogFeed(); // Disk reads must respect the overall Windows test budget.
 }
 // Each progress line advances NextReport; it cannot also gate completion.
 if(EFI_ERROR(Status) || Bytes>=0x100000){
  DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO total_bytes=%Lu request_bytes=%Lu completed_blocks=%Lu end_lba=%Lx us=%Lu status=%r\n",
   TotalBytes,(UINT64)Bytes,(UINT64)I,Lba,GetTimeInNanoSecond(GetPerformanceCounter()-Start)/1000,Status));
  NextReport=TotalBytes+0x1000000;
 }
Done:
 gBS->RestoreTPL(Old);return Status;
}
STATIC BOOLEAN UfsBlockIoTest(BOOLEAN KeepAttached){
 EFI_STATUS S,Disconnect,Uninstall;EFI_HANDLE *Handles=NULL;UINTN Count=0,I,J,Found=0,Expected=0;
 EFI_PARTITION_ENTRY Entries[128];BOOLEAN Seen[128]={0},Good=FALSE;
 EFI_DEVICE_PATH_PROTOCOL *Path;EFI_PARTITION_INFO_PROTOCOL *Pi;EFI_BLOCK_IO_PROTOCOL *Child;
 ZeroMem(&mUfsMedia,sizeof(mUfsMedia));
 mUfsMedia.MediaId=2;mUfsMedia.MediaPresent=TRUE;mUfsMedia.ReadOnly=TRUE;
 mUfsMedia.BlockSize=4096;mUfsMedia.IoAlign=1;mUfsMedia.LastBlock=0x03d95fff;
 mUfsBio.Revision=EFI_BLOCK_IO_PROTOCOL_REVISION;mUfsBio.Media=&mUfsMedia;
 mUfsBio.Reset=UfsBioReset;mUfsBio.ReadBlocks=UfsBioRead;mUfsBio.WriteBlocks=UfsBioWrite;mUfsBio.FlushBlocks=UfsBioFlush;
 mUfsActive=TRUE;mUfsReads=0;mUfsWriteAttempts=0;
 // Read the already CRC-matched primary entry array before publishing handles.
 S=UfsBioRead(&mUfsBio,2,2,sizeof(Entries),Entries);
 if(EFI_ERROR(S))goto Done;
 for(I=0;I<128;I++)if(!IsZeroGuid(&Entries[I].PartitionTypeGUID))Expected++;
 if(Expected!=96){DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO expected partition count changed=%u\n",(UINT32)Expected));goto Done;}
 S=gBS->InstallMultipleProtocolInterfaces(&mUfsHandle,&gEfiBlockIoProtocolGuid,&mUfsBio,
   &gEfiDevicePathProtocolGuid,&mUfsPath,NULL);
 DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO install=%r\n",S));if(EFI_ERROR(S))goto Done;
 S=gBS->ConnectController(mUfsHandle,NULL,NULL,FALSE);
 DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO connect=%r\n",S));if(EFI_ERROR(S))goto Detach;
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiPartitionInfoProtocolGuid,NULL,&Count,&Handles);
 if(EFI_ERROR(S))goto Detach;
 Good=TRUE;
 for(I=0;I<Count;I++){
  if(EFI_ERROR(gBS->HandleProtocol(Handles[I],&gEfiDevicePathProtocolGuid,(VOID **)&Path)))continue;
  if(Path->Type!=HARDWARE_DEVICE_PATH || Path->SubType!=HW_VENDOR_DP ||
     Path->Length[0]!=sizeof(VENDOR_DEVICE_PATH) || Path->Length[1] ||
     CompareMem(Path,&mUfsPath.Vendor,sizeof(VENDOR_DEVICE_PATH)))continue;
  if(EFI_ERROR(gBS->HandleProtocol(Handles[I],&gEfiPartitionInfoProtocolGuid,(VOID **)&Pi)) || Pi->Type!=PARTITION_TYPE_GPT){Good=FALSE;break;}
  for(J=0;J<128;J++)if(!Seen[J] && !IsZeroGuid(&Entries[J].PartitionTypeGUID) &&
    !CompareMem(&Entries[J],&Pi->Info.Gpt,sizeof(EFI_PARTITION_ENTRY)))break;
  if(J==128){Good=FALSE;break;}Seen[J]=TRUE;
  if(EFI_ERROR(gBS->HandleProtocol(Handles[I],&gEfiBlockIoProtocolGuid,(VOID **)&Child)) ||
     !Child->Media->ReadOnly || !Child->Media->LogicalPartition || Child->Media->BlockSize!=4096 ||
     Child->Media->LastBlock!=Entries[J].EndingLBA-Entries[J].StartingLBA){Good=FALSE;break;}
  Found++;
  DEBUG((DEBUG_ERROR,"DEGAS GPT child index=%u start=%Lx end=%Lx readonly=1\n",(UINT32)J,Entries[J].StartingLBA,Entries[J].EndingLBA));
 }
 Good=Good && Found==Expected && mUfsActive && mUfsWriteAttempts==0;
 if(Good && KeepAttached){
  if(Handles)FreePool(Handles);
  DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO retained partitions=%u for Windows\n",(UINT32)Found));
  return TRUE;
 }
Detach:
 if(Handles)FreePool(Handles);
 Disconnect=gBS->DisconnectController(mUfsHandle,NULL,NULL);
 Uninstall=gBS->UninstallMultipleProtocolInterfaces(mUfsHandle,&gEfiBlockIoProtocolGuid,&mUfsBio,
   &gEfiDevicePathProtocolGuid,&mUfsPath,NULL);
 DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO detach=%r uninstall=%r\n",Disconnect,Uninstall));
 if(EFI_ERROR(Disconnect) || EFI_ERROR(Uninstall))Good=FALSE;
Done:
 mUfsActive=FALSE; // All future callbacks fail before touching the stopped HCI.
 DEBUG((DEBUG_ERROR,"DEGAS BLOCKIO result=%u partitions=%u expected=%u reads=%u blocked_writes=%u\n",Good,(UINT32)Found,(UINT32)Expected,mUfsReads,mUfsWriteAttempts));
 return Good;
}
