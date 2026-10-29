#ifndef DEGAS_CPU_STATUS_H
#define DEGAS_CPU_STATUS_H
// Boot Services-only diagnostic protocol; not an OS or runtime interface.
#define DEGAS_CPU_STATUS_GUID {0x43c9af29,0xe06a,0x481d,{0x92,0x67,0x09,0x6d,0x74,0x46,0xe8,0x11}}
typedef struct {
 UINT32 Version,PublishedCount,TemplateCount,DiscoveryStatus;
 UINT32 PsciCpuOnFeature,StateMismatchMask;
 EFI_STATUS (EFIAPI *PrepareParking)(VOID);
} DEGAS_CPU_STATUS;
#endif
