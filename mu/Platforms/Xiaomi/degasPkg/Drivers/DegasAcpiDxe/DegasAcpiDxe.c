// CPU ACPI, secondary GICR/PSCI discovery uses read-only queries.
#include <Uefi.h>
#include <Guid/Acpi.h>
#include <IndustryStandard/Acpi63.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <Protocol/AcpiTable.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/ArmLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/IoLib.h>
#include <Library/CacheMaintenanceLib.h>
#ifndef DEGAS_CPU_TOPOLOGY
#define DEGAS_CPU_TOPOLOGY 0
#endif
#include "AcpiTablesGenerated.h"
#if DEGAS_CPU_TOPOLOGY
#include "CpuPpttGenerated.h"
#endif
#include "CpuTopologyCore.h"
#include "../../Include/Protocol/DegasCpuStatus.h"
#if DEGAS_BOOT_CORE != 0 && DEGAS_BOOT_CORE != 7
#error Unsupported diagnostic boot core
#endif
#ifndef DEGAS_CPU_TEST_LIMIT
#define DEGAS_CPU_TEST_LIMIT 8
#endif
#if DEGAS_CPU_TEST_LIMIT < 2 || DEGAS_CPU_TEST_LIMIT > 8
#error Diagnostic CPU count must be between two and eight
#endif

typedef struct { CONST UINT8 *Data; UINTN Size; UINT32 Signature; } SOURCE;
STATIC SOURCE Tables[]={
 {mDegasDsdt,sizeof(mDegasDsdt),SIGNATURE_32('D','S','D','T')},
 {mDegasFadt,sizeof(mDegasFadt),SIGNATURE_32('F','A','C','P')},
 {mDegasMadt,sizeof(mDegasMadt),SIGNATURE_32('A','P','I','C')},
 {mDegasGtdt,sizeof(mDegasGtdt),SIGNATURE_32('G','T','D','T')},
#if DEGAS_CPU_TOPOLOGY
 {mDegasPptt,sizeof(mDegasPptt),SIGNATURE_32('P','P','T','T')}
#endif
};
STATIC UINT8 mRuntimeMadt[sizeof(mDegasMadt)];
STATIC UINT32 mEnabledCpuCount=1;
STATIC DEGAS_CPU_STATUS mCpuStatus={2,1,DEGAS_ACPI_CPU_COUNT,0,0,0,NULL};
STATIC EFI_ACPI_TABLE_PROTOCOL *mAcpiProtocol;
STATIC UINTN mAcpiKeys[ARRAY_SIZE(Tables)];
STATIC UINT64 CpuReadGicr(VOID *Context,UINT64 Address){(VOID)Context;return MmioRead64((UINTN)Address);}
STATIC VOID PrepareCpuMadt(VOID){
 UINT64 Mpidrs[8],Bases[8];UINT32 Discovery;UINTN I;BOOLEAN Enable=TRUE;
 ARM_SMC_ARGS Smc={0};
 CopyMem(mRuntimeMadt,mDegasMadt,sizeof(mRuntimeMadt));
 if(DEGAS_ACPI_CPU_COUNT==1)return;
 for(I=0;I<DEGAS_ACPI_CPU_COUNT;I++){
  CONST UINT8 *Entry=mRuntimeMadt+44+80*I;
  ASSERT(Entry[0]==11 && Entry[1]==80);
  Mpidrs[I]=ReadUnaligned64((CONST UINT64 *)(Entry+68));
 }
 Discovery=DegasGicrDiscover(CpuReadGicr,NULL,0x0c440000,0x200000,Mpidrs,DEGAS_ACPI_CPU_COUNT,Bases);
 mCpuStatus.DiscoveryStatus=Discovery;
 if(Discovery)Enable=FALSE;
 Smc.Arg0=ARM_SMC_ID_PSCI_FEATURES;Smc.Arg1=ARM_SMC_ID_PSCI_CPU_ON_AARCH64;ArmCallSmc(&Smc);
 mCpuStatus.PsciCpuOnFeature=(UINT32)Smc.Arg0;
 DEBUG((DEBUG_ERROR,"DEGAS SMP discovery=%u PSCI_CPU_ON_FEATURE=%08x\n",Discovery,(UINT32)Smc.Arg0));
 if((UINT32)Smc.Arg0!=0)Enable=FALSE;
 for(I=0;I<DEGAS_ACPI_CPU_COUNT;I++){
  ZeroMem(&Smc,sizeof(Smc));Smc.Arg0=ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64;Smc.Arg1=Mpidrs[I];Smc.Arg2=0;ArmCallSmc(&Smc);
  DEBUG((DEBUG_ERROR,"DEGAS SMP cpu=%u mpidr=%Lx gicr=%Lx affinity_state=%08x\n",(UINT32)I,Mpidrs[I],Discovery?0:Bases[I],(UINT32)Smc.Arg0));
  if((UINT32)Smc.Arg0!=(I==DEGAS_BOOT_CORE?ARM_SMC_ID_PSCI_AFFINITY_INFO_ON:ARM_SMC_ID_PSCI_AFFINITY_INFO_OFF)){Enable=FALSE;mCpuStatus.StateMismatchMask|=1U<<I;}
 }
#if DEGAS_BOOT_CORE == 7
 if(Discovery || mCpuStatus.StateMismatchMask || mCpuStatus.PsciCpuOnFeature){
  mEnabledCpuCount=0;mCpuStatus.PublishedCount=0;return;
 }
 Enable=FALSE; // Experimental single-prime-core Windows boot only.
#endif
 for(I=0;I<DEGAS_ACPI_CPU_COUNT;I++){
  UINT8 *Entry=mRuntimeMadt+44+80*I;
  BOOLEAN Published=(I==DEGAS_BOOT_CORE || (Enable && I<DEGAS_CPU_TEST_LIMIT));
  WriteUnaligned32((UINT32 *)(Entry+12),Published?1:0);
  WriteUnaligned64((UINT64 *)(Entry+60),Published?(Discovery?0x0c440000:Bases[I]):0);
#if DEGAS_BOOT_CORE == 7
  WriteUnaligned32((UINT32 *)(Entry+16),0);
  WriteUnaligned64((UINT64 *)(Entry+24),0);
#endif
 }
 mRuntimeMadt[9]=0;mRuntimeMadt[9]=CalculateCheckSum8(mRuntimeMadt,sizeof(mRuntimeMadt));
 Tables[2].Data=mRuntimeMadt;mEnabledCpuCount=Enable?DEGAS_CPU_TEST_LIMIT:1;
 mCpuStatus.PublishedCount=mEnabledCpuCount;
 DEBUG((DEBUG_ERROR,"DEGAS SMP enabled=%u fallback=%u CPU_ON NOT CALLED BY UEFI\n",mEnabledCpuCount,!Enable));
}

STATIC BOOLEAN TableValid(CONST UINT8 *Data, UINT32 Signature, UINTN ExactSize){
 UINT32 Length;
 if(Data==NULL)return FALSE;
 Length=ReadUnaligned32((CONST UINT32 *)(Data+4));
 return ReadUnaligned32((CONST UINT32 *)Data)==Signature && Length>=36 && Length<=4096 &&
  (ExactSize==0 || Length==ExactSize) && CalculateSum8(Data,Length)==0;
}

// Check the actual OS-visible root and FADT->DSDT link after AcpiTableDxe fixes it.
STATIC EFI_STATUS VerifyPublished(VOID){
 CONST UINT8 *Rsdp=NULL,*Xsdt,*Data,*Dsdt;
 UINTN I,Count;
 UINT32 Length,Mask=0,Sig;
 UINT64 Address;
 for(I=0;I<gST->NumberOfTableEntries;I++){
  if(CompareGuid(&gST->ConfigurationTable[I].VendorGuid,&gEfiAcpiTableGuid)){
   Rsdp=gST->ConfigurationTable[I].VendorTable;break;
  }
 }
 if(Rsdp==NULL || ReadUnaligned64((CONST UINT64 *)Rsdp)!=SIGNATURE_64('R','S','D',' ','P','T','R',' ') ||
    Rsdp[15]<2 || ReadUnaligned32((CONST UINT32 *)(Rsdp+20))!=36 ||
    CalculateSum8(Rsdp,20)!=0 || CalculateSum8(Rsdp,36)!=0)return EFI_COMPROMISED_DATA;
 Address=ReadUnaligned64((CONST UINT64 *)(Rsdp+24));
 if(Address==0)return EFI_COMPROMISED_DATA;
 Xsdt=(CONST UINT8 *)(UINTN)Address;
 if(!TableValid(Xsdt,SIGNATURE_32('X','S','D','T'),0))return EFI_COMPROMISED_DATA;
 Length=ReadUnaligned32((CONST UINT32 *)(Xsdt+4));
 if((Length-36)%8)return EFI_COMPROMISED_DATA;
 Count=(Length-36)/8;
 for(I=0;I<Count;I++){
  Address=ReadUnaligned64((CONST UINT64 *)(Xsdt+36+8*I));
  if(Address==0)return EFI_COMPROMISED_DATA;
  Data=(CONST UINT8 *)(UINTN)Address;
  Sig=ReadUnaligned32((CONST UINT32 *)Data);
  if(!TableValid(Data,Sig,0))return EFI_COMPROMISED_DATA;
  if(Sig==Tables[1].Signature){
   if((Mask&1) || !TableValid(Data,Sig,sizeof(mDegasFadt)))return EFI_COMPROMISED_DATA;
   Mask|=1;
   Address=ReadUnaligned64((CONST UINT64 *)(Data+140));
   if(Address==0)return EFI_COMPROMISED_DATA;
   Dsdt=(CONST UINT8 *)(UINTN)Address;
   if(!TableValid(Dsdt,Tables[0].Signature,sizeof(mDegasDsdt)))return EFI_COMPROMISED_DATA;
   DEBUG((DEBUG_ERROR,"DEGAS ACPI published DSDT=%p bytes=%u checksum=0\n",Dsdt,(UINT32)sizeof(mDegasDsdt)));
  } else if(Sig==Tables[2].Signature){
   if((Mask&2) || !TableValid(Data,Sig,sizeof(mDegasMadt)))return EFI_COMPROMISED_DATA;
   Mask|=2;
  } else if(Sig==Tables[3].Signature){
   if((Mask&4) || !TableValid(Data,Sig,sizeof(mDegasGtdt)))return EFI_COMPROMISED_DATA;
   Mask|=4;
#if DEGAS_CPU_TOPOLOGY
  } else if(Sig==Tables[4].Signature){
   if((Mask&8) || !TableValid(Data,Sig,sizeof(mDegasPptt)) || CompareMem(Data+36,mDegasPptt+36,sizeof(mDegasPptt)-36))return EFI_COMPROMISED_DATA;
   Mask|=8;
#endif
  }
  DEBUG((DEBUG_ERROR,"DEGAS ACPI published signature=%08x address=%p bytes=%u checksum=0\n",
   Sig,Data,ReadUnaligned32((CONST UINT32 *)(Data+4))));
 }
 DEBUG((DEBUG_ERROR,"DEGAS ACPI RSDP=%p XSDT=%p entries=%u mask=%u\n",Rsdp,Xsdt,(UINT32)Count,Mask));
 return Mask==(DEGAS_CPU_TOPOLOGY?15U:7U) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

#if DEGAS_CPU_PARKING_EXPERIMENT
#include "Parking.h"
#endif
#ifndef DEGAS_NATIVE_CONTEXT_BRIDGE
#define DEGAS_NATIVE_CONTEXT_BRIDGE 0
#endif
#if DEGAS_NATIVE_CONTEXT_BRIDGE
#if DEGAS_CPU_PARKING_EXPERIMENT || DEGAS_BOOT_CORE != 0 || (DEGAS_CPU_TEST_LIMIT != 2 && DEGAS_CPU_TEST_LIMIT != 8)
#error Native context experiment requires CPU0 boot, native PSCI and two or eight CPUs
#endif
#include "NativeContext.h"
#endif
EFI_STATUS EFIAPI DegasAcpiEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System){
 EFI_ACPI_TABLE_PROTOCOL *Acpi=NULL;
 EFI_STATUS Status,Undo;
 UINTN I,Installed=0;
 UINT64 Mpidr=ArmReadMpidr(),Typer=MmioRead64(0x0c440008);
 ARM_SMC_ARGS Smc={0};
 (VOID)Image;(VOID)System;
 // MPIDR contains MT/U flags that are not part of the ACPI affinity encoding.
 if((Mpidr&0xff00ffffffULL)!=((UINT64)DEGAS_BOOT_CORE<<8) || (Typer>>32)!=0 || ArmReadCntFrq()!=13000000){
  DEBUG((DEBUG_ERROR,"DEGAS ACPI runtime identity mismatch; no tables installed\n"));
  return EFI_UNSUPPORTED;
 }
 Smc.Arg0=ARM_SMC_ID_PSCI_VERSION;
 ArmCallSmc(&Smc);
 DEBUG((DEBUG_ERROR,"DEGAS ACPI PSCI_VERSION=%08x (query only)\n",(UINT32)Smc.Arg0));
 if((UINT32)Smc.Arg0<0x10000 || (UINT32)Smc.Arg0>=0x80000000)return EFI_UNSUPPORTED;
 PrepareCpuMadt();
 if(!mEnabledCpuCount)return EFI_UNSUPPORTED;
#if DEGAS_NATIVE_CONTEXT_BRIDGE
 Status=PrepareNativeContext();if(EFI_ERROR(Status))return Status;
#endif
 for(I=0;I<ARRAY_SIZE(Tables);I++){
  if(!TableValid(Tables[I].Data,Tables[I].Signature,Tables[I].Size))return EFI_COMPROMISED_DATA;
 }
 Status=gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid,NULL,(VOID **)&Acpi);
 if(EFI_ERROR(Status))return Status;
 mAcpiProtocol=Acpi;
 for(I=0;I<ARRAY_SIZE(Tables);I++){
  Status=Acpi->InstallAcpiTable(Acpi,(VOID *)(UINTN)Tables[I].Data,Tables[I].Size,&mAcpiKeys[I]);
  if(EFI_ERROR(Status))goto Rollback;
  Installed++;
 }
 Status=VerifyPublished();
 if(!EFI_ERROR(Status)){
  EFI_GUID CpuStatusGuid=DEGAS_CPU_STATUS_GUID;
#if DEGAS_CPU_PARKING_EXPERIMENT
  mCpuStatus.PrepareParking=PrepareParking;
#endif
  EFI_STATUS Diagnostic=gBS->InstallProtocolInterface(&Image,&CpuStatusGuid,EFI_NATIVE_INTERFACE,&mCpuStatus);
  DEBUG((DEBUG_ERROR,"DEGAS CPU STATUS protocol=%r\n",Diagnostic));
#if DEGAS_NATIVE_CONTEXT_BRIDGE
  {EFI_GUID NativeGuid=DEGAS_NATIVE_CONTEXT_GUID;
   Diagnostic=gBS->InstallProtocolInterface(&Image,&NativeGuid,EFI_NATIVE_INTERFACE,&mNativeContext);
   DEBUG((DEBUG_ERROR,"DEGAS NATIVE CONTEXT protocol=%r\n",Diagnostic));
   if(EFI_ERROR(Diagnostic))return Diagnostic;
  }
#endif
  DEBUG((DEBUG_ERROR,"DEGAS ACPI READY cpu=%u boot_uid=%u mpidr=%Lx timer=13000000\n",mEnabledCpuCount,(UINT32)DEGAS_BOOT_CORE,Mpidr&0xff00ffffffULL));
  return Status;
 }
Rollback:
 DEBUG((DEBUG_ERROR,"DEGAS ACPI failed=%r; removing diagnostic set\n",Status));
 while(Installed){
  Undo=Acpi->UninstallAcpiTable(Acpi,mAcpiKeys[--Installed]);
  if(EFI_ERROR(Undo))DEBUG((DEBUG_ERROR,"DEGAS ACPI rollback failed=%r\n",Undo));
 }
 return Status;
}
