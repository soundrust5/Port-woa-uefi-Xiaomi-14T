// One PRDT entry, confined to reserved DMA [0x44472000,0x44480000).
#define DEGAS_UFS_MAX_BLOCKS 14u
#define DEGAS_UFS_LAST_LBA 0x03d95fffu
static int DegasUfsReadValid(const unsigned char *q,unsigned bytes){
 unsigned lba=((unsigned)q[18]<<24)|((unsigned)q[19]<<16)|((unsigned)q[20]<<8)|q[21];
 unsigned expected=((unsigned)q[12]<<24)|((unsigned)q[13]<<16)|((unsigned)q[14]<<8)|q[15];
 unsigned blocks=((unsigned)q[23]<<8)|q[24];
 return bytes && bytes%4096==0 && bytes<=DEGAS_UFS_MAX_BLOCKS*4096 &&
  q[0]==1 && q[1]==0x40 && q[2]==2 && q[16]==0x28 && q[17]==0 &&
  q[22]==0 && q[25]==0 && expected==bytes && blocks==bytes/4096 &&
  lba<=DEGAS_UFS_LAST_LBA && blocks-1<=DEGAS_UFS_LAST_LBA-lba;
}
static void DegasUfsReadCommand(unsigned char *q,unsigned lba,unsigned blocks,unsigned char tag){
 unsigned i,bytes=blocks*4096;
 for(i=0;i<32;i++)q[i]=0;
 q[0]=1;q[1]=0x40;q[2]=2;q[3]=tag;
 q[12]=(unsigned char)(bytes>>24);q[13]=(unsigned char)(bytes>>16);
 q[14]=(unsigned char)(bytes>>8);q[15]=(unsigned char)bytes;
 q[16]=0x28;q[18]=(unsigned char)(lba>>24);q[19]=(unsigned char)(lba>>16);
 q[20]=(unsigned char)(lba>>8);q[21]=(unsigned char)lba;
 q[23]=(unsigned char)(blocks>>8);q[24]=(unsigned char)blocks;
}
