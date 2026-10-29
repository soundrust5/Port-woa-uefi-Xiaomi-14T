// Actual GOP source, mocked MMIO/BLT/Boot Services. No physical device access.
#define MDEPKG_NDEBUG
#define _PCD_VALUE_PcdFrameBufferWidth 1220
#define _PCD_VALUE_PcdFrameBufferHeight 2712
#define _PCD_VALUE_PcdFrameBufferColorDepth 32
#define _PCD_VALUE_PcdFrameBufferPixelsPerScanLine 1248
#include <Uefi.h>
#include <Library/IoLib.h>
#include <Library/TimerLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

static UINT32 controls[4],initial[4];
static UINTN addresses[128];static UINT32 values[128];
static unsigned writes,control_writes,fail_write,cache_calls,delays,blts;
static unsigned idle_reads,busy_after;
static UINT32 intsta;static int no_done,ignore_ack;
static int rollback_fail,busy,wrong_geometry;static EFI_TPL current_tpl;
static const UINTN control_addresses[]={0x14402030,0x14602030,0x14402044,0x14602044};

static int control_index(UINTN address){
 for(unsigned i=0;i<4;i++)if(address==control_addresses[i])return (int)i;
 return -1;
}
UINT32 EFIAPI MmioRead32(UINTN address){
 int index=control_index(address);if(index>=0)return controls[index];
 switch(address){
 case 0x14402f40:return wrong_geometry?0xfcc2f004:0xfcc2f000;
 case 0x14602f40:return 0xfcc2f95c;
 case 0x14402038:case 0x14602038:return 0x0a98026d;
 case 0x14001030:return 0xbc7423ff;case 0x14201030:return 0x9c6083ff;
 case 0x14401030:case 0x14601030:return 0x8007;
 case 0x1400d00c:idle_reads++;return intsta|((busy || (busy_after && idle_reads>=busy_after))?BIT31:0);
 default:return 0;
 }
}
UINT32 EFIAPI MmioWrite32(UINTN address,UINT32 value){
 int index=control_index(address);assert(writes<128);
 addresses[writes]=address;values[writes++]=value;
 if(address==0x1400d00c){assert(value==0xffffffefU);if(!ignore_ack)intsta&=value;}
 if(address==0x1400d000 && (value&1) && !no_done)intsta|=BIT4;
 if(index>=0){
  control_writes++;
  if(control_writes==fail_write)return value;
  if(rollback_fail && control_writes>fail_write && index==0 && value==initial[0])return value;
  controls[index]=value;
 }
 return value;
}
UINT32 EFIAPI MmioOr32(UINTN address,UINT32 bits){return MmioWrite32(address,MmioRead32(address)|bits);}
UINT32 EFIAPI MmioAnd32(UINTN address,UINT32 bits){return MmioWrite32(address,MmioRead32(address)&bits);}
UINTN EFIAPI MicroSecondDelay(UINTN us){delays+=(unsigned)us;return us;}
VOID EFIAPI MemoryFence(VOID){}
VOID *EFIAPI CopyMem(VOID *dest,CONST VOID *src,UINTN n){return memcpy(dest,src,n);}
VOID *EFIAPI WriteBackDataCacheRange(VOID *address,UINTN length){
 assert((UINTN)address==0xfcc2f000 && length==4992U*2712U);cache_calls++;return address;
}
static EFI_TPL EFIAPI Raise(EFI_TPL tpl){EFI_TPL old=current_tpl;assert(tpl==TPL_NOTIFY);current_tpl=tpl;return old;}
static VOID EFIAPI Restore(EFI_TPL tpl){assert(current_tpl==TPL_NOTIFY);current_tpl=tpl;}
static EFI_BOOT_SERVICES services={.RaiseTPL=Raise,.RestoreTPL=Restore};
EFI_BOOT_SERVICES *gBS=&services;

#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/DegasGopDxe/DegasGop.c"

RETURN_STATUS EFIAPI FrameBufferBlt(FRAME_BUFFER_CONFIGURE *configuration,
 EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer,EFI_GRAPHICS_OUTPUT_BLT_OPERATION operation,
 UINTN sx,UINTN sy,UINTN dx,UINTN dy,UINTN width,UINTN height,UINTN delta){
 (void)configuration;(void)sx;(void)sy;(void)delta;
 assert(operation==EfiBltVideoFill && width==1 && height==1);blts++;
 memcpy((VOID *)(UINTN)(0xfcc2f000ULL+dy*4992+dx*4),buffer,4);return RETURN_SUCCESS;
}

static void reset(void){
 controls[0]=0x2000;controls[1]=0x2011;controls[2]=controls[3]=4992;
 memcpy(initial,controls,sizeof(initial));writes=control_writes=fail_write=0;
 rollback_fail=busy=wrong_geometry=0;cache_calls=delays=blts=0;
 idle_reads=busy_after=0;
 intsta=0x16;no_done=ignore_ack=0;mDegasCpuRefreshLocked=FALSE;
 mDegasOpaqueVerified=mDegasOpaqueRollbackFailed=FALSE;current_tpl=TPL_APPLICATION;gBS=&services;
}
static void setup_success(void){
 assert(DegasDisplayPrepareOpaque()==EFI_SUCCESS);
 assert(controls[0]==0x21ff && controls[1]==0x21ff);
 assert(controls[2]==0x10001380 && controls[3]==0x10001380);
 assert(writes==4 && mDegasOpaqueVerified && !mDegasOpaqueRollbackFailed);
 for(unsigned i=0;i<4;i++)assert(addresses[i]==control_addresses[i]);
 assert(!cache_calls && !blts); // Setup never accesses the framebuffer.
 unsigned before=writes;assert(DegasDisplayPrepareOpaque()==EFI_SUCCESS && writes==before);
}
static int transfer_address(UINTN address){
 static const UINTN allowed[]={0x1400d00c,0x1400d190,0x1400d008,0x1400d014,0x1400d100,0x1400d104,
  0x14201008,0x14201020,0x14601008,0x14601020,0x14001008,0x14001020,
  0x14401008,0x14401020,0x1400dd00,0x1400d060,0x1400d000};
 for(unsigned i=0;i<sizeof(allowed)/sizeof(*allowed);i++)if(address==allowed[i])return 1;
 return 0;
}
int main(void){
 const size_t size=0xcea000;VOID *frame=mmap((VOID *)0xfcc2f000,size,PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(frame==(VOID *)0xfcc2f000);UINT8 *copy=malloc(size);assert(copy);
 reset();setup_success();
 // Flush has no Boot Services or BLT dependency and preserves every pixel byte.
 memset(frame,0x37,size);memcpy(copy,frame,size);writes=control_writes=0;gBS=NULL;
 assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_SUCCESS);
 assert(writes==19 && control_writes==0 && cache_calls==1 && !blts);
 for(unsigned i=0;i<writes;i++)assert(transfer_address(addresses[i]));
 assert(!memcmp(frame,copy,size));
 assert(mDegasDisplayRefresh.Flush(NULL)==EFI_INVALID_PARAMETER);
 // RGBX zero reserved byte survives GOP drawing once hardware is opaque.
 gBS=&services;EFI_GRAPHICS_OUTPUT_BLT_PIXEL color={3,2,1,0};writes=0;
 assert(FrameBufferGopBlt(NULL,&color,EfiBltVideoFill,0,0,10,20,1,1,0)==EFI_SUCCESS);
 assert(*(UINT32 *)((UINT8 *)frame+20*4992+10*4)==0x00010203 && blts==1);
 // Once verified, drift cannot silently re-enable the CPU-alpha workaround.
 controls[0]^=BIT24;writes=0;
 assert(FrameBufferGopBlt(NULL,&color,EfiBltVideoFill,0,0,10,20,1,1,0)==EFI_DEVICE_ERROR);
 assert(writes==0 && blts==1);
 // Unknown/pre-multiplied formats fail before MMIO writes, retaining legacy GOP.
 const UINT32 invalid_con[]={0x8031ff,0x010021ff,0x3000,0x10002000};
 for(unsigned i=0;i<sizeof(invalid_con)/sizeof(*invalid_con);i++){
  reset();controls[0]=invalid_con[i];assert(DegasDisplayPrepareOpaque()==EFI_UNSUPPORTED && !writes);
  gBS=NULL;assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_UNSUPPORTED && !writes);
 }
 reset();controls[0]=controls[1]=0x8031ff;
 assert(DegasDisplayPrepareOpaque()==EFI_UNSUPPORTED && !writes);
 assert(FrameBufferGopBlt(NULL,&color,EfiBltVideoFill,0,0,10,20,1,1,0)==EFI_SUCCESS);
 assert(*(UINT32 *)((UINT8 *)frame+20*4992+10*4)==0xff010203);
 reset();controls[3]|=BIT31;assert(DegasDisplayPrepareOpaque()==EFI_UNSUPPORTED && !writes);
 reset();wrong_geometry=1;assert(DegasDisplayPrepareOpaque()==EFI_UNSUPPORTED && !writes);
 reset();busy=1;assert(DegasDisplayPrepareOpaque()==EFI_TIMEOUT && !writes && delays==100000);
 // Fault each individual write; reverse rollback restores both views exactly.
 for(unsigned failure=1;failure<=4;failure++){
  reset();fail_write=failure;assert(DegasDisplayPrepareOpaque()==EFI_DEVICE_ERROR);
  assert(!memcmp(controls,initial,sizeof(initial)) && !mDegasOpaqueVerified && !mDegasOpaqueRollbackFailed);
  assert(DegasOvlLegacyReady());
 }
 reset();fail_write=2;rollback_fail=1;
 assert(DegasDisplayPrepareOpaque()==EFI_DEVICE_ERROR && mDegasOpaqueRollbackFailed);
 writes=0;assert(FrameBufferGopBlt(NULL,&color,EfiBltVideoFill,0,0,10,20,1,1,0)==EFI_UNSUPPORTED);
 assert(!writes && !blts);
 // Already opaque state is recognized without redundant register writes.
 reset();controls[0]=controls[1]=0x21ff;controls[2]=controls[3]=0x10001380;
 assert(DegasDisplayPrepareOpaque()==EFI_SUCCESS && !writes);
 // Busy/guard failure in Flush performs no transfer or pixel writes.
 busy=1;gBS=NULL;assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_TIMEOUT && !writes);
 busy=0;wrong_geometry=1;assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_UNSUPPORTED && !writes);
 // A started transfer which remains busy also has a bounded failure result.
 reset();setup_success();writes=control_writes=idle_reads=0;busy_after=5;gBS=NULL;
 memcpy(copy,frame,size);
 assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_TIMEOUT);
 assert(writes==19 && !control_writes && delays==100000 && !memcmp(frame,copy,size));
 reset();setup_success();writes=0;no_done=1;gBS=NULL;
 assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_TIMEOUT && delays==100000);
 assert(!(intsta&BIT4)); // stale completion was cleared; no false success
 reset();setup_success();writes=0;ignore_ack=1;gBS=NULL;
 assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_DEVICE_ERROR && writes==1 && !cache_calls);
 reset();setup_success();writes=0;gBS=NULL;
 assert(mDegasDisplayRefresh.LockCpuRefresh(NULL)==EFI_INVALID_PARAMETER);
 assert(mDegasDisplayRefresh.LockCpuRefresh(&mDegasDisplayRefresh)==EFI_SUCCESS);
 assert(mDegasDisplayRefresh.Flush(&mDegasDisplayRefresh)==EFI_ACCESS_DENIED);
 assert(FrameBufferGopBlt(NULL,&color,EfiBltVideoFill,0,0,10,20,1,1,0)==EFI_ACCESS_DENIED);
 UINTN infosize=0;EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info=NULL;
 assert(FrameBufferQueryMode(&mFrameBuffer,0,&infosize,&info)==EFI_ACCESS_DENIED);
 assert(!writes && !blts && !cache_calls); // no BS, pixels, cache or MMIO after lock
 free(copy);assert(!munmap(frame,size));
 puts("PASS: actual GOP opaque masks, fail-closed formats, per-write rollback, legacy fallback, RGBX preservation, BS-free flush");
 return 0;
}
