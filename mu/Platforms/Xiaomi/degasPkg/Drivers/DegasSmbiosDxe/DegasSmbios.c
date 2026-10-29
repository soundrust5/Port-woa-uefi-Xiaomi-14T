// Physical inventory only. OS allocations and active CPUs come from UEFI/ACPI.
#include <Uefi.h>
#include <Protocol/Smbios.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#ifndef DEGAS_CPU_TOPOLOGY
#define DEGAS_CPU_TOPOLOGY 0
#endif
STATIC EFI_STATUS Add(EFI_SMBIOS_PROTOCOL *S,EFI_HANDLE Parent,VOID *Data,UINTN Length,
 UINT8 Type,EFI_SMBIOS_HANDLE Handle,CONST CHAR8 *CONST *Strings,UINTN Count){
 UINT8 Buffer[512];UINTN Used=Length,I,Bytes;EFI_SMBIOS_TABLE_HEADER *H=(VOID *)Buffer;
 if(Length<sizeof(*H) || Length>MAX_UINT8)return EFI_INVALID_PARAMETER;
 ZeroMem(Buffer,sizeof(Buffer));CopyMem(Buffer,Data,Length);H->Type=Type;H->Length=(UINT8)Length;H->Handle=Handle;
 for(I=0;I<Count;I++){
  Bytes=AsciiStrSize(Strings[I]);if(Bytes<2 || Bytes>sizeof(Buffer)-Used-1)return EFI_BAD_BUFFER_SIZE;
  CopyMem(Buffer+Used,Strings[I],Bytes);Used+=Bytes;
 }
 return S->Add(S,Parent,&Handle,H);
}
EFI_STATUS EFIAPI DegasSmbiosEntry(EFI_HANDLE Parent,EFI_SYSTEM_TABLE *SystemTable){
 EFI_SMBIOS_PROTOCOL *S;EFI_STATUS Status;EFI_SMBIOS_HANDLE Added[9];UINTN Count=0;
 SMBIOS_TABLE_TYPE0 Bios={0};SMBIOS_TABLE_TYPE1 System={0};SMBIOS_TABLE_TYPE3 Chassis={0};
 SMBIOS_TABLE_TYPE4 Cpu={0};SMBIOS_TABLE_TYPE16 Array={0};SMBIOS_TABLE_TYPE17 Memory={0};
 SMBIOS_TABLE_TYPE19 Map={0};SMBIOS_TABLE_TYPE32 Boot={0};
#if DEGAS_CPU_TOPOLOGY
 SMBIOS_TABLE_TYPE7 L3={0};
 CONST CHAR8 *CacheStrings[]={"SoC L3"};
#endif
 CONST CHAR8 *BiosStrings[]={"Project Mu","Xiaomi 14T experimental","10/27/2026"};
 CONST CHAR8 *SystemStrings[]={"Xiaomi","Xiaomi 14T","degas"};
 CONST CHAR8 *Vendor[]={"Xiaomi"};
 CONST CHAR8 *CpuStrings[]={"SoC","MediaTek","MediaTek Dimensity 8300-Ultra"};
 CONST CHAR8 *MemoryStrings[]={"Onboard memory","System memory"};
 (VOID)SystemTable;Status=gBS->LocateProtocol(&gEfiSmbiosProtocolGuid,NULL,(VOID **)&S);if(EFI_ERROR(Status))return Status;
#define RECORD(Data,Length,Type,Handle,Strings,N) do{Status=Add(S,Parent,&Data,Length,Type,Handle,Strings,N);if(EFI_ERROR(Status))goto Failed;Added[Count++]=Handle;}while(0)
 Bios.Vendor=1;Bios.BiosVersion=2;Bios.BiosReleaseDate=3;Bios.BiosSize=31;
 Bios.BIOSCharacteristicsExtensionBytes[1]=BIT3;
 Bios.SystemBiosMajorRelease=Bios.SystemBiosMinorRelease=0xff;
 Bios.EmbeddedControllerFirmwareMajorRelease=Bios.EmbeddedControllerFirmwareMinorRelease=0xff;
 RECORD(Bios,sizeof(Bios),0,0x2000,BiosStrings,ARRAY_SIZE(BiosStrings));
 System.Manufacturer=1;System.ProductName=2;System.Version=3;System.WakeUpType=SystemWakeupTypeUnknown;
 RECORD(System,sizeof(System),1,0x2001,SystemStrings,ARRAY_SIZE(SystemStrings));
 Chassis.Manufacturer=1;Chassis.Type=0x0b;Chassis.BootupState=Chassis.PowerSupplyState=Chassis.ThermalState=2;Chassis.SecurityStatus=2;
 RECORD(Chassis,0x16,3,0x2003,Vendor,ARRAY_SIZE(Vendor));
#if DEGAS_CPU_TOPOLOGY
 // MediaTek's published Dimensity8300 specification: one4MiB L3 cache.
 // Unknown geometry/policy fields remain unknown; no fabricated L1/L2 data.
 L3.SocketDesignation=1;L3.CacheConfiguration=0x0382;
 L3.MaximumCacheSize.Size=L3.InstalledSize.Size=4096;
 L3.MaximumCacheSize2.Size=L3.InstalledSize2.Size=4096;
 L3.SupportedSRAMType.Unknown=L3.CurrentSRAMType.Unknown=1;
 L3.ErrorCorrectionType=CacheErrorUnknown;L3.SystemCacheType=CacheTypeUnified;
 L3.Associativity=CacheAssociativityUnknown;
 RECORD(L3,sizeof(L3),7,0x2007,CacheStrings,ARRAY_SIZE(CacheStrings));
#endif
 Cpu.Socket=1;Cpu.ProcessorType=3;Cpu.ProcessorFamily=0xfe;Cpu.ProcessorFamily2=ProcessorFamilyARMv9;
 Cpu.ProcessorManufacturer=2;Cpu.ProcessorVersion=3;Cpu.Status=0x41;Cpu.ProcessorUpgrade=2;
 Cpu.L1CacheHandle=Cpu.L2CacheHandle=Cpu.L3CacheHandle=0xffff;
#if DEGAS_CPU_TOPOLOGY
 Cpu.L3CacheHandle=0x2007;
#endif
 Cpu.CoreCount=Cpu.ThreadCount=8;Cpu.CoreCount2=Cpu.ThreadCount2=8;
 // Enabled counts and clock speeds0 mean unknown, not a claim that all APs
 // started or that this platform has working CPU frequency management.
 Cpu.ProcessorCharacteristics=BIT2|BIT3;
 RECORD(Cpu,OFFSET_OF(SMBIOS_TABLE_TYPE4,SocketType),4,0x2004,CpuStrings,ARRAY_SIZE(CpuStrings));
 Array.Location=MemoryArrayLocationSystemBoard;Array.Use=MemoryArrayUseSystemMemory;
 Array.MemoryErrorCorrection=2;Array.MaximumCapacity=12U*1024U*1024U;
 Array.MemoryErrorInformationHandle=0xfffe;Array.NumberOfMemoryDevices=1;
 RECORD(Array,sizeof(Array),16,0x2010,NULL,0);
 Memory.MemoryArrayHandle=0x2010;Memory.MemoryErrorInformationHandle=0xfffe;
 Memory.TotalWidth=Memory.DataWidth=0xffff;Memory.Size=12U*1024U;
 Memory.FormFactor=MemoryFormFactorRowOfChips;Memory.DeviceLocator=1;Memory.BankLocator=2;
 Memory.MemoryType=MemoryTypeUnknown;Memory.MemoryTechnology=3;
 Memory.MemoryOperatingModeCapability.Uint16=BIT3;Memory.VolatileSize=12ULL*1024*1024*1024;
 RECORD(Memory,OFFSET_OF(SMBIOS_TABLE_TYPE17,Pmic0ManufacturerID),17,0x2011,MemoryStrings,ARRAY_SIZE(MemoryStrings));
 Map.StartingAddress=Map.EndingAddress=MAX_UINT32;Map.MemoryArrayHandle=0x2010;Map.PartitionWidth=1;
 Map.ExtendedStartingAddress=0x40000000;Map.ExtendedEndingAddress=0x33fffffffULL;
 RECORD(Map,sizeof(Map),19,0x2013,NULL,0);
 RECORD(Boot,sizeof(Boot),32,0x2020,NULL,0);
 DEBUG((DEBUG_ERROR,"DEGAS SMBIOS records=%u CPU=MediaTek Dimensity 8300-Ultra physical_cores=8 physical_RAM=12GiB\n",(UINT32)Count));return EFI_SUCCESS;
Failed:while(Count)S->Remove(S,Added[--Count]);return Status;
#undef RECORD
}
