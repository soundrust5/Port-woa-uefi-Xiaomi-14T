// Host regression: actual UEFI types and actual read-only Block I/O callbacks.
#include <Uefi.h>
#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/PartitionInfo.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/LoadedImage.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#define DEBUG(x) ((void)0)
#define ZeroMem(p,n) memset(p,0,n)
#define CompareMem(a,b,n) memcmp(a,b,n)
#define FreePool(p) free(p)
#define CopyMem(p,q,n) memcpy(p,q,n)
static BOOLEAN IsZeroGuid(const EFI_GUID *g){EFI_GUID z={0};return !memcmp(g,&z,sizeof z);}
EFI_GUID gEfiBlockIoProtocolGuid,gEfiDevicePathProtocolGuid,gEfiPartitionInfoProtocolGuid;
EFI_GUID gEfiSimpleFileSystemProtocolGuid,gEfiLoadedImageProtocolGuid;
static EFI_HANDLE mCheckpointImage;
static EFI_DEVICE_PATH_PROTOCOL *FileDevicePath(EFI_HANDLE H,CHAR16 *P){return NULL;}
static unsigned packets,bad,fail_at,watchdogs,tpl;
static UINT32 packet_lba[8],packet_bytes[8];
static EFI_TPL EFIAPI Raise(EFI_TPL t){assert(t==TPL_NOTIFY&&!tpl);tpl=1;return TPL_APPLICATION;}
static VOID EFIAPI Restore(EFI_TPL t){assert(t==TPL_APPLICATION&&tpl);tpl=0;}
static EFI_BOOT_SERVICES bs={.RaiseTPL=Raise,.RestoreTPL=Restore};
static EFI_BOOT_SERVICES *gBS=&bs;
static void MmioWrite32(UINTN a,UINT32 v){assert(a==0x1c00a008&&v==0x1971);watchdogs++;}
static UINT64 GetPerformanceCounter(void){return 1;}
static UINT64 GetTimeInNanoSecond(UINT64 n){return n;}
static void BootWatchdogFeed(void){MmioWrite32(0x1c00a008,0x1971);}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsReadCommand.h"
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsCopy.h"
static BOOLEAN UfsProbePacket(const UINT8 *q,UINT32 n){
 UINT8 *r=(void*)0x44471080,*d=(void*)0x44472000;
 assert(tpl && DegasUfsReadValid(q,n));
 UINT32 l=((UINT32)q[18]<<24)|((UINT32)q[19]<<16)|((UINT32)q[20]<<8)|q[21];
 assert(packets<8);packet_lba[packets]=l;packet_bytes[packets]=n;packets++;
 memset(r,0,64);
 for(unsigned j=0;j<n/4096;j++)memset(d+j*4096,(UINT8)(l+j),4096);
 r[0]=0x21;r[2]=2;r[3]=q[3];
 if(packets==fail_at){
  if(bad==1)r[3]^=1;
  if(bad==2){r[1]=0x20;r[15]=1;}
  if(bad==4)r[7]=2;
  if(bad==5)r[0]=0x36;
  return bad!=3;
 }
 return TRUE;
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsBlockIo.h"
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsFileTest.h"
static void init(unsigned b){
 memset(&mUfsMedia,0,sizeof mUfsMedia);mUfsMedia.MediaId=2;mUfsMedia.LastBlock=0x03d95fff;
 mUfsMedia.MediaPresent=TRUE;mUfsMedia.ReadOnly=TRUE;mUfsMedia.BlockSize=4096;mUfsMedia.IoAlign=1;
 mUfsActive=TRUE;mUfsReads=mUfsWriteAttempts=packets=watchdogs=tpl=0;bad=b;fail_at=1;
 memset(packet_lba,0,sizeof packet_lba);memset(packet_bytes,0,sizeof packet_bytes);
}
static void content(const UINT8 *b,unsigned blocks,UINT32 lba){
 for(unsigned j=0;j<blocks;j++)for(unsigned k=0;k<4096;k++)assert(b[j*4096+k]==(UINT8)(lba+j));
}
static void untouched(const UINT8 *b,unsigned n){for(unsigned i=0;i<n;i++)assert(b[i]==0xa5);}
int main(void){
 assert(mmap((void*)0x44470000,65536,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void*)0x44470000);
 UINT8 b[29*4096+32];init(0);
 assert(UfsBioRead(&mUfsBio,2,0,8192,b+1)==EFI_SUCCESS);assert(packets==1&&b[1]==0&&b[4097]==1&&watchdogs==1&&!tpl);
 assert(UfsBioRead(&mUfsBio,2,0x03d95fff,4096,b)==EFI_SUCCESS);assert(b[0]==255);
 unsigned p=packets;
 assert(UfsBioRead(&mUfsBio,3,0,4096,b)==EFI_MEDIA_CHANGED);
 assert(UfsBioRead(&mUfsBio,2,0,4095,b)==EFI_BAD_BUFFER_SIZE);
 assert(UfsBioRead(&mUfsBio,2,0,4096,NULL)==EFI_INVALID_PARAMETER);
 assert(UfsBioRead(&mUfsBio,2,0x03d95fff,8192,b)==EFI_INVALID_PARAMETER);
 assert(UfsBioRead(&mUfsBio,2,~(EFI_LBA)0,4096,b)==EFI_INVALID_PARAMETER);
 assert(UfsBioRead(&mUfsBio,2,(EFI_LBA)DEGAS_UFS_LAST_LBA+1,4096,b)==EFI_INVALID_PARAMETER);
 assert(UfsBioRead(NULL,2,0,4096,b)==EFI_INVALID_PARAMETER);
 assert(UfsBioRead(&mUfsBio,2,0,0,NULL)==EFI_SUCCESS);
 assert(UfsBioWrite(&mUfsBio,2,0,4096,b)==EFI_WRITE_PROTECTED);assert(packets==p&&mUfsWriteAttempts==1&&!tpl);
 // Exercise every caller alignment through the real copy helper and batched read.
 for(unsigned offset=0;offset<16;offset++){
  init(0);memset(b,0xa5,sizeof b);
  assert(UfsBioRead(&mUfsBio,2,123,29*4096,b+offset)==EFI_SUCCESS);
  assert(packets==3&&watchdogs==3&&!tpl&&mUfsActive);
  assert(packet_lba[0]==123&&packet_lba[1]==137&&packet_lba[2]==151);
  assert(packet_bytes[0]==57344&&packet_bytes[1]==57344&&packet_bytes[2]==4096);
  content(b+offset,29,123);untouched(b,offset);untouched(b+offset+29*4096,sizeof b-offset-29*4096);
 }
 init(0);memset(b,0xa5,sizeof b);
 assert(UfsBioRead(&mUfsBio,2,DEGAS_UFS_LAST_LBA-28,29*4096,b+1)==EFI_SUCCESS);
 assert(packets==3&&packet_lba[2]==DEGAS_UFS_LAST_LBA&&packet_bytes[2]==4096);
 content(b+1,29,DEGAS_UFS_LAST_LBA-28);assert(b[0]==0xa5&&b[29*4096+1]==0xa5);
 init(0);assert(FileRead(&mFileBio,3,0,4096,b)==EFI_SUCCESS);
 assert(FileRead(&mFileBio,3,8191,4096,b)==EFI_SUCCESS);assert(packets==2);
 assert(FileRead(&mFileBio,3,8191,8192,b)==EFI_INVALID_PARAMETER);
 assert(FileRead(&mFileBio,3,8192,4096,b)==EFI_INVALID_PARAMETER);
 assert(FileRead(&mFileBio,3,~(EFI_LBA)0,4096,b)==EFI_INVALID_PARAMETER);
 assert(FileRead(&mFileBio,2,0,4096,b)==EFI_MEDIA_CHANGED);assert(packets==2);
 for(unsigned k=1;k<=5;k++){
  init(k);memset(b,0xa5,sizeof b);
  assert(UfsBioRead(&mUfsBio,2,0,4096,b)==EFI_DEVICE_ERROR);assert(!mUfsActive&&!tpl&&packets==1&&!watchdogs);
  untouched(b,sizeof b);
  assert(UfsBioRead(&mUfsBio,2,1,4096,b)==EFI_DEVICE_ERROR);assert(packets==1);
  // A rejected second chunk must preserve completed output and the unread tail.
  init(k);fail_at=2;memset(b,0xa5,sizeof b);
  assert(UfsBioRead(&mUfsBio,2,123,29*4096,b+1)==EFI_DEVICE_ERROR);
  assert(!mUfsActive&&!tpl&&packets==2&&watchdogs==1);
  content(b+1,14,123);assert(b[0]==0xa5);untouched(b+1+14*4096,sizeof b-1-14*4096);
  assert(UfsBioRead(&mUfsBio,2,0,4096,b)==EFI_DEVICE_ERROR&&packets==2);
 }
 assert(munmap((void*)0x44470000,65536)==0);
 puts("PASS: BlockIO 14+14+1 batching, all caller alignments/canaries, last LBA, read-only and first/second-chunk failures");
 return 0;
}
