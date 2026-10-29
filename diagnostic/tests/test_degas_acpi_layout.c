// Decode generated tables using independent EDK2 ACPI structures, not Python offsets.
#include <Uefi.h>
#include <IndustryStandard/Acpi63.h>
#include <assert.h>
#include <stdio.h>
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/AcpiTablesGenerated.h"

int main(void){
 const EFI_ACPI_6_3_FIXED_ACPI_DESCRIPTION_TABLE *f=(const void *)mDegasFadt;
 const EFI_ACPI_6_3_GIC_STRUCTURE *c=(const void *)(mDegasMadt+sizeof(EFI_ACPI_6_3_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER));
 const EFI_ACPI_6_3_GIC_DISTRIBUTOR_STRUCTURE *d=(const void *)((const UINT8 *)c+sizeof(*c));
 const EFI_ACPI_6_3_GENERIC_TIMER_DESCRIPTION_TABLE *t=(const void *)mDegasGtdt;
 assert(sizeof(*f)==sizeof(mDegasFadt) && f->Flags==BIT20 && f->ArmBootArch==1);
 assert(f->Dsdt==0 && f->XDsdt==0 && f->XFirmwareCtrl==0);
 assert(f->SleepControlReg.Address==0 && f->SleepStatusReg.Address==0);
 assert(c->Type==11 && c->Length==sizeof(*c) && c->Flags==1);
 assert(c->AcpiProcessorUid==0 && c->MPIDR==0 && c->GICRBaseAddress==0xc440000);
 assert(c->VGICMaintenanceInterrupt==25 && c->PerformanceInterruptGsiv==0);
 assert(d->Type==12 && d->Length==sizeof(*d) && d->PhysicalBaseAddress==0xc400000 && d->GicVersion==3);
 assert((const UINT8 *)d+sizeof(*d)==mDegasMadt+sizeof(mDegasMadt));
 assert(sizeof(*t)==sizeof(mDegasGtdt));
 assert(t->SecurePL1TimerGSIV==29 && t->NonSecurePL1TimerGSIV==30);
 assert(t->VirtualTimerGSIV==27 && t->NonSecurePL2TimerGSIV==26);
 assert(t->SecurePL1TimerFlags==0 && t->NonSecurePL1TimerFlags==0 && t->VirtualTimerFlags==0 && t->NonSecurePL2TimerFlags==0);
 puts("degas ACPI matches EDK2 6.3 structures: boot CPU, GIC, FADT and timer PASS");
 return 0;
}
