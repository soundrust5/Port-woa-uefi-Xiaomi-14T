#ifndef DEGAS_DISPLAY_REFRESH_PROTOCOL_H_
#define DEGAS_DISPLAY_REFRESH_PROTOCOL_H_

#include <Uefi.h>

#define DEGAS_DISPLAY_REFRESH_PROTOCOL_GUID \
  {0xdcb2a9a4,0x31a2,0x432e,{0x95,0xba,0x89,0x37,0x74,0xbc,0xfe,0x86}}

typedef struct _DEGAS_DISPLAY_REFRESH_PROTOCOL DEGAS_DISPLAY_REFRESH_PROTOCOL;

// Board diagnostic snapshot only. Cache before EBS; no GOP/Boot Services calls,
// allocations or pixel writes. Requires previously verified opaque OVL setup.
// This issues one bounded transfer, not autonomous framebuffer refresh.
struct _DEGAS_DISPLAY_REFRESH_PROTOCOL {
  EFI_STATUS (EFIAPI *Flush)(IN DEGAS_DISPLAY_REFRESH_PROTOCOL *This);
  // Irreversible until reset: reject subsequent CPU refresh/GOP access before
  // handing the retained display pipeline to an autonomous GCE packet.
  EFI_STATUS (EFIAPI *LockCpuRefresh)(IN DEGAS_DISPLAY_REFRESH_PROTOCOL *This);
};

#endif
