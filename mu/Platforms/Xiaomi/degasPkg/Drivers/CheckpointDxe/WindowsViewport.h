// Diagnostic-only inset for reading bugcheck parameters behind rounded corners.
// Keeps the inherited scanout, pitch and complete reserved VRAM range intact.
#ifndef DEGAS_WINDOWS_VIEWPORT_H
#define DEGAS_WINDOWS_VIEWPORT_H
#include <Protocol/GraphicsOutput.h>
#ifndef DEGAS_DIAGNOSTIC_INSET
#define DEGAS_DIAGNOSTIC_INSET 0
#endif
#if DEGAS_DIAGNOSTIC_INSET
#define DEGAS_VIEW_X 128U
#define DEGAS_VIEW_Y 196U
#define DEGAS_VIEW_WIDTH 964U
#define DEGAS_VIEW_HEIGHT 2320U
#define DEGAS_VIEW_OFFSET ((DEGAS_VIEW_Y * 1248ULL + DEGAS_VIEW_X) * 4ULL)
#define DEGAS_VIEW_BYTES (((DEGAS_VIEW_HEIGHT * 4992ULL) + 4095ULL) & ~4095ULL)
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL *mViewGop;
STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_BLT mViewOriginalBlt;
STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION mViewOriginalInfo;
STATIC EFI_PHYSICAL_ADDRESS mViewOriginalBase;
STATIC UINTN mViewOriginalSize;

STATIC BOOLEAN ViewFits (UINTN X, UINTN Y, UINTN W, UINTN H)
{
  return W != 0 && H != 0 && X < DEGAS_VIEW_WIDTH && Y < DEGAS_VIEW_HEIGHT &&
    W <= DEGAS_VIEW_WIDTH - X && H <= DEGAS_VIEW_HEIGHT - Y;
}

STATIC EFI_STATUS EFIAPI ViewBlt (
  EFI_GRAPHICS_OUTPUT_PROTOCOL *This, EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer,
  EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op, UINTN Sx, UINTN Sy, UINTN Dx,
  UINTN Dy, UINTN W, UINTN H, UINTN Delta)
{
  if (This != mViewGop || mViewOriginalBlt == NULL) return EFI_INVALID_PARAMETER;
  switch (Op) {
  case EfiBltVideoFill:
  case EfiBltBufferToVideo:
    if (!ViewFits (Dx, Dy, W, H)) return EFI_INVALID_PARAMETER;
    Dx += DEGAS_VIEW_X; Dy += DEGAS_VIEW_Y; break;
  case EfiBltVideoToBltBuffer:
    if (!ViewFits (Sx, Sy, W, H)) return EFI_INVALID_PARAMETER;
    Sx += DEGAS_VIEW_X; Sy += DEGAS_VIEW_Y; break;
  case EfiBltVideoToVideo:
    if (!ViewFits (Sx, Sy, W, H) || !ViewFits (Dx, Dy, W, H)) return EFI_INVALID_PARAMETER;
    Sx += DEGAS_VIEW_X; Sy += DEGAS_VIEW_Y;
    Dx += DEGAS_VIEW_X; Dy += DEGAS_VIEW_Y; break;
  default: return EFI_INVALID_PARAMETER;
  }
  // Original GOP still owns full-size BltLib configuration and buffer checks.
  return mViewOriginalBlt (This, Buffer, Op, Sx, Sy, Dx, Dy, W, H, Delta);
}

STATIC EFI_STATUS DegasWindowsInsetArm (VOID)
{
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop;
  EFI_STATUS S;
  if (mViewGop != NULL) return EFI_ALREADY_STARTED;
  S = gBS->LocateProtocol (&gEfiGraphicsOutputProtocolGuid, NULL, (VOID **)&Gop);
  if (EFI_ERROR (S)) return S;
  if (Gop == NULL || Gop->Mode == NULL || Gop->Mode->Info == NULL || Gop->Blt == NULL ||
      Gop->Mode->MaxMode != 1 || Gop->Mode->Mode != 0 ||
      Gop->Mode->FrameBufferBase != 0xfcc2f000ULL ||
      Gop->Mode->FrameBufferSize < 4992ULL * 2712ULL ||
      Gop->Mode->Info->HorizontalResolution != 1220 ||
      Gop->Mode->Info->VerticalResolution != 2712 ||
      Gop->Mode->Info->PixelsPerScanLine != 1248 ||
      Gop->Mode->Info->PixelFormat != PixelBlueGreenRedReserved8BitPerColor ||
      (DEGAS_VIEW_OFFSET & 4095) != 0 ||
      DEGAS_VIEW_OFFSET + DEGAS_VIEW_BYTES > Gop->Mode->FrameBufferSize) return EFI_UNSUPPORTED;
  mViewOriginalInfo = *Gop->Mode->Info;
  mViewOriginalBase = Gop->Mode->FrameBufferBase;
  mViewOriginalSize = Gop->Mode->FrameBufferSize;
  mViewOriginalBlt = Gop->Blt;
  mViewGop = Gop;
  // QueryMode copies this same Info in DegasGopDxe. SetMode(0) preserves it;
  // its native implementation only clears the original full framebuffer.
  Gop->Mode->Info->HorizontalResolution = DEGAS_VIEW_WIDTH;
  Gop->Mode->Info->VerticalResolution = DEGAS_VIEW_HEIGHT;
  Gop->Mode->FrameBufferBase += DEGAS_VIEW_OFFSET;
  Gop->Mode->FrameBufferSize = DEGAS_VIEW_BYTES;
  Gop->Blt = ViewBlt;
  return EFI_SUCCESS;
}

STATIC VOID DegasWindowsInsetDisarm (VOID)
{
  if (mViewGop == NULL) return;
  mViewGop->Blt = mViewOriginalBlt;
  *mViewGop->Mode->Info = mViewOriginalInfo;
  mViewGop->Mode->FrameBufferBase = mViewOriginalBase;
  mViewGop->Mode->FrameBufferSize = mViewOriginalSize;
  mViewGop = NULL; mViewOriginalBlt = NULL;
}
#else
// Normal Windows uses the original native GOP surface and framebuffer base.
STATIC EFI_STATUS DegasWindowsInsetArm(VOID){return EFI_SUCCESS;}
STATIC VOID DegasWindowsInsetDisarm(VOID){}
#endif
#endif
