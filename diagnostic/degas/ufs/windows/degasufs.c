/* Experimental MT6897 Storport miniport, bounded scratch writes, firmware-initialized link.
 * Own uncached common buffer; never reuses the firmware DMA arena. Legacy UTRL
 * slot 0, polled completion, bounded timeout. No Qualcomm code or vendor SMC.
 * Requires the explicit live-link/idle-queues UEFI handoff. Not boot qualified.
 */
#include <ntddk.h>
#include <storport.h>

#define HCI_PA 0x112b0000ULL
#define HCI_SIZE 0x3000
#ifndef DEGAS_UFS_PERFORMANCE
#define DEGAS_UFS_PERFORMANCE 0
#endif
#if DEGAS_UFS_PERFORMANCE != 0 && DEGAS_UFS_PERFORMANCE != 1
#error DEGAS_UFS_PERFORMANCE must be 0 or 1
#endif
// One PRDT entry supports up to 256 KiB. Extra allocation includes descriptor
// space at +0x2000 and up to1023 bytes to align the private arena to1024.
#define MAX_DATA (DEGAS_UFS_PERFORMANCE ? 0x40000u : 0xe000u)
#define DMA_SIZE (0x2000u + MAX_DATA + 0x1000u)
C_ASSERT(MAX_DATA<=0x40000u && !(MAX_DATA&4095u));
#define FATAL_IS 0x70880
#include "retained_log.h"
#include "../write_policy.h"
#include "../install_policy.h"
typedef struct { PVOID Buffer; ULONG MapStatus; } REQUEST;
typedef struct {
 PUCHAR Reg,Dma; ULONG DmaPa;
 PSCSI_REQUEST_BLOCK Pending;
 ULONG Ticks,Submitted,Completed,Timeouts,Blocked,LastIs,LastOcs,Stage,Requests;
 LARGE_INTEGER Deadline;
 LARGE_INTEGER RecoveryStopAt;
 ULONG WireBytes,CompletionBytes; UCHAR WireLun,WireOpcode;
 BOOLEAN FlushAfterWrite,FlushPhase;
 BOOLEAN Ready,Dead; UCHAR Tag;
} ADAPTER;

static ULONG Rd(ADAPTER *a,ULONG o){return StorPortReadRegisterUlong(a,(PULONG)(a->Reg+o));}
static void Wr(ADAPTER *a,ULONG o,ULONG v){StorPortWriteRegisterUlong(a,(PULONG)(a->Reg+o),v);}
static void Zero(PVOID p,ULONG n){volatile UCHAR *q=p;while(n--)*q++=0;}
static void Copy(PVOID p,const void *s,ULONG n){
#if DEGAS_UFS_PERFORMANCE
 RtlCopyMemory(p,s,n);
#else
 UCHAR *q=p;const UCHAR *r=s;while(n--)*q++=*r++;
#endif
}
static ULONG Be32(const UCHAR *p){return ((ULONG)p[0]<<24)|((ULONG)p[1]<<16)|((ULONG)p[2]<<8)|p[3];}
static ULONGLONG Be64(const UCHAR *p){return ((ULONGLONG)Be32(p)<<32)|Be32(p+4);}
static void Put32(PUCHAR p,ULONG n){p[0]=(UCHAR)(n>>24);p[1]=(UCHAR)(n>>16);p[2]=(UCHAR)(n>>8);p[3]=(UCHAR)n;}

/* Called only under Storport's half-duplex synchronization. On timeout the
 * controller is quarantined; no buffer is reused for another request. */
static HW_TIMER Poll;
static BOOLEAN Stop(ADAPTER *a){
 ULONG i;a->Ready=FALSE;a->Dead=TRUE;
 StorPortNotification(RequestTimerCall,a,Poll,0);
 Wr(a,0x24,0);Wr(a,0x60,0);Wr(a,0x80,0);Wr(a,0x34,0);
 for(i=0;i<1000;i++){if(!(Rd(a,0x34)&1))return TRUE;StorPortStallExecution(1);}
 return FALSE;
}
static void Complete(ADAPTER *a,PSCSI_REQUEST_BLOCK s,UCHAR status){
 if(a->Requests<=64)Retain("SRB complete status scsi bytes",status,s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->ScsiStatus:0,s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->DataTransferLength:0);
 s->SrbStatus=status;StorPortNotification(RequestComplete,a,s);
}
static void Sense(ADAPTER *a,PSCSI_REQUEST_BLOCK s,UCHAR key,UCHAR asc){
 UCHAR status=SRB_STATUS_ERROR;s->ScsiStatus=SCSISTAT_CHECK_CONDITION;s->DataTransferLength=0;
 if(s->SenseInfoBuffer && s->SenseInfoBufferLength>=18){
  PUCHAR p=s->SenseInfoBuffer;Zero(p,18);p[0]=0x70;p[2]=key;p[7]=10;p[12]=asc;
  status|=SRB_STATUS_AUTOSENSE_VALID;
 }
 Complete(a,s,status);
}
/* Whitelist. Writes never escape the compile-time authorized extents.
 * UNMAP, FORMAT, SECURITY OUT, MODE SELECT and vendor commands are rejected. */
static BOOLEAN Allowed(PSCSI_REQUEST_BLOCK s){
 const UCHAR *c=s->Cdb;ULONGLONG lba=0,blocks=0,limit=s->Lun==2?64577536ULL:1024;
 if(c[0]==0x2a || c[0]==0x8a){
  return DegasUfsWriteAllowed(c,s->CdbLength,s->Lun,s->DataTransferLength,MAX_DATA) &&
   (s->SrbFlags&SRB_FLAGS_DATA_OUT) && !(s->SrbFlags&SRB_FLAGS_DATA_IN) &&
   s->DataTransferLength;
 }
 if(c[0]==0x35)return s->CdbLength==10 && s->Lun==2 && !s->DataTransferLength &&
   !(s->SrbFlags&(SRB_FLAGS_DATA_IN|SRB_FLAGS_DATA_OUT)) &&
   !c[1] && !Be32(c+2) && !c[6] && !c[7] && !c[8] && !c[9];
 if(!s->CdbLength || s->CdbLength>16 || s->DataTransferLength>MAX_DATA ||
    (s->SrbFlags&SRB_FLAGS_DATA_OUT))return FALSE;
 switch(c[0]){
 case 0x00: return s->CdbLength==6 && s->DataTransferLength==0; /* TUR */
 case 0x03: case 0x12: case 0x1a: return s->CdbLength==6;
 case 0x25: return s->CdbLength==10 && s->DataTransferLength>=8;
 case 0x5a: return s->CdbLength==10;
 case 0x9e: return s->CdbLength==16 && c[1]==0x10 && Be32(c+10)<=s->DataTransferLength;
 case 0x28:
  if(s->CdbLength!=10)return FALSE;lba=Be32(c+2);blocks=((ULONG)c[7]<<8)|c[8];break;
 case 0x88:
  if(s->CdbLength!=16)return FALSE;lba=Be64(c+2);blocks=Be32(c+10);break;
 default:return FALSE;
 }
 return blocks && blocks<=MAX_DATA/4096 && lba<limit && blocks<=limit-lba &&
        s->DataTransferLength==blocks*4096;
}
static BOOLEAN Send(ADAPTER *a,const UCHAR *cdb,UCHAR length,UCHAR lun,ULONG bytes,BOOLEAN nop){
 PULONG t=(PULONG)a->Dma,p=(PULONG)(a->Dma+0x1100);PUCHAR q=a->Dma+0x1000;
 BOOLEAN write=!nop && (cdb[0]==0x2a || cdb[0]==0x8a);
 if(a->Dead || (Rd(a,0x30)&15)!=15 || Rd(a,0x58) || !(Rd(a,0x34)&1))return FALSE;
 Zero(a->Dma,32);Zero(q,0x120);a->Tag++;if(!a->Tag)a->Tag++;
 q[0]=nop?0:1;q[1]=bytes?(write?0x20:0x40):0;q[2]=lun;q[3]=a->Tag;
 if(!nop){Put32(q+12,bytes);Copy(q+16,cdb,length);}
 t[0]=bytes?(write?0x13000000:0x15000000):0x11000000;t[2]=15;t[4]=a->DmaPa+0x1000;
 t[6]=0x00200010; /* response at128, room64 bytes */
 if(bytes){t[7]=0x00400001;p[0]=a->DmaPa+0x2000;p[3]=((bytes+3)&~3u)-1;}
 KeMemoryBarrier();Wr(a,0x20,Rd(a,0x20));Wr(a,0x60,1);
 if(!(Rd(a,0x60)&1))return FALSE;
 KeMemoryBarrier();Wr(a,0x58,1);a->Submitted++;return TRUE;
}
static BOOLEAN TransportDone(ADAPTER *a){
 KeMemoryBarrier();a->LastIs=Rd(a,0x20);a->LastOcs=((volatile ULONG*)a->Dma)[2]&255;
 return !Rd(a,0x58) && !(a->LastIs&FATAL_IS) && !a->LastOcs;
}
static VOID Poll(PVOID extension){
 ADAPTER *a=extension;PSCSI_REQUEST_BLOCK s=a->Pending;PUCHAR r=a->Dma+0x1080;
 ULONG residual,actual,senseBytes,dataBytes;REQUEST *req;UCHAR status;LARGE_INTEGER now;
 if(a->Dead)return;
 StorPortQuerySystemTime(&now);
 if(a->RecoveryStopAt.QuadPart && now.QuadPart>=a->RecoveryStopAt.QuadPart){
  Retain("pre-reset quiesce",a->Completed,a->Timeouts,Rd(a,0x58));
  Retain("pre-reset requests submitted blocked",a->Requests,a->Submitted,a->Blocked);
  Stop(a);a->Pending=NULL;
  if(s){s->DataTransferLength=0;Complete(a,s,SRB_STATUS_BUS_RESET);}
  return;
 }
 // Keep a recovery tick even when no disk request is pending.
 if(a->RecoveryStopAt.QuadPart && !a->Dead)StorPortNotification(RequestTimerCall,a,Poll,10000);
 if(!s)return;
 if(Rd(a,0x58)&1){
  StorPortQuerySystemTime(&now);
  if(!(Rd(a,0x20)&FATAL_IS) && now.QuadPart<a->Deadline.QuadPart &&
     ++a->Ticks<(a->WireOpcode==0x35?10000u:1000u)){StorPortNotification(RequestTimerCall,a,Poll,1000);return;}
  a->Timeouts++;Retain("command timeout",a->WireOpcode,Rd(a,0x58),Rd(a,0x20));Stop(a);a->Pending=NULL;s->DataTransferLength=0;Complete(a,s,SRB_STATUS_TIMEOUT);return;
 }
 a->Pending=NULL;Wr(a,0x60,0);
 if(!TransportDone(a) || r[0]!=0x21 || r[2]!=a->WireLun || r[3]!=a->Tag || r[8] || r[6]>1){
  Retain("transport failure",a->LastIs,a->LastOcs,((ULONG)r[0]<<24)|((ULONG)r[2]<<16)|((ULONG)r[3]<<8)|r[6]);
  Stop(a);s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return;
 }
 residual=Be32(r+12);dataBytes=((ULONG)r[10]<<8)|r[11];
 if((r[1]&0x40) || residual>a->WireBytes || (!(r[1]&0x20)&&residual)){
  Stop(a);s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return;
 }
 actual=a->WireBytes-residual;req=s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->SrbExtension:NULL;
 if(s->Function==SRB_FUNCTION_EXECUTE_SCSI)s->ScsiStatus=r[7];
 if(r[7]==SCSISTAT_GOOD && r[6]==0 && !dataBytes){
  if(actual && (!req || !req->Buffer)){Stop(a);s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return;}
  if(actual && a->WireOpcode!=0x2a && a->WireOpcode!=0x8a)Copy(req->Buffer,a->Dma+0x2000,actual);
  if(a->FlushAfterWrite && !a->FlushPhase && !residual){
   UCHAR flush[10]={0x35};
   /* Do not expose successful FUA until the device confirms cache sync. */
   a->CompletionBytes=actual;a->FlushPhase=TRUE;a->WireBytes=0;a->WireOpcode=0x35;
   a->Ticks=0;a->Pending=s;StorPortQuerySystemTime(&a->Deadline);a->Deadline.QuadPart+=100000000;
   if(!Send(a,flush,10,a->WireLun,0,FALSE)){
    a->Pending=NULL;Stop(a);s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return;
   }
   StorPortNotification(RequestTimerCall,a,Poll,1000);return;
  }
  if(a->FlushPhase)actual=a->CompletionBytes;
  if(a->WireOpcode==0x35 || ((a->WireOpcode==0x2a || a->WireOpcode==0x8a) && !(a->Completed%1024)))Retain("write/flush completed",a->WireOpcode,actual,residual);
  /* Force software-visible write protection in both MODE SENSE formats. */
  if(!(DEGAS_UFS_INSTALL_MODE && s->Lun==2)){
   if(a->WireOpcode==0x1a && actual>=3)((PUCHAR)req->Buffer)[2]|=0x80;
   if(a->WireOpcode==0x5a && actual>=4)((PUCHAR)req->Buffer)[3]|=0x80;
  }
  s->DataTransferLength=actual;a->Completed++;
  if(a->Completed<=16 || !(a->Completed%1024))Retain("command complete",((ULONG)a->WireLun<<16)|a->WireOpcode,actual,a->Completed);
  Complete(a,s,residual?SRB_STATUS_DATA_OVERRUN:SRB_STATUS_SUCCESS);return;
 }
 s->DataTransferLength=0;status=SRB_STATUS_ERROR;senseBytes=((ULONG)r[32]<<8)|r[33];
 if(s->Function==SRB_FUNCTION_EXECUTE_SCSI && r[7]==SCSISTAT_CHECK_CONDITION && senseBytes && senseBytes<=18 && dataBytes==senseBytes+2 && s->SenseInfoBuffer){
  if(senseBytes>s->SenseInfoBufferLength)senseBytes=s->SenseInfoBufferLength;
  if(senseBytes){Copy(s->SenseInfoBuffer,r+34,senseBytes);status|=SRB_STATUS_AUTOSENSE_VALID;}
 }
 Complete(a,s,status);
}
static HW_BUILDIO BuildIo;
static void TraceMapping(PVOID extension,PSCSI_REQUEST_BLOCK s,PVOID buffer){
 PSTOR_SCATTER_GATHER_LIST sg;PHYSICAL_ADDRESS cpu;
 if(s->Cdb[0]!=0xa0 || !buffer || !s->DataTransferLength)return;
 cpu=MmGetPhysicalAddress(buffer);sg=StorPortGetScatterGatherList(extension,s);
 Retain("REPORT buffer CPU physical",cpu.HighPart,cpu.LowPart,s->DataTransferLength);
 if(sg && sg->NumberOfElements)Retain("REPORT buffer DMA physical",sg->List[0].PhysicalAddress.HighPart,sg->List[0].PhysicalAddress.LowPart,sg->List[0].Length);
 else Retain("REPORT no DMA list",sg!=NULL,0,0);
}
static BOOLEAN BuildIo(PVOID extension,PSCSI_REQUEST_BLOCK s){
 REQUEST *r;
 if(s->Function!=SRB_FUNCTION_EXECUTE_SCSI)return TRUE;
 r=s->SrbExtension;if(!r)return TRUE;Zero(r,sizeof(*r));
 // MapBuffers=ALL_INCLUDING_READ_WRITE: Storport supplies the transfer's
 // mapped VA here, including any DMA bounce MDL. GetSystemAddress maps the
 // original MDL instead and bypasses that replacement on this ARM64 build.
 if(s->DataTransferLength){
  r->Buffer=s->DataBuffer;
  r->MapStatus=r->Buffer?STOR_STATUS_SUCCESS:STOR_STATUS_INSUFFICIENT_RESOURCES;
 }
 if(s->Cdb[0]==0xa0 || r->MapStatus)Retain("BuildIo opcode map buffer",s->Cdb[0],r->MapStatus,r->Buffer!=NULL);
 if(r->MapStatus==STOR_STATUS_SUCCESS)TraceMapping(extension,s,r->Buffer);
 return TRUE;
}
static HW_STARTIO StartIo;
static BOOLEAN StartIo(PVOID extension,PSCSI_REQUEST_BLOCK s){
 ADAPTER *a=extension;REQUEST *r=s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->SrbExtension:NULL;ULONG i,bytes;
 UCHAR wire[16]={0};UCHAR wireLength;
 BOOLEAN flush=s->Function==SRB_FUNCTION_FLUSH || s->Function==SRB_FUNCTION_SHUTDOWN;
 a->Requests++;
 if(a->Requests<=64){
  Retain("StartIo function opcode flags",s->Function,s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->Cdb[0]:0,s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->SrbFlags:0);
  Retain("StartIo BTL length extension",((ULONG)s->PathId<<16)|((ULONG)s->TargetId<<8)|s->Lun,s->Function==SRB_FUNCTION_EXECUTE_SCSI?s->DataTransferLength:0,r!=NULL);
 }
 if(!flush && s->Function!=SRB_FUNCTION_EXECUTE_SCSI){Complete(a,s,SRB_STATUS_INVALID_REQUEST);return TRUE;}
 if(!flush)s->ScsiStatus=0;
 if(!flush && !r){s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return TRUE;}
 if(s->PathId || s->TargetId || s->Lun>2){s->DataTransferLength=0;Complete(a,s,SRB_STATUS_NO_DEVICE);return TRUE;}
 if(!a->Ready || a->Dead){s->DataTransferLength=0;Complete(a,s,SRB_STATUS_NO_DEVICE);return TRUE;}
 if(a->Pending){Complete(a,s,SRB_STATUS_BUSY);return TRUE;}
 if(!flush && s->DataTransferLength && (r->MapStatus!=STOR_STATUS_SUCCESS || !r->Buffer)){
  s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return TRUE;
 }
 if(!flush && s->CdbLength==12 && s->Cdb[0]==0xa0){ /* REPORT LUNS, captured physical0..2 */
  UCHAR reply[32]={0};Put32(reply,24);reply[17]=1;reply[25]=2;
  if(s->SrbFlags&SRB_FLAGS_DATA_OUT){Sense(a,s,5,0x24);return TRUE;}
  bytes=s->DataTransferLength;if(bytes>Be32(s->Cdb+6))bytes=Be32(s->Cdb+6);if(bytes>32)bytes=32;
  for(i=0;i<bytes;i++)((PUCHAR)r->Buffer)[i]=reply[i];
  // Storport needs DATA_OVERRUN for an underrun as well as actual byte count.
  // Enumeration requests 64 bytes while three LUN descriptors occupy 32.
  UCHAR result=bytes<s->DataTransferLength?SRB_STATUS_DATA_OVERRUN:SRB_STATUS_SUCCESS;
  Retain("REPORT LUNS requested actual header",s->DataTransferLength,bytes,bytes>=4?Be32((PUCHAR)r->Buffer):0);
  s->DataTransferLength=bytes;Complete(a,s,result);return TRUE;
 }
 a->FlushAfterWrite=FALSE;a->FlushPhase=FALSE;a->CompletionBytes=0;
 if(flush){
  /* Neither SRB CDB nor SrbExtension is required for these notifications. */
  wire[0]=0x35;wireLength=10;s->DataTransferLength=0;
 }else{
  if(!Allowed(s)){a->Blocked++;Sense(a,s,7,0x27);return TRUE;}
  wireLength=s->CdbLength;Copy(wire,s->Cdb,wireLength);
  if(wire[0]==0x2a || wire[0]==0x8a){
   Copy(a->Dma+0x2000,r->Buffer,s->DataTransferLength);
   a->FlushAfterWrite=(wire[1]&8)!=0;wire[1]&=(UCHAR)~8u;
  }
 }
 a->WireLun=s->Lun;a->WireOpcode=wire[0];a->WireBytes=s->DataTransferLength;
 a->Ticks=0;a->Pending=s;StorPortQuerySystemTime(&a->Deadline);
 a->Deadline.QuadPart+=wire[0]==0x35?100000000:10000000;
 if(!Send(a,wire,wireLength,s->Lun,s->DataTransferLength,FALSE)){
  a->Pending=NULL;Stop(a);s->DataTransferLength=0;Complete(a,s,SRB_STATUS_ERROR);return TRUE;
 }
 /* Avoid a timer round trip for short commands. Bound the busy wait to
  * 50us; slow media/flush commands retain asynchronous completion. Poll
  * performs the identical response, residual and DMA ownership checks. */
 for(i=0;i<50;i++){
  if(!(Rd(a,0x58)&1) || (Rd(a,0x20)&FATAL_IS)){Poll(a);return TRUE;}
  StorPortStallExecution(1);
 }
 StorPortNotification(RequestTimerCall,a,Poll,1000);return TRUE;
}
static HW_INITIALIZE Initialize;
static BOOLEAN Initialize(PVOID extension){
 ADAPTER *a=extension;ULONG i;PUCHAR r;
 a->Stage=2;
 Retain("Initialize HCE HCS IE",Rd(a,0x34),Rd(a,0x30),Rd(a,0x24));
 if(a->Dead || Rd(a,8)!=0x400 || (Rd(a,0x30)&15)!=15 || Rd(a,0x24) ||
    Rd(a,0x58) || Rd(a,0x78) || Rd(a,0x60) || Rd(a,0x80) || !(Rd(a,0x34)&1)){
  Retain("Initialize precondition failed",Rd(a,8),Rd(a,0x58),Rd(a,0x60));return FALSE;
 }
 Zero(a->Dma,0x10000);Wr(a,0x50,a->DmaPa);Wr(a,0x54,0);
 if(Rd(a,0x50)!=a->DmaPa || Rd(a,0x54))return FALSE;
 a->Stage=3;
 if(!Send(a,NULL,0,0,0,TRUE))return FALSE;
 for(i=0;i<10000;i++){if(!Rd(a,0x58) || (Rd(a,0x20)&FATAL_IS))break;StorPortStallExecution(10);}
 Wr(a,0x60,0);r=a->Dma+0x1080;
 if(!TransportDone(a) || r[0]!=0x20 || r[3]!=a->Tag || r[6] || r[8] || r[10] || r[11]){
  Retain("NOP failed",a->LastIs,a->LastOcs,((ULONG)r[0]<<8)|r[6]);Stop(a);return FALSE;
 }
 a->Ready=TRUE;a->Stage=4;Retain("NOP PASS ready",a->DmaPa,Rd(a,0x30),a->Tag);
 StorPortNotification(RequestTimerCall,a,Poll,10000);return TRUE;
}
static HW_FIND_ADAPTER FindAdapter;
static ULONG FindAdapter(PVOID extension,PVOID context,PVOID bus,PCHAR args,PPORT_CONFIGURATION_INFORMATION c,PBOOLEAN again){
 ADAPTER *a=extension;ULONG i,length=0,offset,mode;PVOID raw;STOR_PHYSICAL_ADDRESS pa;PULONG watchdog;
 UNREFERENCED_PARAMETER(context);UNREFERENCED_PARAMETER(bus);UNREFERENCED_PARAMETER(args);*again=FALSE;
 Retain("FindAdapter bus ranges dump",c->AdapterInterfaceType,c->NumberOfAccessRanges,c->DumpMode);
 if(c->DumpMode)return SP_RETURN_NOT_FOUND;
 for(i=0;i<c->NumberOfAccessRanges;i++)if(!(*c->AccessRanges)[i].RangeInMemory ||
   (*c->AccessRanges)[i].RangeStart.QuadPart!=HCI_PA || (*c->AccessRanges)[i].RangeLength!=HCI_SIZE)continue;else break;
 if(i==c->NumberOfAccessRanges){Retain("HCI resource missing",c->NumberOfAccessRanges,0,0);return SP_RETURN_BAD_CONFIG;}
 pa.QuadPart=HCI_PA;a->Reg=StorPortGetDeviceBase(a,c->AdapterInterfaceType,c->SystemIoBusNumber,pa,HCI_SIZE,FALSE);
 if(!a->Reg)return SP_RETURN_BAD_CONFIG;
 // Diagnostic read-only build: arm the already-proven platform watchdog for
 // 30 seconds from driver initialization, not from pressing the boot menu.
 // Require an assigned WDT resource. Never add this timer to write testing.
 for(i=0;i<c->NumberOfAccessRanges;i++)if((*c->AccessRanges)[i].RangeInMemory &&
    (*c->AccessRanges)[i].RangeStart.QuadPart==0x1c00a000 && (*c->AccessRanges)[i].RangeLength==0x1000)break;
 if(i==c->NumberOfAccessRanges){Retain("WDT resource missing",0,0,0);return SP_RETURN_BAD_CONFIG;}
 pa.QuadPart=0x1c00a000;
 watchdog=StorPortGetDeviceBase(a,c->AdapterInterfaceType,c->SystemIoBusNumber,pa,0x1000,FALSE);
 if(!watchdog)return SP_RETURN_BAD_CONFIG;
 mode=StorPortReadRegisterUlong(a,watchdog);
 StorPortWriteRegisterUlong(a,watchdog,0x22000000u|(mode&0x00fffffeu));
 if(StorPortReadRegisterUlong(a,watchdog)&1)return SP_RETURN_BAD_CONFIG;
 a->RecoveryStopAt.QuadPart=0;
 Retain("WDT disabled write qualification",0,mode,StorPortReadRegisterUlong(a,watchdog));
 // SRB payloads are accessed by CPU through a system mapping, then copied
 // to/from our private low DMA arena. They may therefore reside above 4GB.
 // A 32-bit Storport mapping creates a separate bounce buffer; completing
 // a CPU-filled reply can copy that stale buffer over our original reply.
 // This flag avoids I/O remapping while retaining a below-4GB uncached
 // extension (unlike FULL64BIT_SUPPORTED). Never pass SRB addresses to HCI.
 c->Master=TRUE;c->ScatterGather=TRUE;c->Dma64BitAddresses=SCSI_DMA64_MINIPORT_SUPPORTED;
 c->CachesData=DEGAS_UFS_INSTALL_MODE?TRUE:FALSE;
 Retain("Write scope install mode",DEGAS_UFS_INSTALL_MODE,DEGAS_WINDOWS_FIRST,DEGAS_WINDOWS_END);
 Retain("DMA payload64 arena32",c->Dma64BitAddresses,0,0);
 c->MapBuffers=STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;c->MaximumTransferLength=MAX_DATA;c->NumberOfPhysicalBreaks=DEGAS_UFS_PERFORMANCE?(MAX_DATA/4096)+1:16;
 Retain("Transfer limit arena performance",MAX_DATA,DMA_SIZE,DEGAS_UFS_PERFORMANCE);
 c->NumberOfBuses=1;c->MaximumNumberOfTargets=1;c->MaximumNumberOfLogicalUnits=3;
 c->InitiatorBusId[0]=0xff;c->AlignmentMask=3;c->TaggedQueuing=TRUE;
 c->MultipleRequestPerLu=TRUE;c->AutoRequestSense=TRUE;c->SynchronizationModel=StorSynchronizeHalfDuplex;
 c->MaxNumberOfIO=1;c->MaxIOsPerLun=1;c->InitialLunQueueDepth=1;
 Retain("Queue limits adapter lun initial",c->MaxNumberOfIO,c->MaxIOsPerLun,c->InitialLunQueueDepth);
 raw=StorPortGetUncachedExtension(a,c,DMA_SIZE);if(!raw){Retain("DMA allocate failed",DMA_SIZE,0,0);return SP_RETURN_BAD_CONFIG;}
 pa=StorPortGetPhysicalAddress(a,NULL,raw,&length);
 offset=(ULONG)((1024-(pa.QuadPart&1023))&1023);
 if(pa.QuadPart<=0 || (ULONGLONG)pa.QuadPart>=0x100000000ULL ||
    (ULONGLONG)pa.QuadPart+offset+0x2000+MAX_DATA>0x100000000ULL || length<offset+0x2000+MAX_DATA){
  Retain("DMA address rejected",pa.HighPart,pa.LowPart,length);return SP_RETURN_BAD_CONFIG;
 }
 a->Dma=(PUCHAR)raw+offset;a->DmaPa=pa.LowPart+offset;a->Stage=1;
 return SP_RETURN_FOUND;
}
static HW_INTERRUPT Interrupt;
static BOOLEAN Interrupt(PVOID extension){UNREFERENCED_PARAMETER(extension);return FALSE;}
static HW_RESET_BUS ResetBus;
static BOOLEAN ResetBus(PVOID extension,ULONG path){
 ADAPTER *a=extension;PSCSI_REQUEST_BLOCK s=a->Pending;UNREFERENCED_PARAMETER(path);
 Stop(a);a->Pending=NULL;
 if(s){s->DataTransferLength=0;Complete(a,s,SRB_STATUS_BUS_RESET);}return FALSE;
}
static HW_ADAPTER_CONTROL Control;
static SCSI_ADAPTER_CONTROL_STATUS Control(PVOID extension,SCSI_ADAPTER_CONTROL_TYPE type,PVOID parameters){
 ADAPTER *a=extension;
 if(type==ScsiQuerySupportedControlTypes){
  PSCSI_SUPPORTED_CONTROL_TYPE_LIST l=parameters;
  if(l->MaxControlType>ScsiStopAdapter)l->SupportedTypeList[ScsiStopAdapter]=TRUE;
  if(l->MaxControlType>ScsiRestartAdapter)l->SupportedTypeList[ScsiRestartAdapter]=TRUE;
  return ScsiAdapterControlSuccess;
 }
 if(type==ScsiStopAdapter)return Stop(a)?ScsiAdapterControlSuccess:ScsiAdapterControlUnsuccessful;
 /* Restart requires a fresh UEFI handoff, never fake a successful resume. */
 return ScsiAdapterControlUnsuccessful;
}
ULONG DriverEntry(PVOID driver,PVOID registry){
 HW_INITIALIZATION_DATA h;ULONG status;PDRIVER_OBJECT object=driver;
 RetainedInit();Zero(&h,sizeof h);h.HwInitializationDataSize=sizeof h;
 // The PDO is enumerated by ACPI; Storport matches initialization records by
 // bus type during AddDevice. Internal does not describe this ACPI device.
 h.AdapterInterfaceType=ACPIBus;h.HwFindAdapter=FindAdapter;h.HwInitialize=Initialize;
 h.HwStartIo=StartIo;h.HwBuildIo=BuildIo;h.HwInterrupt=Interrupt;h.HwResetBus=ResetBus;h.HwAdapterControl=Control;
 h.DeviceExtensionSize=sizeof(ADAPTER);h.SrbExtensionSize=sizeof(REQUEST);h.NumberOfAccessRanges=2;
 h.MapBuffers=STOR_MAP_ALL_BUFFERS_INCLUDING_READ_WRITE;h.AutoRequestSense=TRUE;
 h.NeedPhysicalAddresses=TRUE;h.TaggedQueuing=TRUE;h.MultipleRequestPerLu=TRUE;
 h.AddressTypeFlags=ADDRESS_TYPE_FLAG_BTL8;h.SrbTypeFlags=SRB_TYPE_FLAG_SCSI_REQUEST_BLOCK;
 status=StorPortInitialize(driver,registry,&h,NULL);Retain("StorPortInitialize returned",status,sizeof h,0);
 if((LONG)status<0)RetainedClose();else{PortUnload=object->DriverUnload;object->DriverUnload=DiagnosticUnload;}
 return status;
}
