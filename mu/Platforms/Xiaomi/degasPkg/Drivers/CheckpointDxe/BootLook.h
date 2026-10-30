#include "LogoImage.h"
STATIC EFI_STATUS DrawMiLogo(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixels;EFI_STATUS S;UINTN I,N=0,J,Count;
 CONST UINTN Total=DEGAS_LOGO_WIDTH*DEGAS_LOGO_HEIGHT;
 Pixels=AllocatePool(Total*sizeof(*Pixels));if(!Pixels)return EFI_OUT_OF_RESOURCES;
 for(I=0;I+2<sizeof(mDegasMiLogoRle);I+=3){
  Count=(UINTN)mDegasMiLogoRle[I]|((UINTN)mDegasMiLogoRle[I+1]<<8);
  if(!Count || Count>Total-N){FreePool(Pixels);return EFI_COMPROMISED_DATA;}
  for(J=0;J<Count;J++,N++){
   Pixels[N].Blue=Pixels[N].Green=Pixels[N].Red=mDegasMiLogoRle[I+2];Pixels[N].Reserved=0;
  }
 }
 if(I!=sizeof(mDegasMiLogoRle) || N!=Total){FreePool(Pixels);return EFI_COMPROMISED_DATA;}
 S=Gop->Blt(Gop,Pixels,EfiBltBufferToVideo,0,0,430,1000,DEGAS_LOGO_WIDTH,DEGAS_LOGO_HEIGHT,DEGAS_LOGO_WIDTH*sizeof(*Pixels));
 FreePool(Pixels);return S;
}
STATIC EFI_STATUS DrawBootChoice(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop,UINTN Index,BOOLEAN Selected){
 CONST CHAR8 *Text=Index?"WINDOWS":"ANDROID";CONST UINTN W=600,H=80,Scale=4;
 EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Pixels;UINTN I,R,C,J,K,X=216,At;EFI_STATUS S;
 Pixels=AllocatePool(W*H*sizeof(*Pixels));if(!Pixels)return EFI_OUT_OF_RESOURCES;
 for(I=0;I<W*H;I++){Pixels[I].Blue=Pixels[I].Green=Pixels[I].Red=Selected?255:0;Pixels[I].Reserved=0;}
 for(;*Text;Text++,X+=24){
  CONST UINT8 *Glyph=letters[*Text-'A'];
  for(R=0;R<7;R++)for(C=0;C<5;C++)if(Glyph[R]&(1U<<(4-C)))
   for(J=0;J<Scale;J++)for(K=0;K<Scale;K++){
    At=(26+R*Scale+J)*W+X+C*Scale+K;
    Pixels[At].Blue=Pixels[At].Green=Pixels[At].Red=Selected?0:255;
   }
 }
 S=Gop->Blt(Gop,Pixels,EfiBltBufferToVideo,0,0,310,1550+Index*120,W,H,W*sizeof(*Pixels));
 FreePool(Pixels);return S;
}
