#include <Library/IoLib.h>
#include <Library/BaseLib.h>
#include <Library/TimerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PcdLib.h>

#include <Configuration/BootDevices.h>
#include "../../Include/Protocol/DegasDisplayRefresh.h"
#include "DegasOpaque.h"

//
// Global Variables
//
STATIC FRAME_BUFFER_CONFIGURE *FrameBufferConfiguration;
STATIC EFI_STATUS EFIAPI DegasDisplayFlush(DEGAS_DISPLAY_REFRESH_PROTOCOL *This);
STATIC EFI_STATUS EFIAPI DegasDisplayLockCpu(DEGAS_DISPLAY_REFRESH_PROTOCOL *This);
STATIC BOOLEAN mDegasCpuRefreshLocked;
STATIC DEGAS_DISPLAY_REFRESH_PROTOCOL mDegasDisplayRefresh={DegasDisplayFlush,DegasDisplayLockCpu};
STATIC EFI_GUID mDegasDisplayRefreshGuid=DEGAS_DISPLAY_REFRESH_PROTOCOL_GUID;

EFI_STATUS
FrameBufferQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                 ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info)
{
  EFI_STATUS Status;

  if (This == NULL || SizeOfInfo == NULL || Info == NULL) return EFI_INVALID_PARAMETER;
  if(mDegasCpuRefreshLocked)return EFI_ACCESS_DENIED;
  if (ModeNumber != 0) return EFI_INVALID_PARAMETER;

  // Allocate Memory
  Status = gBS->AllocatePool (EfiBootServicesData, sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION), (VOID *)Info);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Pass Info Size
  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  // Pass Frame Buffer Infos
  CopyMem (*Info, This->Mode->Info, sizeof (**Info));

  return EFI_SUCCESS;
}

EFI_STATUS
FrameBufferSetMode (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN UINT32                        ModeNumber)
{
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL Black = {0, 0, 0, 0};
  if (This == NULL || ModeNumber != 0) return EFI_UNSUPPORTED;
  // GOP SetMode must clear the visible display even when reselecting mode 0.
  return This->Blt (This, &Black, EfiBltVideoFill, 0, 0, 0, 0,
                   This->Mode->Info->HorizontalResolution,
                   This->Mode->Info->VerticalResolution, 0);
}


// Proven single-DSI/two-processing-pipe transfer from 2026-10-24 probes.
// Requires display MMIO mapped Device and framebuffer identity mapped.
STATIC BOOLEAN DegasDisplayIdle(VOID) {
  for (UINTN I=0; I<1000; ++I) {
    if ((MmioRead32(0x1400d00c) & BIT31)==0) return TRUE;
    MicroSecondDelay(100);
  }
  return FALSE;
}
STATIC BOOLEAN DegasDisplayHardwareGuard(VOID) {
  if (MmioRead32(0x14402f40)!=0xfcc2f000 ||
      MmioRead32(0x14602f40)!=0xfcc2f95c ||
      MmioRead32(0x14402038)!=0x0a98026d || MmioRead32(0x14602038)!=0x0a98026d ||
      MmioRead32(0x1400d014)!=0 ||
      MmioRead32(0x14001030)!=0xbc7423ff || MmioRead32(0x14201030)!=0x9c6083ff ||
      MmioRead32(0x14401030)!=0x8007 || MmioRead32(0x14601030)!=0x8007)
    return FALSE;
  return TRUE;
}
STATIC EFI_STATUS DegasDisplayPrepareOpaque(VOID){
  if(!DegasDisplayHardwareGuard())return EFI_UNSUPPORTED;
  if(!DegasDisplayIdle())return EFI_TIMEOUT;
  return DegasOvlSetupOpaque();
}
STATIC EFI_STATUS DegasDisplayRefresh(VOID) {
  if(mDegasCpuRefreshLocked)return EFI_ACCESS_DENIED;
  if(!DegasDisplayHardwareGuard())return EFI_UNSUPPORTED;
  if(mDegasOpaqueVerified){
    if(!DegasOvlOpaqueReady())return EFI_DEVICE_ERROR;
  }else if(!DegasOvlLegacyReady())return EFI_UNSUPPORTED;
  if (!DegasDisplayIdle()) return EFI_TIMEOUT;
  WriteBackDataCacheRange((VOID *)(UINTN)0xfcc2f000,4992U*2712U);
  MmioOr32(0x1400d190,2); MmioOr32(0x1400d190,4);
  MmioWrite32(0x1400d008,0); MmioAnd32(0x1400d014,~3U);
  MmioWrite32(0x1400d100,0x55); MmioOr32(0x1400d104,1);
  MmioWrite32(0x14201008,0); MmioWrite32(0x14201020,1);
  MmioWrite32(0x14601008,0); MmioWrite32(0x14601020,1);
  MmioWrite32(0x14001008,0); MmioWrite32(0x14001020,1);
  MmioWrite32(0x14401008,0); MmioWrite32(0x14401020,1);
  if (!DegasDisplayIdle()) return EFI_TIMEOUT;
  MmioWrite32(0x1400dd00,0x002c3909); MmioWrite32(0x1400d060,0x8001);
  MmioAnd32(0x1400d000,~1U); MmioOr32(0x1400d000,1);
  return DegasDisplayIdle() ? EFI_SUCCESS : EFI_TIMEOUT;
}

STATIC EFI_STATUS EFIAPI DegasDisplayFlush(DEGAS_DISPLAY_REFRESH_PROTOCOL *This){
  EFI_STATUS Status;
  if(This!=&mDegasDisplayRefresh)return EFI_INVALID_PARAMETER;
  if(mDegasCpuRefreshLocked)return EFI_ACCESS_DENIED;
  if(!mDegasOpaqueVerified){
    DEBUG((DEBUG_ERROR,"DEGAS DISPLAY direct flush unsupported: opaque setup not verified\n"));
    return EFI_UNSUPPORTED;
  }
  // Fresh latched completion avoids the BUSY-not-yet-asserted race observed
  // in V17. Full W0C clears only FRAME_DONE, matching the proven GCE packet.
  if(!DegasDisplayHardwareGuard() || !DegasOvlOpaqueReady())return EFI_UNSUPPORTED;
  if(!DegasDisplayIdle())return EFI_TIMEOUT;
  MmioWrite32(0x1400d00c,0xffffffefU);
  if(MmioRead32(0x1400d00c)&BIT4)return EFI_DEVICE_ERROR;
  // No setup, Boot Services, allocation or pixel stores in this callback.
  Status=DegasDisplayRefresh();
  if(!EFI_ERROR(Status)){
    Status=EFI_TIMEOUT;
    for(UINTN I=0;I<1000;I++){
      if((MmioRead32(0x1400d00c)&0x80000010U)==0x10U){Status=EFI_SUCCESS;break;}
      MicroSecondDelay(100);
    }
  }
  DEBUG((DEBUG_ERROR,"DEGAS DISPLAY direct snapshot flush=%r\n",Status));
  return Status;
}
STATIC EFI_STATUS EFIAPI DegasDisplayLockCpu(DEGAS_DISPLAY_REFRESH_PROTOCOL *This){
  if(This!=&mDegasDisplayRefresh)return EFI_INVALID_PARAMETER;
  mDegasCpuRefreshLocked=TRUE;
  MemoryFence();
  return EFI_SUCCESS;
}

EFI_STATUS
FrameBufferGopBlt (
  IN EFI_GRAPHICS_OUTPUT_PROTOCOL      *This,
  IN EFI_GRAPHICS_OUTPUT_BLT_PIXEL     *BltBuffer,
  IN EFI_GRAPHICS_OUTPUT_BLT_OPERATION  BltOperation OPTIONAL,
  IN UINTN                              SourceX,
  IN UINTN                              SourceY,
  IN UINTN                              DestinationX,
  IN UINTN                              DestinationY,
  IN UINTN                              Width,
  IN UINTN                              Height,
  IN UINTN                              Delta OPTIONAL)
{
  EFI_STATUS Status;
  EFI_TPL    Tpl;

  // Check before even calling Boot Services after display ownership transfer.
  if(mDegasCpuRefreshLocked)return EFI_ACCESS_DENIED;
  // Raise TPL Level
  Tpl = gBS->RaiseTPL (TPL_NOTIFY);

  if(BltOperation!=EfiBltVideoToBltBuffer){
    if(!DegasDisplayHardwareGuard()) {Status=EFI_UNSUPPORTED;goto Done;}
    if(mDegasOpaqueVerified){
      if(!DegasOvlOpaqueReady()){Status=EFI_DEVICE_ERROR;goto Done;}
    }else if(!DegasOvlLegacyReady()){Status=EFI_UNSUPPORTED;goto Done;}
  }

  // Draw to Frame Buffer
  Status = FrameBufferBlt (FrameBufferConfiguration, BltBuffer, BltOperation, SourceX, SourceY, DestinationX, DestinationY, Width, Height, Delta);

  if (!EFI_ERROR(Status) && BltOperation != EfiBltVideoToBltBuffer) {
    // Keep the old menu workaround only if opaque setup was unsupported or
    // safely rolled back. Verified opaque hardware never rewrites pixel bytes.
    if (DestinationX >= 1220 || DestinationY >= 2712 ||
        Width > 1220 - DestinationX || Height > 2712 - DestinationY) {
      Status = EFI_INVALID_PARAMETER;
    } else {
      if(!mDegasOpaqueVerified)for (UINTN Y = DestinationY; Y < DestinationY + Height; ++Y) {
        volatile UINT32 *Row = (volatile UINT32 *)(UINTN)(0xfcc2f000ULL + Y * 4992);
        for (UINTN X = DestinationX; X < DestinationX + Width; ++X) Row[X] |= 0xff000000U;
      }
      Status = DegasDisplayRefresh();
    }
  }

  // Restore TPL Level
Done:
  gBS->RestoreTPL (Tpl);

  return Status;
}

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL mFrameBuffer = {
  FrameBufferQueryMode,
  FrameBufferSetMode,
  FrameBufferGopBlt,
  NULL
};

EFI_STATUS
GetFrameBufferInfos (
  OUT EFI_PHYSICAL_ADDRESS *BaseAddress,
  OUT UINT32               *Width,
  OUT UINT32               *Height,
  OUT UINT32               *ColorDepth,
  OUT UINT32               *PixelsPerScanLine,
  OUT UINTN                *FrameBufferSize)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR FrameBufferRegion;
  UINT64                       BytesPerScanLine;
  UINT64                       SurfaceSize;

  // Locate "Display Reserved" Memory Region
  ZeroMem (&FrameBufferRegion, sizeof (FrameBufferRegion));
  Status = LocateMemoryRegionByName ("Display Reserved", &FrameBufferRegion);
  if (EFI_ERROR (Status)) {
    Status = LocateMemoryRegionByName ("Display_Reserved", &FrameBufferRegion);
  }

  // Verify Frame Buffer Region
  if (EFI_ERROR (Status) || (FrameBufferRegion.Address == 0) ||
      (FrameBufferRegion.Length == 0)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate \"Display Reserved\" Memory Region!\n"));
    return EFI_NOT_FOUND;
  }

  // Get Frame Buffer Details
  *Width      = FixedPcdGet32 (PcdFrameBufferWidth);
  *Height     = FixedPcdGet32 (PcdFrameBufferHeight);
  *ColorDepth = FixedPcdGet8 (PcdFrameBufferColorDepth);
  *PixelsPerScanLine = FixedPcdGet32 (PcdFrameBufferPixelsPerScanLine);
  if (*PixelsPerScanLine == 0) {
    *PixelsPerScanLine = *Width;
  }

  // Verify Frame Buffer Resolution
  if (*Width < 640 || *Height < 475) {
    DEBUG ((EFI_D_ERROR, "Frame Buffer Resolution is too Small!\n"));
    DEBUG ((EFI_D_ERROR, "Minimal Supported Resolution = 640x475\n"));

    return EFI_UNSUPPORTED;
  }

  if (*PixelsPerScanLine < *Width) {
    DEBUG ((EFI_D_ERROR, "Frame Buffer PixelsPerScanLine is smaller than the visible width!\n"));
    return EFI_UNSUPPORTED;
  }

  if ((*ColorDepth != 16) && (*ColorDepth != 24) && (*ColorDepth != 32)) {
    DEBUG ((EFI_D_ERROR, "Color Depth %u is not Supported!\n", *ColorDepth));
    return EFI_UNSUPPORTED;
  }

  BytesPerScanLine = (UINT64)*PixelsPerScanLine * (*ColorDepth / 8);
  if ((BytesPerScanLine > MAX_UINTN) ||
      ((*Height != 0) && (BytesPerScanLine > (MAX_UINT64 / *Height)))) {
    DEBUG ((EFI_D_ERROR, "Frame Buffer dimensions overflow the address space!\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  SurfaceSize = BytesPerScanLine * *Height;
  if ((SurfaceSize > MAX_UINTN) || (SurfaceSize > FrameBufferRegion.Length) ||
      (FrameBufferRegion.Address > MAX_UINTN) ||
      (FrameBufferRegion.Address > (MAX_UINTN - SurfaceSize))) {
    DEBUG ((EFI_D_ERROR, "Frame Buffer surface exceeds the Display Reserved region!\n"));
    return EFI_BAD_BUFFER_SIZE;
  }

  // Pass Frame Buffer Address
  *BaseAddress = FrameBufferRegion.Address;
  *FrameBufferSize = (UINTN)SurfaceSize;

  return EFI_SUCCESS;
}

EFI_STATUS
AllocateGopMemory ()
{
  EFI_STATUS Status;

  // Allocate Memory
  Status = gBS->AllocatePool (EfiBootServicesData, sizeof (EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE), (VOID *)&mFrameBuffer.Mode);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Allocate Memory for GOP Mode! Status = %r\n", Status));
    return Status;
  }

  // Clear Memory
  ZeroMem (mFrameBuffer.Mode, sizeof (EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE));

  // Allocate Memory
  Status = gBS->AllocatePool (EfiBootServicesData, sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION), (VOID *)&mFrameBuffer.Mode->Info);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Allocate Memory for GOP Info! Status = %r\n", Status));
    return Status;
  }

  // Clear Memory
  ZeroMem (mFrameBuffer.Mode->Info, sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION));

  return EFI_SUCCESS;
}

EFI_STATUS
SetFrameBufferModeDetails (
  IN EFI_PHYSICAL_ADDRESS BaseAddress,
  IN UINT32               Width,
  IN UINT32               Height,
  IN UINT32               ColorDepth,
  IN UINT32               PixelsPerScanLine,
  IN UINTN                FrameBufferSize)
{
  // Set Info Size
  mFrameBuffer.Mode->SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  // Set Mode Infos
  mFrameBuffer.Mode->MaxMode = 1;
  mFrameBuffer.Mode->Mode    = 0;

  // Set Frame Buffer Range
  mFrameBuffer.Mode->FrameBufferBase = BaseAddress;
  mFrameBuffer.Mode->FrameBufferSize = FrameBufferSize;

  // Set Mode Info Version
  mFrameBuffer.Mode->Info->Version = 0;

  // Set Frame Buffer Infos
  mFrameBuffer.Mode->Info->HorizontalResolution = Width;
  mFrameBuffer.Mode->Info->VerticalResolution   = Height;
  mFrameBuffer.Mode->Info->PixelsPerScanLine    = PixelsPerScanLine;

  // Set Pixel Format
  switch (ColorDepth) {
    case 32:
      mFrameBuffer.Mode->Info->PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
      break;

    case 24:
      mFrameBuffer.Mode->Info->PixelFormat                   = PixelBitMask;
      mFrameBuffer.Mode->Info->PixelInformation.RedMask      = 0x00FF0000;
      mFrameBuffer.Mode->Info->PixelInformation.GreenMask    = 0x0000FF00;
      mFrameBuffer.Mode->Info->PixelInformation.BlueMask     = 0x000000FF;
      mFrameBuffer.Mode->Info->PixelInformation.ReservedMask = 0x00000000;
      break;

    case 16:
      mFrameBuffer.Mode->Info->PixelFormat                   = PixelBitMask;
      mFrameBuffer.Mode->Info->PixelInformation.RedMask      = 0x0000F800;
      mFrameBuffer.Mode->Info->PixelInformation.GreenMask    = 0x000007E0;
      mFrameBuffer.Mode->Info->PixelInformation.BlueMask     = 0x0000001F;
      mFrameBuffer.Mode->Info->PixelInformation.ReservedMask = 0x00000000;
      break;

    default:
      DEBUG ((EFI_D_ERROR, "Color Depth %u is not Supported!\n", ColorDepth));
      return EFI_UNSUPPORTED;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
CreateFrameBufferConfig (IN EFI_PHYSICAL_ADDRESS BaseAddress)
{
  EFI_STATUS Status;

  // Set Configuration Size
  STATIC UINTN FrameBufferConfigurationSize = 0;

  // Get Frame Buffer Configuration Size
  FrameBufferBltConfigure ((VOID *)(UINTN)BaseAddress, mFrameBuffer.Mode->Info, FrameBufferConfiguration, &FrameBufferConfigurationSize);

  // Allocate Memory
  FrameBufferConfiguration = AllocatePool (FrameBufferConfigurationSize);
  if (FrameBufferConfiguration == NULL) {
    DEBUG ((EFI_D_ERROR, "Failed to Allocate Memory for the Frame Buffer Configuration!\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  // Create FrameBufferBltLib Configuration
  Status = FrameBufferBltConfigure ((VOID *)(UINTN)BaseAddress, mFrameBuffer.Mode->Info, FrameBufferConfiguration, &FrameBufferConfigurationSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Create the Frame Buffer Configuration! Status = %r\n", Status));
    return Status;
  }

  return EFI_SUCCESS;
}

VOID
FreeMemoryBuffers ()
{
  // Free Buffer
  if (mFrameBuffer.Mode->Info != NULL) {
    gBS->FreePool (mFrameBuffer.Mode->Info);
  }

  // Free Buffer
  if (mFrameBuffer.Mode != NULL) {
    gBS->FreePool (mFrameBuffer.Mode);
  }

  // Free Buffer
  if (FrameBufferConfiguration != NULL) {
    FreePool (FrameBufferConfiguration);
  }
}

EFI_STATUS
EFIAPI
RegisterFrameBuffer (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS BaseAddress;
  UINT32               Width;
  UINT32               Height;
  UINT32               ColorDepth;
  UINT32               PixelsPerScanLine;
  UINTN                FrameBufferSize;
  EFI_STATUS           OpaqueStatus;
  EFI_TPL              OpaqueTpl;

  // Get Frame Buffer Infos
  Status = GetFrameBufferInfos (
             &BaseAddress,
             &Width,
             &Height,
             &ColorDepth,
             &PixelsPerScanLine,
             &FrameBufferSize
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  // Allocate Memory
  Status = AllocateGopMemory ();
  if (EFI_ERROR (Status)) {
    goto cleanup;
  }

  // Set Frame Buffer Mode Details
  Status = SetFrameBufferModeDetails (
             BaseAddress,
             Width,
             Height,
             ColorDepth,
             PixelsPerScanLine,
             FrameBufferSize
             );
  if (EFI_ERROR (Status)) {
    goto cleanup;
  }

  // Create Frame Buffer Config
  Status = CreateFrameBufferConfig (BaseAddress);
  if (EFI_ERROR (Status)) {
    goto cleanup;
  }

  // Register GOP Protocol
  OpaqueTpl=gBS->RaiseTPL(TPL_NOTIFY);
  OpaqueStatus=DegasDisplayPrepareOpaque();
  gBS->RestoreTPL(OpaqueTpl);
  DEBUG((DEBUG_ERROR,"DEGAS OVL ALPHA setup=%r opaque=%u rollback_failed=%u\n",
    OpaqueStatus,(UINT32)mDegasOpaqueVerified,(UINT32)mDegasOpaqueRollbackFailed));
  Status = gBS->InstallMultipleProtocolInterfaces (&ImageHandle, &gEfiDevicePathProtocolGuid, &DisplayDevicePath, &gEfiGraphicsOutputProtocolGuid, &mFrameBuffer,
    &mDegasDisplayRefreshGuid,&mDegasDisplayRefresh,NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Register GOP Protocol!\n"));
    ASSERT_EFI_ERROR (Status);
  }

  return EFI_SUCCESS;

cleanup:
  // Free Memory Buffers
  FreeMemoryBuffers ();

  return Status;
}
