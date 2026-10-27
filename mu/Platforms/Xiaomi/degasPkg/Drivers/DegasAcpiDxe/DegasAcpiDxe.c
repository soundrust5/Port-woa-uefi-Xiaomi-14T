// Diagnostic single-CPU ACPI. No peripheral or power-state programming.
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
#include "AcpiTablesGenerated.h"

typedef struct { CONST UINT8 *Data; UINTN Size; UINT32 Signature; } SOURCE;
STATIC CONST SOURCE Tables[]={
 {mDegasDsdt,sizeof(mDegasDsdt),SIGNATURE_32('D','S','D','T')},
 {mDegasFadt,sizeof(mDegasFadt),SIGNATURE_32('F','A','C','P')},
 {mDegasMadt,sizeof(mDegasMadt),SIGNATURE_32('A','P','I','C')},
 {mDegasGtdt,sizeof(mDegasGtdt),SIGNATURE_32('G','T','D','T')}
};

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
  }
  DEBUG((DEBUG_ERROR,"DEGAS ACPI published signature=%08x address=%p bytes=%u checksum=0\n",
   Sig,Data,ReadUnaligned32((CONST UINT32 *)(Data+4))));
 }
 DEBUG((DEBUG_ERROR,"DEGAS ACPI RSDP=%p XSDT=%p entries=%u mask=%u\n",Rsdp,Xsdt,(UINT32)Count,Mask));
 return Mask==7 ? EFI_SUCCESS : EFI_NOT_FOUND;
}

EFI_STATUS EFIAPI DegasAcpiEntry(EFI_HANDLE Image,EFI_SYSTEM_TABLE *System){
 EFI_ACPI_TABLE_PROTOCOL *Acpi=NULL;
 EFI_STATUS Status,Undo;
 UINTN I,Installed=0,Keys[ARRAY_SIZE(Tables)];
 UINT64 Mpidr=ArmReadMpidr(),Typer=MmioRead64(0x0c440008);
 ARM_SMC_ARGS Smc={0};
 (VOID)Image;(VOID)System;
 // MPIDR contains MT/U flags that are not part of the ACPI affinity encoding.
 if((Mpidr&0xff00ffffffULL)!=0 || (Typer>>32)!=0 || ArmReadCntFrq()!=13000000){
  DEBUG((DEBUG_ERROR,"DEGAS ACPI runtime identity mismatch; no tables installed\n"));
  return EFI_UNSUPPORTED;
 }
 Smc.Arg0=ARM_SMC_ID_PSCI_VERSION;
 ArmCallSmc(&Smc);
 DEBUG((DEBUG_ERROR,"DEGAS ACPI PSCI_VERSION=%08x (query only)\n",(UINT32)Smc.Arg0));
 if((UINT32)Smc.Arg0<0x10000 || (UINT32)Smc.Arg0>=0x80000000)return EFI_UNSUPPORTED;
 for(I=0;I<ARRAY_SIZE(Tables);I++){
  if(!TableValid(Tables[I].Data,Tables[I].Signature,Tables[I].Size))return EFI_COMPROMISED_DATA;
 }
 Status=gBS->LocateProtocol(&gEfiAcpiTableProtocolGuid,NULL,(VOID **)&Acpi);
 if(EFI_ERROR(Status))return Status;
 for(I=0;I<ARRAY_SIZE(Tables);I++){
  Status=Acpi->InstallAcpiTable(Acpi,(VOID *)(UINTN)Tables[I].Data,Tables[I].Size,&Keys[I]);
  if(EFI_ERROR(Status))goto Rollback;
  Installed++;
 }
 Status=VerifyPublished();
 if(!EFI_ERROR(Status)){
  DEBUG((DEBUG_ERROR,"DEGAS ACPI READY cpu=1 uid=0 mpidr=0 gicr=0c440000 timer=13000000\n"));
  return Status;
 }
Rollback:
 DEBUG((DEBUG_ERROR,"DEGAS ACPI failed=%r; removing diagnostic set\n",Status));
 while(Installed){
  Undo=Acpi->UninstallAcpiTable(Acpi,Keys[--Installed]);
  if(EFI_ERROR(Undo))DEBUG((DEBUG_ERROR,"DEGAS ACPI rollback failed=%r\n",Undo));
 }
 return Status;
}
