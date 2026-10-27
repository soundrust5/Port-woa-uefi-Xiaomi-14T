// Read-only spatial sample, not a full-frame checksum or a display refresh.
typedef struct {
 UINT32 RgbHash,AlphaHash,Pixels,ZeroAlpha,OpaqueAlpha,Nonblack;
} DEGAS_FRAME_SAMPLE;
STATIC BOOLEAN DegasFrameSample(volatile UINT32 *Base,UINTN Bytes,UINT32 Width,
 UINT32 Height,UINT32 Pitch,DEGAS_FRAME_SAMPLE *Out){
 UINT32 X,Y,C,R,V;UINT64 Surface;
 if(!Base || !Out || ((UINTN)Base&3) || !Width || !Height || Pitch<Width)return FALSE;
 Surface=(UINT64)Pitch*Height;
 if(Surface>MAX_UINTN/sizeof(UINT32) || Surface*sizeof(UINT32)>Bytes)return FALSE;
 Out->RgbHash=2166136261U;Out->AlphaHash=2166136261U;
 Out->Pixels=0;Out->ZeroAlpha=0;Out->OpaqueAlpha=0;Out->Nonblack=0;
 for(R=0;R<48;R++){
  Y=(UINT32)((UINT64)R*(Height-1)/47);
  for(C=0;C<32;C++){
   X=(UINT32)((UINT64)C*(Width-1)/31);
   V=Base[(UINTN)Y*Pitch+X];
   Out->RgbHash=(Out->RgbHash^(V&0xffffffU))*16777619U;
   Out->AlphaHash=(Out->AlphaHash^(V>>24))*16777619U;
   Out->Pixels++;Out->ZeroAlpha+=(V>>24)==0;
   Out->OpaqueAlpha+=(V>>24)==255;Out->Nonblack+=(V&0xffffffU)!=0;
  }
 }
 return TRUE;
}
