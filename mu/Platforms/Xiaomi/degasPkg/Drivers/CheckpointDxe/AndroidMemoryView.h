// Android-only EFI memory view. The vendor DRM driver releases the complete
// DT videolfb reservation into the page allocator. Those pages must be RAM in
// Linux's linear map, not an omitted range or an EFI nomap framebuffer.
#define ANDROID_FB_BASE 0xfcc2f000ULL
#define ANDROID_FB_EFI_END 0xfd919000ULL
#define ANDROID_FB_END 0xfffff000ULL
#ifndef DEGAS_ANDROID_MAP_HOST_TEST
STATIC EFI_GET_MEMORY_MAP mAndroidOriginalGetMemoryMap;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *mAndroidViewInfo[8];
STATIC EFI_GRAPHICS_PIXEL_FORMAT mAndroidOriginalPixelFormat[8];
STATIC UINTN mAndroidViewCount;
#endif

STATIC EFI_STATUS AndroidProjectMemoryMap(EFI_MEMORY_DESCRIPTOR *Map,UINTN *Bytes,
 UINTN Capacity,UINTN Stride){
 UINTN I,Found=0;EFI_MEMORY_DESCRIPTOR *D;
 if(!Map || !Bytes || Stride<sizeof(*D) || *Bytes%Stride || Capacity<*Bytes)return EFI_COMPROMISED_DATA;
 if(*Bytes>MAX_UINTN-Stride)return EFI_OUT_OF_RESOURCES;
 // Check the entire map before changing any descriptor.
 for(I=0;I<*Bytes;I+=Stride){
  UINT64 End;D=(VOID *)((UINT8 *)Map+I);
  if(D->NumberOfPages>(MAX_UINT64-D->PhysicalStart)/EFI_PAGE_SIZE)return EFI_COMPROMISED_DATA;
  End=D->PhysicalStart+D->NumberOfPages*EFI_PAGE_SIZE;
  if(End<=ANDROID_FB_BASE || D->PhysicalStart>=ANDROID_FB_END)continue;
  if(D->PhysicalStart!=ANDROID_FB_BASE || End!=ANDROID_FB_EFI_END ||
     D->Type!=EfiReservedMemoryType || (D->Attribute&EFI_MEMORY_RUNTIME))return EFI_COMPROMISED_DATA;
  Found++;
 }
 if(Found!=1)return EFI_COMPROMISED_DATA;
 if(Capacity-*Bytes<Stride){*Bytes+=Stride;return EFI_BUFFER_TOO_SMALL;}
 for(I=0;I<*Bytes;I+=Stride){
  D=(VOID *)((UINT8 *)Map+I);
  if(D->PhysicalStart==ANDROID_FB_BASE){
   D->Type=EfiLoaderData;D->Attribute=EFI_MEMORY_WB;break;
  }
 }
 D=(VOID *)((UINT8 *)Map+*Bytes);ZeroMem(D,Stride);
 D->Type=EfiLoaderData;D->PhysicalStart=ANDROID_FB_EFI_END;
 D->NumberOfPages=(ANDROID_FB_END-ANDROID_FB_EFI_END)/EFI_PAGE_SIZE;
 D->Attribute=EFI_MEMORY_WB;*Bytes+=Stride;return EFI_SUCCESS;
}
#ifndef DEGAS_ANDROID_MAP_HOST_TEST
STATIC EFI_STATUS EFIAPI AndroidGetMemoryMap(UINTN *Bytes,EFI_MEMORY_DESCRIPTOR *Map,
 UINTN *Key,UINTN *Stride,UINT32 *Version){
 UINTN Capacity=Bytes?*Bytes:0;EFI_STATUS S;
 S=mAndroidOriginalGetMemoryMap(Bytes,Map,Key,Stride,Version);
 if(S==EFI_BUFFER_TOO_SMALL && Bytes && Stride && *Stride>=sizeof(*Map)){
  if(*Bytes>MAX_UINTN-*Stride)return EFI_OUT_OF_RESOURCES;
  *Bytes+=*Stride;return S;
 }
 if(EFI_ERROR(S))return S;
 return AndroidProjectMemoryMap(Map,Bytes,Capacity,*Stride);
}
STATIC VOID AndroidMemoryViewStop(VOID){
 if(mAndroidOriginalGetMemoryMap){
  gBS->GetMemoryMap=mAndroidOriginalGetMemoryMap;mAndroidOriginalGetMemoryMap=NULL;
  gBS->Hdr.CRC32=0;gBS->CalculateCrc32(gBS,gBS->Hdr.HeaderSize,&gBS->Hdr.CRC32);
 }
 while(mAndroidViewCount){UINTN I=--mAndroidViewCount;mAndroidViewInfo[I]->PixelFormat=mAndroidOriginalPixelFormat[I];mAndroidViewInfo[I]=NULL;}
}
STATIC EFI_STATUS AndroidMemoryViewStart(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_STATUS S;EFI_MEMORY_DESCRIPTOR *Map=NULL;UINTN Bytes=0,Key,Stride=0,Capacity;UINT32 Version;
 EFI_HANDLE *Handles=NULL;UINTN Count,I,J;EFI_GRAPHICS_OUTPUT_PROTOCOL *Other;
 if(mAndroidOriginalGetMemoryMap || !Gop || !Gop->Mode || !Gop->Mode->Info)return EFI_ALREADY_STARTED;
 if(Gop->Mode->FrameBufferBase!=ANDROID_FB_BASE)return EFI_UNSUPPORTED;
 S=gBS->GetMemoryMap(&Bytes,NULL,&Key,&Stride,&Version);
 if(S!=EFI_BUFFER_TOO_SMALL || Stride<sizeof(*Map))return EFI_COMPROMISED_DATA;
 if(Bytes>MAX_UINTN-8*Stride)return EFI_OUT_OF_RESOURCES;
 Capacity=Bytes+8*Stride;Map=AllocatePool(Capacity);if(!Map)return EFI_OUT_OF_RESOURCES;
 Bytes=Capacity;S=gBS->GetMemoryMap(&Bytes,Map,&Key,&Stride,&Version);
 if(!EFI_ERROR(S))S=AndroidProjectMemoryMap(Map,&Bytes,Capacity,Stride);
 FreePool(Map);if(EFI_ERROR(S))return S;
 // The native DT DRM driver owns this display. BLT remains available for EFI
 // text, but the Linux stub must not install a conflicting raw GOP screen_info.
 // Include any console-splitter GOP, which may retain its own mode-info copy.
 S=gBS->LocateHandleBuffer(ByProtocol,&gEfiGraphicsOutputProtocolGuid,NULL,&Count,&Handles);
 if(EFI_ERROR(S))return S;
 for(I=0;I<Count;I++){
  S=gBS->HandleProtocol(Handles[I],&gEfiGraphicsOutputProtocolGuid,(VOID **)&Other);
  if(EFI_ERROR(S) || !Other || !Other->Mode || !Other->Mode->Info){S=EFI_COMPROMISED_DATA;break;}
  for(J=0;J<mAndroidViewCount;J++)if(mAndroidViewInfo[J]==Other->Mode->Info)break;
  if(J<mAndroidViewCount)continue;
  if(mAndroidViewCount==ARRAY_SIZE(mAndroidViewInfo)){S=EFI_OUT_OF_RESOURCES;break;}
  J=mAndroidViewCount++;mAndroidViewInfo[J]=Other->Mode->Info;
  mAndroidOriginalPixelFormat[J]=Other->Mode->Info->PixelFormat;
  Other->Mode->Info->PixelFormat=PixelBltOnly;
 }
 FreePool(Handles);if(EFI_ERROR(S)){AndroidMemoryViewStop();return S;}
 mAndroidOriginalGetMemoryMap=gBS->GetMemoryMap;gBS->GetMemoryMap=AndroidGetMemoryMap;
 gBS->Hdr.CRC32=0;S=gBS->CalculateCrc32(gBS,gBS->Hdr.HeaderSize,&gBS->Hdr.CRC32);
 if(EFI_ERROR(S))AndroidMemoryViewStop();
 DEBUG((DEBUG_ERROR,"DEGAS ANDROID MEMORY VIEW full DT framebuffer mapped WB; GOP BLT only status=%r\n",S));
 return S;
}
#endif
