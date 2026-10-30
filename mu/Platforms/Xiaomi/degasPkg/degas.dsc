[Defines]
 PLATFORM_NAME=degas
 PLATFORM_GUID=5C9664ED-1952-48A3-9DB2-8B6F0281F96D
 PLATFORM_VERSION=0.1
 DSC_SPECIFICATION=0x00010005
 OUTPUT_DIRECTORY=Build/degasPkg
 SUPPORTED_ARCHITECTURES=AARCH64
 BUILD_TARGETS=DEBUG|RELEASE
 SKUID_IDENTIFIER=DEFAULT
 FLASH_DEFINITION=degasPkg/degas.fdf
 USE_CUSTOM_DISPLAY_DRIVER=1
 USE_PHYSICAL_TIMER=1
!ifndef DEGAS_CPU_PARKING_EXPERIMENT
 DEFINE DEGAS_CPU_PARKING_EXPERIMENT=0
!endif
!ifndef DEGAS_DIAGNOSTIC_EBS_WATCHDOG
 DEFINE DEGAS_DIAGNOSTIC_EBS_WATCHDOG=0
!endif
!ifndef DEGAS_BOOT_CORE
 DEFINE DEGAS_BOOT_CORE=0
!endif
!ifndef DEGAS_CPU_TEST_LIMIT
 DEFINE DEGAS_CPU_TEST_LIMIT=8
!endif
!ifndef DEGAS_WINDOWS_IDENTITY_OBSERVER
 DEFINE DEGAS_WINDOWS_IDENTITY_OBSERVER=0
!endif
!ifndef DEGAS_WINDOWS_BUGCHECK_MONITOR
 DEFINE DEGAS_WINDOWS_BUGCHECK_MONITOR=0
!endif
!ifndef DEGAS_NATIVE_CONTEXT_BRIDGE
 DEFINE DEGAS_NATIVE_CONTEXT_BRIDGE=0
!endif
!ifndef DEGAS_CPU_TOPOLOGY
 DEFINE DEGAS_CPU_TOPOLOGY=0
!endif
!include SiliciumPkg/SiliciumPkg.dsc.inc
!ifndef DEGAS_WIFI_INIT_TEST
 DEFINE DEGAS_WIFI_INIT_TEST=0
!endif
!ifndef DEGAS_ANDROID_EFI_BOOT
 DEFINE DEGAS_ANDROID_EFI_BOOT=0
!endif
[BuildOptions]
 *_*_AARCH64_PP_FLAGS = -DDEGAS_WINDOWS_IDENTITY_OBSERVER=$(DEGAS_WINDOWS_IDENTITY_OBSERVER) -DDEGAS_WINDOWS_BUGCHECK_MONITOR=$(DEGAS_WINDOWS_BUGCHECK_MONITOR)
 *_*_AARCH64_CC_FLAGS = -march=armv8-a -mstrict-align -DDEGAS_BOOT_OBSERVATION=1 -DDEGAS_WIFI_INIT_TEST=$(DEGAS_WIFI_INIT_TEST) -DDEGAS_ANDROID_EFI_BOOT=$(DEGAS_ANDROID_EFI_BOOT) -DDEGAS_CPU_PARKING_EXPERIMENT=$(DEGAS_CPU_PARKING_EXPERIMENT) -DDEGAS_DIAGNOSTIC_EBS_WATCHDOG=$(DEGAS_DIAGNOSTIC_EBS_WATCHDOG) -DDEGAS_BOOT_CORE=$(DEGAS_BOOT_CORE) -DDEGAS_CPU_TEST_LIMIT=$(DEGAS_CPU_TEST_LIMIT) -DDEGAS_WINDOWS_IDENTITY_OBSERVER=$(DEGAS_WINDOWS_IDENTITY_OBSERVER) -DDEGAS_WINDOWS_BUGCHECK_MONITOR=$(DEGAS_WINDOWS_BUGCHECK_MONITOR) -DDEGAS_NATIVE_CONTEXT_BRIDGE=$(DEGAS_NATIVE_CONTEXT_BRIDGE) -DDEGAS_CPU_TOPOLOGY=$(DEGAS_CPU_TOPOLOGY)
[PcdsFixedAtBuild]
 gArmTokenSpaceGuid.PcdSystemMemoryBase|0x40000000
 gArmPlatformTokenSpaceGuid.PcdCPUCoresStackBase|0x40400000
 gArmPlatformTokenSpaceGuid.PcdCPUCorePrimaryStackSize|0x40000
 # Firmware itself runs only the boot CPU; Windows CPU count comes from MADT.
 # Do not start APs in SEC or change its stack layout for OS SMP enumeration.
 gArmPlatformTokenSpaceGuid.PcdCoreCount|1
 gArmPlatformTokenSpaceGuid.PcdClusterCount|1
 gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosVersion|0x0306
 gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosDocRev|0
 gEfiMdeModulePkgTokenSpaceGuid.PcdSmbiosEntryPointProvideMethod|0x3
 gArmTokenSpaceGuid.PcdGicDistributorBase|0x0c400000
 gArmTokenSpaceGuid.PcdGicRedistributorsBase|0x0c440000
 gArmTokenSpaceGuid.PcdArmArchTimerSecIntrNum|29
 gArmTokenSpaceGuid.PcdArmArchTimerIntrNum|30
 gArmTokenSpaceGuid.PcdArmArchTimerVirtIntrNum|27
 gArmTokenSpaceGuid.PcdArmArchTimerHypIntrNum|26
 gSiliciumPkgTokenSpaceGuid.PcdFrameBufferWidth|1220
 gSiliciumPkgTokenSpaceGuid.PcdFrameBufferHeight|2712
 gSiliciumPkgTokenSpaceGuid.PcdFrameBufferColorDepth|32
 gSiliciumPkgTokenSpaceGuid.PcdFrameBufferPixelsPerScanLine|1248
[LibraryClasses]
 FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  ArmSmcLib|MdePkg/Library/ArmSmcLib/ArmSmcLib.inf
 MemoryMapLib|degasPkg/Library/MemoryMapLib/MemoryMapLib.inf
 SerialPortLib|degasPkg/Library/RamSerialPortLib/RamSerialPortLib.inf
[Components]
 degasPkg/Drivers/DegasAcpiDxe/DegasAcpiDxe.inf
 degasPkg/Drivers/DegasSmbiosDxe/DegasSmbiosDxe.inf
 degasPkg/Applications/UfsFileProbe/UfsFileProbe.inf
 degasPkg/Drivers/DegasGopDxe/DegasGopDxe.inf
 degasPkg/Drivers/CheckpointDxe/CheckpointDxe.inf
