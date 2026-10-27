#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/SerialPortLib.h>
#include <Library/TimerLib.h>

#include "FrameBuffer.h"
#include "Font.h"

STATIC EFI_FRAME_BUFFER_POSITION  mCurrentPosition = { 0, 0 };

inline
STATIC
UINT8*
DrawPixel (
  IN UINT8  *Pixels,
  IN UINT32  PixelColor,
  IN UINT32  FontScale)
{
  // Repeat Action
  for (UINT32 i = 0; i < FontScale; i++) {
    // Save Pixel Color
    UINT32 Color = PixelColor;

    // Go theu each Byte
    for (UINT8 j = 0; j < FB_BPP; j++) {
      // Update Pixel Color
      *Pixels++ = (UINT8)Color;

      // Shift Pixel Color
      Color >>= 8;
    }
  }

  return Pixels;
}

inline
STATIC
UINT8*
DrawRow (
  IN UINT8  *Pixels,
  IN UINT32  RowData,
  IN UINT32  FontScale,
  IN UINTN   Stride)
{
  // Repeat Action
  for (UINT32 i = 0; i < FontScale; i++) {
    // Save Row Data
    UINT32 Data = RowData;

    // Go thru each Pixel
    for (UINT8 j = 0; j < FONT_WIDTH; j++) {
      // Set Pixel Color
      UINT32 PixelColor = (Data & 1) ? 0xFFFFFFFF : 0x00000000;
      
      // Draw Pixel Color
      Pixels = DrawPixel (Pixels, PixelColor, FontScale);

      // Shift Row
      Data >>= 1;
    }

    // Append Stride
    Pixels += Stride;
  }
  
  return Pixels;
}

VOID
DrawGlyph (
  IN UINT8  *Pixels,
  IN UINT64  Glyph,
  IN UINT32  FontScale,
  IN UINTN   PixelsPerScanLine)
{
  // Calculate Stride
  UINTN Stride = (PixelsPerScanLine - (FONT_WIDTH * FontScale)) * FB_BPP;

  // Split Glyph
  UINT32 TopGlyph    = (UINT32)(Glyph >> 32);
  UINT32 BottomGlyph = (UINT32)Glyph;

  // Set Inital Row Data
  UINT32 RowData = TopGlyph;

  // Go thru each Row
  for (UINT8 Row = 0; Row < 12; Row++) {
    // Switch Row Data
    if (Row == 6) {
      RowData = BottomGlyph;
    }

    // Draw Row
    Pixels = DrawRow (Pixels, RowData, FontScale, Stride);

    // Shift to next Row
    RowData >>= FONT_WIDTH;
  }
}

VOID
AdvanceNewLine (
  IN EFI_PHYSICAL_ADDRESS       FbBase,
  IN UINTN                      FbSize,
  IN EFI_FRAME_BUFFER_POSITION *CurrentPosition,
  IN EFI_FRAME_BUFFER_POSITION  MaxPosition)
{
  // Wait
  MicroSecondDelay (FB_DELAY_US);

  // Update Position
  CurrentPosition->XPos  = 0;
  CurrentPosition->YPos += 1;

  // Verify Y Position
  if (CurrentPosition->YPos >= MaxPosition.YPos) {
    // Clear Frame Buffer
    ZeroMem ((VOID *)FbBase, FbSize);

    // Reset X & Y Position
    CurrentPosition->XPos = 0;
    CurrentPosition->YPos = 0;
  }
}

UINT32
GetFontScale ()
{
  // Get Smaller Display Dimension
  UINT32 ShorterDimension = (FB_WIDTH < FB_HEIGHT) ? FB_WIDTH : FB_HEIGHT;

  // Return Calculated Font Scale
  return (ShorterDimension < 426) ? 1 : (ShorterDimension / 426);
}

VOID
GetFbPositions (
  IN  UINT32                      FontScale,
  OUT EFI_FRAME_BUFFER_POSITION  *MaxPosition)
{
  EFI_FRAME_BUFFER_POSITION LocalMaxPosition;

  // Calculate Max Positions
  LocalMaxPosition.XPos = FB_WIDTH  / ((FONT_WIDTH  + 1) * FontScale);
  LocalMaxPosition.YPos = FB_HEIGHT / ((FONT_HEIGHT - 4) * FontScale);

  // Reset a stale position without storing state in the framebuffer carveout.
  if ((mCurrentPosition.XPos >= LocalMaxPosition.XPos) ||
      (mCurrentPosition.YPos >= LocalMaxPosition.YPos)) {
    mCurrentPosition.XPos = 0;
    mCurrentPosition.YPos = 0;
  }

  // Pass Max Position
  *MaxPosition = LocalMaxPosition;
}

VOID
WriteFrameBuffer (
  IN EFI_PHYSICAL_ADDRESS FbBase,
  IN UINTN                FbSize,
  IN UINTN                PixelsPerScanLine,
  IN UINT8                Character)
{
  EFI_FRAME_BUFFER_POSITION  MaxPosition;
  UINT8                     *Pixels;

  // Set Glyph Font
  STATIC CONST UINT64 GlyphFont[] = GLYPH_FONT;

  // Set Glyph Font Scale
  UINT32 GlyphFontScale = GetFontScale ();

  // Get Frame Buffer Positions
  GetFbPositions (GlyphFontScale, &MaxPosition);

  // Skip Invalid ACSII Characters
  if (Character >= 127) {
    return;
  }

  // Handle Control Characters
  if (Character < 32) {
    if (Character == '\n') {
      AdvanceNewLine (FbBase, FbSize, &mCurrentPosition, MaxPosition);
    } else if (Character == '\r') {
      mCurrentPosition.XPos = 0;
    }

    return;
  }

  // Skip leading Spaces
  if (mCurrentPosition.XPos == 0 && Character == ' ') {
    return;
  }

  // Translate ACSII Character
  UINT64 Glyph = GlyphFont[Character - 32];

  // Calculate Glyph Dimension
  UINTN GlyphWidth  = (FONT_WIDTH  + 1) * GlyphFontScale;
  UINTN GlyphHeight = (FONT_HEIGHT - 4) * GlyphFontScale;

  // Set Glyph Pixels
  Pixels  = (VOID *)FbBase;
  Pixels += (UINTN)mCurrentPosition.YPos * GlyphHeight * (PixelsPerScanLine * FB_BPP);
  Pixels += (UINTN)mCurrentPosition.XPos * GlyphWidth  * FB_BPP;

  // Draw Glyph
  DrawGlyph (Pixels, Glyph, GlyphFontScale, PixelsPerScanLine);

  // Increase X Position
  mCurrentPosition.XPos++;

  // Check Max X Position
  if (mCurrentPosition.XPos >= MaxPosition.XPos) {
    AdvanceNewLine (FbBase, FbSize, &mCurrentPosition, MaxPosition);
  }
}

EFI_STATUS
GetFrameBufferMemory (
  OUT EFI_PHYSICAL_ADDRESS *Base,
  OUT UINTN                *Size,
  OUT UINTN                *PixelsPerScanLine)
{
  EFI_STATUS                   Status;
  EFI_MEMORY_REGION_DESCRIPTOR FbRegion;
  UINT32                       ColorDepth;
  UINT32                       FontScale;
  UINT32                       Height;
  UINT32                       Width;
  UINT64                       BytesPerScanLine;
  UINT64                       SurfaceSize;

  // Locate "Display Reserved" Memory Region
  ZeroMem (&FbRegion, sizeof (FbRegion));
  Status = LocateMemoryRegionByName ("Display Reserved", &FbRegion);
  if (EFI_ERROR (Status)) {
    Status = LocateMemoryRegionByName ("Display_Reserved", &FbRegion);
  }

  if (EFI_ERROR (Status) || (FbRegion.Address == 0) || (FbRegion.Length == 0)) {
    return EFI_NOT_FOUND;
  }

  Width      = FB_WIDTH;
  Height     = FB_HEIGHT;
  ColorDepth = FixedPcdGet8 (PcdFrameBufferColorDepth);
  *PixelsPerScanLine = FixedPcdGet32 (PcdFrameBufferPixelsPerScanLine);
  if (*PixelsPerScanLine == 0) {
    *PixelsPerScanLine = Width;
  }

  if ((Width == 0) || (Height == 0) ||
      ((*PixelsPerScanLine) < Width) ||
      ((ColorDepth != 16) && (ColorDepth != 24) && (ColorDepth != 32))) {
    return EFI_UNSUPPORTED;
  }

  FontScale = GetFontScale ();
  if ((Width < ((FONT_WIDTH + 1) * FontScale)) ||
      (Height < ((FONT_HEIGHT - 4) * FontScale))) {
    return EFI_UNSUPPORTED;
  }

  BytesPerScanLine = (UINT64)*PixelsPerScanLine * (ColorDepth / 8);
  if ((BytesPerScanLine > MAX_UINTN) ||
      ((Height != 0) && (BytesPerScanLine > (MAX_UINT64 / Height)))) {
    return EFI_BAD_BUFFER_SIZE;
  }

  SurfaceSize = BytesPerScanLine * Height;
  if ((SurfaceSize > MAX_UINTN) || (SurfaceSize > FbRegion.Length) ||
      (FbRegion.Address > MAX_UINTN) ||
      (FbRegion.Address > (MAX_UINTN - SurfaceSize))) {
    return EFI_BAD_BUFFER_SIZE;
  }

  // Pass Frame Buffer Memory
  *Base = FbRegion.Address;
  *Size = (UINTN)SurfaceSize;

  return EFI_SUCCESS;
}

UINTN
EFIAPI
SerialPortWrite (
  IN UINT8 *Buffer,
  IN UINTN  NumberOfBytes)
{
  EFI_STATUS           Status;
  EFI_PHYSICAL_ADDRESS FbBase;
  UINTN                FbSize;
  UINTN                PixelsPerScanLine;

  // Get Frame Buffer Memory
  Status = GetFrameBufferMemory (&FbBase, &FbSize, &PixelsPerScanLine);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  // Get Interrupts State
  BOOLEAN InterruptState = ArmGetInterruptState ();

  // Disable Interrupts
  if (InterruptState) {
    ArmDisableInterrupts ();
  }

  // Write Debug Message to Frame Buffer
  for (UINTN i = 0; i < NumberOfBytes; i++) {
    WriteFrameBuffer (FbBase, FbSize, PixelsPerScanLine, *Buffer++);
  }

  // Enable Interrupts
  if (InterruptState) {
    ArmEnableInterrupts ();
  }

  // Flush Frame Buffer
  WriteBackInvalidateDataCacheRange ((VOID *)FbBase, FbSize);

  return NumberOfBytes;
}

UINTN
EFIAPI
SerialPortRead (
  OUT UINT8 *Buffer,
  IN  UINTN  NumberOfBytes)
{
  return 0;
}

BOOLEAN
EFIAPI
SerialPortPoll ()
{
  return FALSE;
}

EFI_STATUS
EFIAPI
SerialPortSetControl (IN UINT32 Control)
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortGetControl (OUT UINT32 *Control)
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64             *BaudRate,
  IN OUT UINT32             *ReceiveFifoDepth,
  IN OUT UINT32             *Timeout,
  IN OUT EFI_PARITY_TYPE    *Parity,
  IN OUT UINT8              *DataBits,
  IN OUT EFI_STOP_BITS_TYPE *StopBits)
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SerialPortInitialize ()
{
  mCurrentPosition.XPos = 0;
  mCurrentPosition.YPos = 0;

  return EFI_SUCCESS;
}
