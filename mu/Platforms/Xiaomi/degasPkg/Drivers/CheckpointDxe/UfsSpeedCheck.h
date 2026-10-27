// Re-read the known first 64 KiB sixteen times. This checks the HS transport,
// not cold-media throughput: the drive can cache these sectors.
STATIC BOOLEAN UfsSpeedCheck(VOID){
 UINT8 Q[32];UINTN Round,Lba,Chunk,J;UINT32 C;
 UINT64 Start,Elapsed=0,CopyElapsed=0,Bytes=0;BOOLEAN Good=FALSE;
 UINT8 *Copy=AllocatePool(DEGAS_UFS_MAX_BLOCKS*4096);
 volatile UINT8 *R=(volatile UINT8 *)(UINTN)0x44471080;
 volatile UINT8 *D=(volatile UINT8 *)(UINTN)0x44472000;
 if(!Copy)return FALSE;
 for(Round=0;Round<16;Round++)for(Lba=0;Lba<16;Lba+=Chunk){
  Chunk=MIN(16-Lba,DEGAS_UFS_MAX_BLOCKS);
  DegasUfsReadCommand(Q,(UINT32)Lba,(UINT32)Chunk,(UINT8)(0xc0+2*Round+(Lba!=0)));
  Start=GetPerformanceCounter();
  if(!UfsProbePacket(Q,(UINT32)(Chunk*4096)))goto Done;
  Elapsed+=GetTimeInNanoSecond(GetPerformanceCounter()-Start);
  if(R[0]!=0x21 || R[2]!=2 || R[3]!=Q[3] || R[6] || R[7] || R[8] ||
     R[10] || R[11] || (R[1]&0x60) || R[12] || R[13] || R[14] || R[15])goto Done;
  Start=GetPerformanceCounter();
  UfsCopyRead(Copy,D,Chunk*4096);
  CopyElapsed+=GetTimeInNanoSecond(GetPerformanceCounter()-Start);
  for(J=0;J<Chunk;J++){
   C=UfsCrc32(Copy+J*4096,4096);
   if(C!=UfsReadExpected[Lba+J]){
    DEBUG((DEBUG_ERROR,"DEGAS UFS HS CRC FAIL round=%u lba=%u got=%08x expected=%08x\n",(UINT32)Round,(UINT32)(Lba+J),C,UfsReadExpected[Lba+J]));
    goto Done;
   }
  }
  Bytes+=Chunk*4096;BootWatchdogFeed();
 }
 if(!Elapsed)goto Done;
 DEBUG((DEBUG_ERROR,"DEGAS UFS HS CRC PASS rounds=16 bytes=%Lu transport_us=%Lu transport_KiB_s=%Lu NO WRITES; REPEATED SECTORS\n",Bytes,Elapsed/1000,(Bytes/1024)*1000000000ULL/Elapsed));
 DEBUG((DEBUG_ERROR,"DEGAS UFS HS READ PLUS COPY bytes=%Lu read_us=%Lu copy_us=%Lu combined_KiB_s=%Lu CRC EXCLUDED\n",Bytes,Elapsed/1000,CopyElapsed/1000,(Bytes/1024)*1000000000ULL/(Elapsed+CopyElapsed)));
 Good=TRUE;
Done:
 FreePool(Copy);return Good;
}
