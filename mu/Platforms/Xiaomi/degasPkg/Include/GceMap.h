#ifndef DEGAS_GCE_MAP_H
#define DEGAS_GCE_MAP_H
// Owned low-RAM carveout, immediately below the independent UFS DMA arena.
// Must be removed from conventional memory before any use by the IOMMU.
#include "GceArena.h"

// ARMv7s IAS34/PA35, ARM_NS + NO_PERMS + MTK_EXT, as in the MT6897 driver.
// Deliberately map ONE 4 KiB page, with all other entries invalid. This builds
// software tables only; it neither installs a root nor grants DMA permission.
// Types UINT32/BOOLEAN/TRUE/FALSE are supplied by the UEFI caller or host test.
STATIC BOOLEAN DegasGceBuildMap(UINT32 *Root,UINT32 *Leaf){
 UINT32 I;
 if(Root==NULL || Leaf==NULL)return FALSE;
 for(I=0;I<DEGAS_GCE_ROOT_WORDS;I++)Root[I]=0;
 for(I=0;I<DEGAS_GCE_LEAF_WORDS;I++)Leaf[I]=0;
 // Normal noncacheable, shareable, not-global small page: (S|NG|TEX1)<<4 | 2.
 // NO_PERMS is essential: ARM permission bits are repurposed for extended PA.
 Leaf[DEGAS_GCE_PAGE_INDEX]=(UINT32)DEGAS_GCE_PACKET_PA|0xc42U;
 // L1 coarse table + NS. Leaf PA is below 4 GiB, so no extended PA bits.
 Root[DEGAS_GCE_IOVA>>20]=(UINT32)DEGAS_GCE_LEAF_PA|9U;
 return TRUE;
}

STATIC UINT64 DegasGceQueryPa(UINT32 Entry){
 // HYP_PMM_IOVA_TO_PHYS return format, not a page-table descriptor.
 return ((UINT64)(Entry&7U)<<32)|(Entry&0xfffff000U);
}
#endif
