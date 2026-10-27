#ifndef DEGAS_OPAQUE_H_
#define DEGAS_OPAQUE_H_

// MT6897 OVL layer0: pinned mtk_disp_ovl.c:115-147,2400-2415,2529-2539;
// installed mediatek-drm.ko mtk_ovl_layer_config confirms PITCH bit28.
// DRM XRGB8888 is little-endian B,G,R,X and maps to CON CFMT=2, no swaps.
// Reject PREMULTI/MAN (0x803000): its opaque behavior is not established.
#define DEGAS_OVL_ALPHA_MASK  0x000001ffU
#define DEGAS_OVL_BGRX_FORMAT 0x00002000U
#define DEGAS_OVL_CONST_BLEND 0x10000000U
#define DEGAS_OVL_PITCH       4992U

STATIC BOOLEAN mDegasOpaqueVerified;
STATIC BOOLEAN mDegasOpaqueRollbackFailed;

STATIC CONST UINTN mDegasOvlControls[4]={
  0x14402030,0x14602030,0x14402044,0x14602044
};

STATIC VOID DegasOvlReadControls(UINT32 Values[4]){
  for(UINTN I=0;I<4;I++)Values[I]=MmioRead32(mDegasOvlControls[I]);
}

STATIC BOOLEAN DegasOvlOpaqueReady(VOID){
  UINT32 V[4];
  if(!mDegasOpaqueVerified || mDegasOpaqueRollbackFailed)return FALSE;
  DegasOvlReadControls(V);
  return V[0]==(DEGAS_OVL_BGRX_FORMAT|DEGAS_OVL_ALPHA_MASK) &&
         V[1]==(DEGAS_OVL_BGRX_FORMAT|DEGAS_OVL_ALPHA_MASK) &&
         V[2]==(DEGAS_OVL_PITCH|DEGAS_OVL_CONST_BLEND) &&
         V[3]==(DEGAS_OVL_PITCH|DEGAS_OVL_CONST_BLEND);
}

STATIC BOOLEAN DegasOvlLegacyReady(VOID){
  return !mDegasOpaqueVerified && !mDegasOpaqueRollbackFailed &&
         MmioRead32(mDegasOvlControls[2])==DEGAS_OVL_PITCH &&
         MmioRead32(mDegasOvlControls[3])==DEGAS_OVL_PITCH;
}

// Caller first verifies retained addresses/geometry/mutexes and DSI idle, at
// TPL_NOTIFY. No framebuffer access. Partial failures restore attempted writes.
STATIC EFI_STATUS DegasOvlSetupOpaque(VOID){
  UINT32 Original[4],Target[4],Readback[4];BOOLEAN Touched[4]={FALSE,FALSE,FALSE,FALSE};
  UINTN I;
  if(mDegasOpaqueRollbackFailed)return EFI_DEVICE_ERROR;
  if(mDegasOpaqueVerified)return DegasOvlOpaqueReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
  DegasOvlReadControls(Original);
  DEBUG((DEBUG_ERROR,"DEGAS OVL ALPHA before con0=%08x con1=%08x pitch0=%08x pitch1=%08x\n",
    Original[0],Original[1],Original[2],Original[3]));
  for(I=0;I<2;I++){
    if((Original[I]&~DEGAS_OVL_ALPHA_MASK)!=DEGAS_OVL_BGRX_FORMAT ||
       (Original[I+2]&~DEGAS_OVL_CONST_BLEND)!=DEGAS_OVL_PITCH)
      return EFI_UNSUPPORTED;
    // Preserve every field except the two explicitly selected alpha controls.
    Target[I]=Original[I]|DEGAS_OVL_ALPHA_MASK;
    Target[I+2]=Original[I+2]|DEGAS_OVL_CONST_BLEND;
  }
  for(I=0;I<4;I++){
    if(Original[I]==Target[I])continue;
    Touched[I]=TRUE;
    MmioWrite32(mDegasOvlControls[I],Target[I]);
    if(MmioRead32(mDegasOvlControls[I])!=Target[I])goto Rollback;
  }
  DegasOvlReadControls(Readback);
  for(I=0;I<4;I++)if(Readback[I]!=Target[I])goto Rollback;
  mDegasOpaqueVerified=TRUE;
  DEBUG((DEBUG_ERROR,"DEGAS OVL ALPHA opaque register readback verified con=%08x pitch=%08x\n",Target[0],Target[2]));
  return EFI_SUCCESS;

Rollback:
  DEBUG((DEBUG_ERROR,"DEGAS OVL ALPHA readback failed; restoring original controls\n"));
  for(I=4;I>0;I--)if(Touched[I-1])MmioWrite32(mDegasOvlControls[I-1],Original[I-1]);
  DegasOvlReadControls(Readback);
  for(I=0;I<4;I++)if(Readback[I]!=Original[I])mDegasOpaqueRollbackFailed=TRUE;
  DEBUG((DEBUG_ERROR,"DEGAS OVL ALPHA rollback_verified=%u con0=%08x con1=%08x pitch0=%08x pitch1=%08x\n",
    (UINT32)!mDegasOpaqueRollbackFailed,Readback[0],Readback[1],Readback[2],Readback[3]));
  return EFI_DEVICE_ERROR;
}

#endif
