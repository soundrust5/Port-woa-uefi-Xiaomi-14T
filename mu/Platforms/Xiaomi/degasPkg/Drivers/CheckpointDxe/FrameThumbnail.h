// Small RGB-only snapshot of the already mapped framebuffer into retained RAM.
// Diagnostic only: no allocation, framebuffer writes, GOP or filesystem access.
// 122 x 272 RGB565 as bounded ASCII rows: under 140 KiB in the 256 KiB ring.
#ifndef DEGAS_FRAME_THUMBNAIL_H_
#define DEGAS_FRAME_THUMBNAIL_H_
STATIC VOID FrameThumbnail(VOID){
 STATIC CONST CHAR8 Hex[]="0123456789abcdef";
 CHAR8 Line[520];UINT32 Row,Column,X,Y,V,Crc=2166136261U;UINT16 Color;
 UINTN Count,At;
 volatile UINT32 *Frame=(volatile UINT32 *)(UINTN)0xfcc2f000;
 SerialPortWrite((UINT8 *)"DEGAS FRAME RGB565 width=122 height=272\n",sizeof("DEGAS FRAME RGB565 width=122 height=272\n")-1);
 for(Row=0;Row<272;Row++){
  At=AsciiSPrint(Line,sizeof(Line),"DEGAS FRAME row=%03u ",Row);
  Y=(UINT32)((UINT64)Row*2711/271);
  for(Column=0;Column<122;Column++){
   X=(UINT32)((UINT64)Column*1219/121);
   V=Frame[(UINTN)Y*1248+X];
   Color=(UINT16)(((V>>8)&0xf800)|((V>>5)&0x07e0)|((V>>3)&0x001f));
   Crc=(Crc^(Color>>8))*16777619U;Crc=(Crc^(Color&255))*16777619U;
   Line[At++]=Hex[(Color>>12)&15];Line[At++]=Hex[(Color>>8)&15];
   Line[At++]=Hex[(Color>>4)&15];Line[At++]=Hex[Color&15];
  }
  Line[At++]='\n';SerialPortWrite((UINT8 *)Line,At);
 }
 Count=AsciiSPrint(Line,sizeof(Line),"DEGAS FRAME END fnv=%08x\n",Crc);
 SerialPortWrite((UINT8 *)Line,Count);
}
#endif
