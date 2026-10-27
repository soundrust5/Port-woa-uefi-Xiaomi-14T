// DMA source is uncached and must be read only after completion/fence.
// Use aligned scalar 64-bit loads, never SIMD or unaligned device accesses.
STATIC VOID UfsCopyRead(VOID *Buffer,volatile UINT8 *Source,UINTN Bytes){
 UINT8 *Out=(UINT8 *)Buffer;UINT64 Word;
 if(((UINTN)Source&7)==0){
  while(Bytes>=8){
   Word=*(volatile UINT64 *)(UINTN)Source;
   if(((UINTN)Out&7)==0)*(UINT64 *)(UINTN)Out=Word;
   else CopyMem(Out,&Word,8);
   Source+=8;Out+=8;Bytes-=8;
  }
 }
 while(Bytes--)*Out++=*Source++;
}
