"""Compile and execute the current Mu framebuffer geometry function bodies."""

from pathlib import Path
import re
import shlex
import subprocess


ROOT = Path(__file__).resolve().parents[2]
MU_PACKAGE = ROOT / "mu/Silicon/Silicium/SiliciumPkg"
SIMPLE_FB_SOURCE = MU_PACKAGE / "Drivers/SimpleFbDxe/SimpleFb.c"
SERIAL_FB_SOURCE = (
    MU_PACKAGE
    / "Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib.c"
)
BUILD_DIR = ROOT / "diagnostic/build"
HARNESS_PATH = BUILD_DIR / "mu-geometry-harness.c"
BINARY_PATH = BUILD_DIR / "test_mu_geometry"


def extract_function_body(source: str, function_name: str) -> str:
    """Return a definition's balanced outer braces, ignoring comments/strings."""
    match = re.search(r"\b" + re.escape(function_name) + r"\s*\(", source)
    if match is None:
        raise AssertionError(f"function not found: {function_name}")

    opening = source.find("{", match.end())
    if opening < 0:
        raise AssertionError(f"function body not found: {function_name}")

    depth = 0
    index = opening
    state = "code"
    quote = ""
    while index < len(source):
        char = source[index]
        following = source[index + 1] if index + 1 < len(source) else ""

        if state == "line_comment":
            if char == "\n":
                state = "code"
        elif state == "block_comment":
            if char == "*" and following == "/":
                state = "code"
                index += 1
        elif state == "quoted":
            if char == "\\":
                index += 1
            elif char == quote:
                state = "code"
        elif char == "/" and following == "/":
            state = "line_comment"
            index += 1
        elif char == "/" and following == "*":
            state = "block_comment"
            index += 1
        elif char in {'"', "'"}:
            state = "quoted"
            quote = char
        elif char == "{":
            depth += 1
        elif char == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]

        index += 1

    raise AssertionError(f"unbalanced function body: {function_name}")


def create_harness() -> str:
    simple_source = SIMPLE_FB_SOURCE.read_text(encoding="utf-8")
    serial_source = SERIAL_FB_SOURCE.read_text(encoding="utf-8")
    simple_body = extract_function_body(simple_source, "GetFrameBufferInfos")
    scale_body = extract_function_body(serial_source, "GetFontScale")
    serial_body = extract_function_body(serial_source, "GetFrameBufferMemory")

    preamble = r'''/* Generated from the current Mu sources by test_mu_geometry.py. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int64_t INT64;
typedef uintptr_t UINTN;
typedef UINT64 EFI_STATUS;
typedef UINT64 EFI_PHYSICAL_ADDRESS;

#define IN
#define OUT
#define EFI_SUCCESS UINT64_C(0)
#define EFIERR(Code) (UINT64_C(0x8000000000000000) | (Code))
#define EFI_UNSUPPORTED EFIERR(3)
#define EFI_BAD_BUFFER_SIZE EFIERR(4)
#define EFI_NOT_FOUND EFIERR(14)
#define EFI_ERROR(Status) (((INT64)(Status)) < 0)
#define MAX_UINTN UINTPTR_MAX
#define MAX_UINT64 UINT64_MAX
#define EFI_D_ERROR 0
#define DEBUG(...) ((void)0)

typedef struct {
  EFI_PHYSICAL_ADDRESS Address;
  UINT64 Length;
} EFI_MEMORY_REGION_DESCRIPTOR;

enum {
  PcdFrameBufferWidth = 1,
  PcdFrameBufferHeight,
  PcdFrameBufferColorDepth,
  PcdFrameBufferPixelsPerScanLine
};

static UINT32 gWidth;
static UINT32 gHeight;
static UINT32 gPixelsPerScanLine;
static UINT8 gColorDepth;
static EFI_MEMORY_REGION_DESCRIPTOR gRegion;
static unsigned int gRegionMode;

static UINT32 FixedPcdGet32Impl(unsigned int Token)
{
  switch (Token) {
    case PcdFrameBufferWidth:
      return gWidth;
    case PcdFrameBufferHeight:
      return gHeight;
    case PcdFrameBufferPixelsPerScanLine:
      return gPixelsPerScanLine;
    default:
      assert(0 && "unexpected FixedPcdGet32 token");
      return 0;
  }
}

static UINT8 FixedPcdGet8Impl(unsigned int Token)
{
  assert(Token == PcdFrameBufferColorDepth);
  return gColorDepth;
}

#define FixedPcdGet32(Token) FixedPcdGet32Impl(Token)
#define FixedPcdGet8(Token) FixedPcdGet8Impl(Token)

static void ZeroMem(void *Buffer, UINTN Size)
{
  memset(Buffer, 0, Size);
}

static EFI_STATUS
LocateMemoryRegionByName(const char *Name, EFI_MEMORY_REGION_DESCRIPTOR *Region)
{
  int primary = strcmp(Name, "Display Reserved") == 0;
  int fallback = strcmp(Name, "Display_Reserved") == 0;

  if ((gRegionMode == 1U && primary) || (gRegionMode == 2U && fallback)) {
    *Region = gRegion;
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

#include "mu/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/FrameBuffer.h"
#include "mu/Silicon/Silicium/SiliciumPkg/Library/FrameBufferSerialPortLib/Font.h"
'''

    definitions = (
        "\nUINT32\nGetFontScale (void)\n"
        + scale_body
        + "\n\nEFI_STATUS\nGetFrameBufferInfos (\n"
        + "  OUT EFI_PHYSICAL_ADDRESS *BaseAddress,\n"
        + "  OUT UINT32 *Width,\n"
        + "  OUT UINT32 *Height,\n"
        + "  OUT UINT32 *ColorDepth,\n"
        + "  OUT UINT32 *PixelsPerScanLine,\n"
        + "  OUT UINTN *FrameBufferSize)\n"
        + simple_body
        + "\n\nEFI_STATUS\nGetFrameBufferMemory (\n"
        + "  OUT EFI_PHYSICAL_ADDRESS *Base,\n"
        + "  OUT UINTN *Size,\n"
        + "  OUT UINTN *PixelsPerScanLine)\n"
        + serial_body
        + "\n"
    )

    tests = r'''
static void
Configure(UINT32 Width, UINT32 Height, UINT32 PixelsPerScanLine,
          UINT8 ColorDepth, UINT64 Address, UINT64 Length, unsigned int Mode)
{
  gWidth = Width;
  gHeight = Height;
  gPixelsPerScanLine = PixelsPerScanLine;
  gColorDepth = ColorDepth;
  gRegion.Address = Address;
  gRegion.Length = Length;
  gRegionMode = Mode;
}

static EFI_STATUS
CallSimple(EFI_PHYSICAL_ADDRESS *Base, UINT32 *Width, UINT32 *Height,
           UINT32 *Depth, UINT32 *Pixels, UINTN *Size)
{
  return GetFrameBufferInfos(Base, Width, Height, Depth, Pixels, Size);
}

static EFI_STATUS
CallSerial(EFI_PHYSICAL_ADDRESS *Base, UINTN *Size, UINTN *Pixels)
{
  return GetFrameBufferMemory(Base, Size, Pixels);
}

static void TestDegasGeometry(void)
{
  EFI_PHYSICAL_ADDRESS Base = 0;
  UINT32 Width = 0;
  UINT32 Height = 0;
  UINT32 Depth = 0;
  UINT32 SimplePixels = 0;
  UINTN SerialPixels = 0;
  UINTN SimpleSize = 0;
  UINTN SerialSize = 0;

  Configure(1220, 2712, 1248, 32, UINT64_C(0xfcc2f000),
            UINT64_C(0x033d0000), 1);
  assert(GetFontScale() == 2);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_SUCCESS);
  assert(Base == UINT64_C(0xfcc2f000));
  assert(Width == 1220 && Height == 2712 && Depth == 32);
  assert(SimplePixels == 1248);
  assert(SimplePixels * (Depth / 8U) == 4992);
  assert(SimpleSize == UINT64_C(0x00ce9400));

  Base = 0;
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_SUCCESS);
  assert(Base == UINT64_C(0xfcc2f000));
  assert(SerialPixels == 1248);
  assert(SerialPixels * (Depth / 8U) == 4992);
  assert(SerialSize == UINT64_C(0x00ce9400));

  gRegionMode = 2;
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_SUCCESS);
}

static void TestDefaultPitch(void)
{
  EFI_PHYSICAL_ADDRESS Base;
  UINT32 Width;
  UINT32 Height;
  UINT32 Depth;
  UINT32 SimplePixels;
  UINTN SerialPixels;
  UINTN SimpleSize;
  UINTN SerialSize;
  const UINTN Expected = (UINTN)1220 * 4U * 2712U;

  Configure(1220, 2712, 0, 32, UINT64_C(0xfcc2f000),
            UINT64_C(0x033d0000), 1);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_SUCCESS);
  assert(SimplePixels == Width && SimplePixels == 1220);
  assert(SimpleSize == Expected);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_SUCCESS);
  assert(SerialPixels == 1220);
  assert(SerialSize == Expected);
}

static void TestRejectedGeometry(void)
{
  EFI_PHYSICAL_ADDRESS Base;
  UINT32 Width;
  UINT32 Height;
  UINT32 Depth;
  UINT32 SimplePixels;
  UINTN SerialPixels;
  UINTN SimpleSize;
  UINTN SerialSize;
  const UINT64 SurfaceSize = UINT64_C(0x00ce9400);

  Configure(1220, 2712, 1219, 32, UINT64_C(0xfcc2f000),
            UINT64_C(0x033d0000), 1);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_UNSUPPORTED);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_UNSUPPORTED);

  Configure(1220, 2712, 1248, 32, UINT64_C(0xfcc2f000), SurfaceSize - 1U, 1);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_BAD_BUFFER_SIZE);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_BAD_BUFFER_SIZE);

  Configure(1220, 2712, 1248, 32, UINT64_MAX - SurfaceSize + 1U,
            UINT64_MAX, 1);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_BAD_BUFFER_SIZE);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_BAD_BUFFER_SIZE);

  Configure(1220, 2712, 1248, 32, UINT64_C(0xfcc2f000),
            UINT64_C(0x033d0000), 0);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_NOT_FOUND);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_NOT_FOUND);

  Configure(1220, 2712, 1248, 8, UINT64_C(0xfcc2f000),
            UINT64_C(0x033d0000), 1);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_UNSUPPORTED);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_UNSUPPORTED);

  Configure(5, 12, 5, 32, UINT64_C(0xfcc2f000), UINT64_C(0x1000), 1);
  assert(GetFontScale() == 1);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_UNSUPPORTED);

  Configure(UINT32_MAX, UINT32_MAX, UINT32_MAX, 32,
            UINT64_C(0xfcc2f000), UINT64_MAX, 1);
  assert(CallSimple(&Base, &Width, &Height, &Depth, &SimplePixels,
                    &SimpleSize) == EFI_BAD_BUFFER_SIZE);
  assert(CallSerial(&Base, &SerialSize, &SerialPixels) == EFI_BAD_BUFFER_SIZE);
}

int main(void)
{
  TestDegasGeometry();
  TestDefaultPitch();
  TestRejectedGeometry();
  puts("Mu framebuffer geometry tests passed");
  return 0;
}
'''
    return preamble + definitions + tests


def windows_path_to_wsl(path: Path) -> str:
    resolved = path.resolve()
    drive = resolved.drive.rstrip(":").lower()
    suffix = resolved.as_posix()[len(resolved.drive) :]
    return f"/mnt/{drive}{suffix}"


def main() -> None:
    BUILD_DIR.mkdir(exist_ok=True)
    HARNESS_PATH.write_text(create_harness(), encoding="utf-8", newline="\n")

    linux_root = windows_path_to_wsl(ROOT)
    command = (
        "cd "
        + shlex.quote(linux_root)
        + " && clang -std=c11 -Wall -Wextra -Werror "
        + "-fsanitize=address,undefined -fno-sanitize-recover=all -I . "
        + "diagnostic/build/mu-geometry-harness.c "
        + "-o diagnostic/build/test_mu_geometry "
        + "&& diagnostic/build/test_mu_geometry"
    )
    subprocess.run(["wsl", "--", "bash", "-c", command], check=True)
    print(f"generated harness: {HARNESS_PATH}")
    print(f"generated binary: {BINARY_PATH}")
    print("scope: extracted function bodies with host stubs; no whole-Mu build")


if __name__ == "__main__":
    main()
