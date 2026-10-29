/* Mock environment for verbatim-extracted miniport functions. No Windows ABI
 * claim: integer widths match WDK, but this SRB exposes only tested fields. */
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdatomic.h>
#define RtlCopyMemory memcpy

typedef void VOID;
typedef void *PVOID;
typedef uint8_t UCHAR,BOOLEAN,*PUCHAR;
typedef uint32_t ULONG,*PULONG;
typedef uint64_t ULONGLONG;
typedef union {int64_t QuadPart;struct {uint32_t LowPart;int32_t HighPart;};} LARGE_INTEGER;
typedef struct {
 UCHAR Function,PathId,TargetId,Lun,CdbLength,ScsiStatus,SrbStatus,SenseInfoBufferLength;
 ULONG DataTransferLength,SrbFlags;
 UCHAR Cdb[16];PVOID SrbExtension,DataBuffer,SenseInfoBuffer;
} SCSI_REQUEST_BLOCK,*PSCSI_REQUEST_BLOCK;
#define TRUE 1
#define FALSE 0
#define SRB_FLAGS_DATA_IN 0x40
#define SRB_FLAGS_DATA_OUT 0x80
#define SRB_FUNCTION_EXECUTE_SCSI 0x00
#define SRB_FUNCTION_SHUTDOWN 0x07
#define SRB_FUNCTION_FLUSH 0x08
#define SRB_STATUS_SUCCESS 0x01
#define SRB_STATUS_ERROR 0x04
#define SRB_STATUS_BUSY 0x05
#define SRB_STATUS_INVALID_REQUEST 0x06
#define SRB_STATUS_NO_DEVICE 0x08
#define SRB_STATUS_TIMEOUT 0x09
#define SRB_STATUS_BUS_RESET 0x0e
#define SRB_STATUS_DATA_OVERRUN 0x12
#define SRB_STATUS_AUTOSENSE_VALID 0x80
#define SCSISTAT_GOOD 0
#define SCSISTAT_CHECK_CONDITION 2
#define STOR_STATUS_SUCCESS 0
#define STOR_STATUS_INSUFFICIENT_RESOURCES 5
enum {RequestComplete,RequestTimerCall};
static unsigned Checks;
#define CHECK(x) do {Checks++;if(!(x)){fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)

static ULONG Registers[0x3000/4];
static struct {ULONG offset,value;} Writes[128];
static unsigned WriteCount,Barriers,Completions,Timers,Maps,Retains,Cancellations;
static ULONG LastTimerUs,RetainValues[3];
static ULONG QuiesceValues[3];
static const char *LastRetain;
static BOOLEAN RejectRun;
static int64_t MockTime=1234567890;
static PSCSI_REQUEST_BLOCK LastCompleted;
static VOID Poll(PVOID extension);
static ULONG Offset(PULONG address){
 uintptr_t offset=(uintptr_t)address-(uintptr_t)Registers;
 CHECK(offset<sizeof Registers && !(offset&3));return (ULONG)offset;
}
static ULONG StorPortReadRegisterUlong(PVOID extension,PULONG address){
 (void)extension;return Registers[Offset(address)/4];
}
static VOID StorPortWriteRegisterUlong(PVOID extension,PULONG address,ULONG value){
 ULONG offset=Offset(address);(void)extension;
 CHECK(WriteCount<sizeof Writes/sizeof Writes[0]);
 Writes[WriteCount].offset=offset;Writes[WriteCount++].value=value;
 if(offset==0x20)Registers[offset/4]&=~value;
 else Registers[offset/4]=(offset==0x60 && RejectRun)?0:value;
}
static VOID KeMemoryBarrier(void){Barriers++;atomic_thread_fence(memory_order_seq_cst);}
static unsigned StallUs;
static void (*StallHook)(void);
static VOID StorPortStallExecution(ULONG us){CHECK(us<=1000);StallUs+=us;if(StallHook)StallHook();}
static VOID StorPortQuerySystemTime(LARGE_INTEGER *time){time->QuadPart=MockTime;}
static void Retain(const char *event,ULONG x,ULONG y,ULONG z){
 Retains++;LastRetain=event;RetainValues[0]=x;RetainValues[1]=y;RetainValues[2]=z;
 if(!strcmp(event,"pre-reset quiesce")){QuiesceValues[0]=x;QuiesceValues[1]=y;QuiesceValues[2]=z;}
}
static VOID StorPortNotification(int type,PVOID extension,...){
 va_list args;(void)extension;va_start(args,extension);
 if(type==RequestComplete){Completions++;LastCompleted=va_arg(args,PSCSI_REQUEST_BLOCK);}
 else {
  void (*callback)(PVOID)=va_arg(args,void (*)(PVOID));ULONG us=va_arg(args,ULONG);
  CHECK(type==RequestTimerCall && callback==Poll && (us==0 || us==1000 || us==10000));
  if(us)Timers++;else Cancellations++;LastTimerUs=us;
 }
 va_end(args);
}
/* Mapping telemetry has no request side effects; Windows PA/SG API is tested
 * on-device, not reproduced by this logical request harness. */
static void TraceMapping(PVOID extension,PSCSI_REQUEST_BLOCK s,PVOID buffer){
 (void)extension;(void)s;(void)buffer;
}
#include "../degas/ufs/write_policy.h"
#include "../degas/ufs/install_policy.h"
#include "degasufs-under-test.inc"

static ADAPTER Adapter;
static REQUEST Request;
static UCHAR *Arena,*Buffer,*SenseBuffer;
static void Reset(void){
 StallUs=0;StallHook=NULL;
 memset(&Adapter,0,sizeof Adapter);memset(&Request,0,sizeof Request);
 memset(Registers,0,sizeof Registers);memset(Arena,0xa5,DMA_SIZE);
 memset(Buffer,0x5a,MAX_DATA+32);memset(SenseBuffer,0x6b,50);
 Adapter.Reg=(PUCHAR)Registers;Adapter.Dma=Arena;Adapter.DmaPa=0x60000000;Adapter.Ready=TRUE;
 Registers[0x30/4]=15;Registers[0x34/4]=1;
 WriteCount=Barriers=Completions=Timers=Maps=Retains=Cancellations=0;LastCompleted=NULL;RejectRun=FALSE;
 LastTimerUs=0;LastRetain=NULL;memset(RetainValues,0,sizeof RetainValues);
 Request.Buffer=Buffer+16;Request.MapStatus=STOR_STATUS_SUCCESS;
}
static void Encode32(UCHAR *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(UCHAR)(n>>(24-i*8));}
static void Encode64(UCHAR *p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(UCHAR)(n>>(56-i*8));}
static SCSI_REQUEST_BLOCK Command(UCHAR opcode,UCHAR length,ULONG bytes,UCHAR lun){
 SCSI_REQUEST_BLOCK s={0};s.Cdb[0]=opcode;s.CdbLength=length;s.DataTransferLength=bytes;s.Lun=lun;
 s.SrbFlags=bytes?SRB_FLAGS_DATA_IN:0;s.SrbExtension=&Request;s.DataBuffer=Buffer+16;
 s.SenseInfoBuffer=SenseBuffer+16;s.SenseInfoBufferLength=18;return s;
}
static SCSI_REQUEST_BLOCK Read(UCHAR opcode,UCHAR lun,uint64_t lba,uint32_t blocks){
 SCSI_REQUEST_BLOCK s=Command(opcode,opcode==0x28?10:16,blocks*4096u,lun);
 if(opcode==0x28){Encode32(s.Cdb+2,(uint32_t)lba);s.Cdb[7]=(UCHAR)(blocks>>8);s.Cdb[8]=(UCHAR)blocks;}
 else {Encode64(s.Cdb+2,lba);Encode32(s.Cdb+10,blocks);}return s;
}
static void Guards(void){
 for(unsigned i=0;i<16;i++){CHECK(Buffer[i]==0x5a);CHECK(Buffer[MAX_DATA+16+i]==0x5a);CHECK(SenseBuffer[i]==0x6b);CHECK(SenseBuffer[34+i]==0x6b);}
}
static void Whitelist(void){
 static const struct {UCHAR opcode,length;ULONG bytes;} allowed[]={
  {0,6,0},{3,6,18},{0x12,6,255},{0x1a,6,255},{0x25,10,8},{0x5a,10,255},{0x9e,16,32},{0x28,10,4096},{0x88,16,4096}};
 SCSI_REQUEST_BLOCK s;
 for(unsigned lun=0;lun<3;lun++)for(unsigned i=0;i<sizeof allowed/sizeof allowed[0];i++){
  s=Command(allowed[i].opcode,allowed[i].length,allowed[i].bytes,(UCHAR)lun);
  if(s.Cdb[0]==0x9e){s.Cdb[1]=0x10;Encode32(s.Cdb+10,32);}
  if(s.Cdb[0]==0x28)s=Read(0x28,(UCHAR)lun,0,1);
  if(s.Cdb[0]==0x88)s=Read(0x88,(UCHAR)lun,0,1);
  CHECK(Allowed(&s));
  for(unsigned length=0;length<256;length++) {s.CdbLength=(UCHAR)length;CHECK(Allowed(&s)==(length==allowed[i].length));}
  s.CdbLength=allowed[i].length;s.SrbFlags|=SRB_FLAGS_DATA_OUT;CHECK(!Allowed(&s));
  s.SrbFlags=SRB_FLAGS_DATA_OUT;CHECK(!Allowed(&s));s.SrbFlags=SRB_FLAGS_DATA_IN;
  s.DataTransferLength=MAX_DATA+1;CHECK(!Allowed(&s));
 }
 /* Exhaust every opcode outside the explicit read-only set, including all
  * WRITE/UNMAP/FORMAT/SECURITY/MODE SELECT/vendor and pass-through opcodes. */
 for(unsigned opcode=0;opcode<256;opcode++){
  BOOLEAN known=FALSE;for(unsigned i=0;i<sizeof allowed/sizeof allowed[0];i++)if(opcode==allowed[i].opcode)known=TRUE;
  if(known)continue;
  for(unsigned length=6;length<=16;length+=2){s=Command((UCHAR)opcode,(UCHAR)length,4096,2);CHECK(!Allowed(&s));}
 }
 for(unsigned lun=0;lun<3;lun++)for(unsigned op=0;op<2;op++){
  uint64_t limit=lun==2?64577536:1024;UCHAR opcode=op?0x88:0x28;
  s=Read(opcode,(UCHAR)lun,limit-1,1);CHECK(Allowed(&s));
  s=Read(opcode,(UCHAR)lun,limit-14,14);CHECK(Allowed(&s));
  s=Read(opcode,(UCHAR)lun,limit,1);CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,limit-1,2);CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,0,0);CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,0,MAX_DATA/4096+1);CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,0,1);s.DataTransferLength=4095;CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,0,1);s.DataTransferLength=8192;CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,op?UINT64_MAX:UINT32_MAX,1);CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,op?UINT64_MAX-6:UINT32_MAX-6,14);CHECK(!Allowed(&s));
  s=Read(opcode,(UCHAR)lun,1,op?UINT32_MAX:UINT16_MAX);CHECK(!Allowed(&s));
 }
 s=Command(0x9e,16,32,2);s.Cdb[1]=0x10;Encode32(s.Cdb+10,33);CHECK(!Allowed(&s));
 Encode32(s.Cdb+10,32);s.Cdb[1]=0x11;CHECK(!Allowed(&s));
 s=Command(0,6,1,2);CHECK(!Allowed(&s));s=Command(0x25,10,7,2);CHECK(!Allowed(&s));
}
static void Descriptors(void){
 const ULONG sizes[]={0,1,2,3,4,18,36,255,256,MAX_DATA-1,MAX_DATA};
 for(unsigned near4g=0;near4g<2;near4g++)for(unsigned n=0;n<sizeof sizes/sizeof sizes[0];n++){
  Reset();if(near4g)Adapter.DmaPa=0xffff0000u;
  ULONG bytes=sizes[n];UCHAR cdb[16];for(unsigned i=0;i<16;i++)cdb[i]=(UCHAR)(i+1);
  Adapter.Tag=255;Adapter.Submitted=7;Registers[0x20/4]=0x1234;
  CHECK(Send(&Adapter,cdb,16,2,bytes,FALSE));
  ULONG *t=(PULONG)Arena,*p=(PULONG)(Arena+0x1100);PUCHAR q=Arena+0x1000;
  CHECK(t[0]==(bytes?0x15000000u:0x11000000u));CHECK(t[1]==0 && t[2]==15 && t[3]==0);
  CHECK(t[4]==Adapter.DmaPa+0x1000 && t[5]==0);
  ULONG responseOffset=(t[6]>>16)*4,responseLength=(t[6]&0xffff)*4;
  CHECK(responseOffset==128 && responseLength==64);
  CHECK(0x1000+responseOffset+responseLength<=0x1100);
  CHECK(q[0]==1 && q[1]==(bytes?0x40:0) && q[2]==2 && q[3]==1);
  CHECK(!memcmp(q+16,cdb,16));CHECK(Be32(q+12)==bytes);
  CHECK(Adapter.Tag==1 && Adapter.Submitted==8 && Barriers==2);
  CHECK(WriteCount==3 && Writes[0].offset==0x20 && Writes[0].value==0x1234);
  CHECK(Writes[1].offset==0x60 && Writes[1].value==1);
  CHECK(Writes[2].offset==0x58 && Writes[2].value==1);
  if(bytes){
   ULONG length=p[3]+1;CHECK((p[3]&3)==3);CHECK(length>=bytes && length-bytes<4);
   CHECK(length<=MAX_DATA && p[0]==Adapter.DmaPa+0x2000 && !p[1] && !p[2]);
   CHECK((t[7]>>16)*4==0x100 && (t[7]&0xffff)==1);
   CHECK((uint64_t)p[0]+length<=(uint64_t)Adapter.DmaPa+DMA_SIZE-0x1000);
  }else CHECK(!t[7] && !p[0] && !p[1] && !p[2] && !p[3]);
  /* Send may write UTRD[0..31] and UCD[1000..111f] only. */
  for(unsigned i=0;i<DMA_SIZE;i++)if(i>=32 && (i<0x1000 || i>=0x1120))CHECK(Arena[i]==0xa5);
 }
 Reset();CHECK(Send(&Adapter,NULL,0,0,0,TRUE));CHECK(Arena[0x1000]==0 && Arena[0x1001]==0);
 for(unsigned i=0;i<16;i++)CHECK(Arena[0x1010+i]==0);
 for(unsigned fault=0;fault<5;fault++){
  Reset();if(fault==0)Adapter.Dead=TRUE;if(fault==1)Registers[0x30/4]=7;
  if(fault==2)Registers[0x58/4]=1;if(fault==3)Registers[0x34/4]=0;if(fault==4)RejectRun=TRUE;
  UCHAR cdb[16]={0};CHECK(!Send(&Adapter,cdb,6,2,0,FALSE));CHECK(Adapter.Submitted==0);
  for(unsigned i=0;i<WriteCount;i++)CHECK(Writes[i].offset!=0x58);
  if(fault<4){CHECK(!WriteCount);for(unsigned i=0;i<DMA_SIZE;i++)CHECK(Arena[i]==0xa5);}
 }
}
static void CallerGates(void){
 SCSI_REQUEST_BLOCK s;
 for(unsigned lun=3;lun<256;lun++){
  Reset();s=Read(0x28,(UCHAR)lun,0,1);CHECK(StartIo(&Adapter,&s));
  CHECK(s.SrbStatus==SRB_STATUS_NO_DEVICE && Completions==1 && !WriteCount && !Timers);
 }
 for(unsigned path=0;path<2;path++){
  Reset();s=Read(0x28,2,0,1);if(path)s.PathId=1;else s.TargetId=1;
  CHECK(StartIo(&Adapter,&s));CHECK(s.SrbStatus==SRB_STATUS_NO_DEVICE && !WriteCount);
 }
 for(unsigned opcode=0;opcode<256;opcode++){
  if(opcode!=0x2a && opcode!=0x8a && opcode!=0x42 && opcode!=0xb5 && opcode<0xc0)continue;
  Reset();s=Command((UCHAR)opcode,16,4096,2);CHECK(StartIo(&Adapter,&s));
  CHECK((s.SrbStatus&SRB_STATUS_AUTOSENSE_VALID) && s.ScsiStatus==2 && !WriteCount && !Timers);Guards();
 }
 Reset();s=Read(0x28,2,0,1);s.SrbFlags|=SRB_FLAGS_DATA_OUT;CHECK(StartIo(&Adapter,&s));CHECK(!WriteCount);
 Reset();s=Read(0x28,2,0,1);Request.Buffer=NULL;CHECK(StartIo(&Adapter,&s));CHECK(!WriteCount && s.SrbStatus==SRB_STATUS_ERROR);
 Reset();s=Read(0x28,2,0,1);CHECK(BuildIo(&Adapter,&s));CHECK(Maps==0 && Request.Buffer==s.DataBuffer);
 /* Missing Storport mapping must fail closed, without an original-MDL fallback. */
 Reset();s=Read(0x28,2,0,1);s.DataBuffer=NULL;CHECK(BuildIo(&Adapter,&s));
 CHECK(Request.MapStatus!=STOR_STATUS_SUCCESS && !Request.Buffer);
 CHECK(StartIo(&Adapter,&s));CHECK(s.SrbStatus==SRB_STATUS_ERROR && !WriteCount);
 /* Model the remapped transfer separately from the caller's original buffer.
  * On completion, Storport copies mapped bytes back to the original. */
 {
  UCHAR mapped[64]={0},original[64]={0};
  Reset();s=Command(0xa0,12,64,0);Encode32(s.Cdb+6,64);s.DataBuffer=mapped;
  CHECK(BuildIo(&Adapter,&s));CHECK(Request.Buffer==mapped);
  CHECK(StartIo(&Adapter,&s));CHECK(s.DataTransferLength==32);
  CHECK(Be32(original)==0 && Be32(mapped)==24);
  memcpy(original,mapped,s.DataTransferLength);
  CHECK(Be32(original)==24 && original[17]==1 && original[25]==2);
 }
 /* Reset/power/service requests must not inspect an invalid data extension. */
 for(unsigned function=1;function<256;function++){
  Reset();s=Command(0,0,UINT32_MAX,0);s.Function=(UCHAR)function;s.SrbExtension=(PVOID)(uintptr_t)1;
  CHECK(BuildIo(&Adapter,&s));CHECK(!Maps && !WriteCount);
 }
 Reset();s=Read(0x28,2,0,1);s.SrbExtension=NULL;CHECK(BuildIo(&Adapter,&s));CHECK(StartIo(&Adapter,&s));CHECK(!WriteCount);
 Reset();s=Read(0x28,2,0,1);Adapter.Pending=&s;CHECK(StartIo(&Adapter,&s));CHECK(s.SrbStatus==SRB_STATUS_BUSY && !WriteCount);
 Reset();s=Command(0xa0,12,32,2);Encode32(s.Cdb+6,32);CHECK(StartIo(&Adapter,&s));
 CHECK(s.SrbStatus==SRB_STATUS_SUCCESS && !WriteCount && s.DataTransferLength==32);
 CHECK(Be32(Buffer+16)==24 && Buffer[16+17]==1 && Buffer[16+25]==2);Guards();
 /* Hardware capture: REPORT LUNS requests 64 bytes, reply has only 32. */
 Reset();s=Command(0xa0,12,64,0);Encode32(s.Cdb+6,64);CHECK(StartIo(&Adapter,&s));
 CHECK(s.SrbStatus==SRB_STATUS_DATA_OVERRUN && s.ScsiStatus==0 && s.DataTransferLength==32 && !WriteCount);
 CHECK(Be32(Buffer+16)==24 && Buffer[16+17]==1 && Buffer[16+25]==2);Guards();
 for(unsigned length=0;length<4;length++){
  Reset();s=Command(0xa0,12,length,0);Encode32(s.Cdb+6,length);CHECK(StartIo(&Adapter,&s));
  CHECK(s.DataTransferLength==length && !WriteCount);Guards();
 }
}
static void BeginRead(SCSI_REQUEST_BLOCK *s){
 Reset();*s=Read(0x28,2,0,1);CHECK(StartIo(&Adapter,s));
 CHECK(Adapter.Pending==s && !Completions && Timers==1);
 memset(Arena+0x2000,0x3c,4096);Registers[0x58/4]=0;((PULONG)Arena)[2]=0;
 UCHAR *r=Arena+0x1080;r[0]=0x21;r[2]=2;r[3]=Adapter.Tag;
}
static void Responses(void){
 SCSI_REQUEST_BLOCK s;UCHAR *r=Arena+0x1080;
 BeginRead(&s);Poll(&Adapter);CHECK(Completions==1 && LastCompleted==&s && s.SrbStatus==SRB_STATUS_SUCCESS);
 CHECK(s.DataTransferLength==4096 && !Adapter.Pending);for(unsigned i=0;i<4096;i++)CHECK(Buffer[16+i]==0x3c);Guards();
 Poll(&Adapter);CHECK(Completions==1);
 for(unsigned bad=0;bad<10;bad++){
  BeginRead(&s);
  switch(bad){case 0:r[0]=0x20;break;case 1:r[2]=1;break;case 2:r[3]++;break;case 3:r[8]=1;break;
   case 4:((PULONG)Arena)[2]=1;break;case 5:Registers[0x20/4]=FATAL_IS;break;
   case 6:r[1]=0x40;break;case 7:Encode32(r+12,4097);break;
   case 8:Encode32(r+12,1);break;case 9:r[6]=2;break;}
  Poll(&Adapter);CHECK(Completions==1 && s.SrbStatus==SRB_STATUS_ERROR && !s.DataTransferLength && Adapter.Dead);
  for(unsigned i=0;i<4096;i++)CHECK(Buffer[16+i]==0x5a);Guards();
 }
 BeginRead(&s);r[1]=0x20;Encode32(r+12,4096-128);Poll(&Adapter);
 CHECK(s.DataTransferLength==128 && s.SrbStatus==SRB_STATUS_DATA_OVERRUN);
 for(unsigned i=0;i<4096;i++)CHECK(Buffer[16+i]==(i<128?0x3c:0x5a));Guards();
 for(unsigned bad=0;bad<4;bad++){
  BeginRead(&s);if(bad==0)r[6]=1;if(bad==1)r[11]=1;if(bad==2){r[10]=255;r[11]=255;}if(bad==3)r[7]=8;
  Poll(&Adapter);CHECK(s.SrbStatus==SRB_STATUS_ERROR && !s.DataTransferLength);Guards();
 }
 for(unsigned senseLength=0;senseLength<=64;senseLength++){
  BeginRead(&s);r[7]=2;r[32]=0;r[33]=(UCHAR)senseLength;r[10]=0;r[11]=(UCHAR)(senseLength+2);
  for(unsigned i=0;i<18;i++)r[34+i]=(UCHAR)(0x70+i);
  Poll(&Adapter);CHECK(Completions==1 && !s.DataTransferLength);
  CHECK((s.SrbStatus&SRB_STATUS_AUTOSENSE_VALID)==((senseLength>0 && senseLength<=18)?SRB_STATUS_AUTOSENSE_VALID:0));
  for(unsigned i=0;i<18;i++)CHECK(SenseBuffer[16+i]==(senseLength>0 && senseLength<=18 && i<senseLength?(UCHAR)(0x70+i):0x6b));Guards();
 }
 BeginRead(&s);r[7]=2;r[33]=18;r[11]=20;s.SenseInfoBufferLength=4;Poll(&Adapter);
 for(unsigned i=4;i<18;i++)CHECK(SenseBuffer[16+i]==0x6b);Guards();
 BeginRead(&s);Registers[0x58/4]=1;MockTime=Adapter.Deadline.QuadPart-1;Poll(&Adapter);CHECK(!Completions && Timers==2);
 MockTime=Adapter.Deadline.QuadPart;Poll(&Adapter);CHECK(s.SrbStatus==SRB_STATUS_TIMEOUT && Completions==1 && Adapter.Dead && !Adapter.Pending);
 Poll(&Adapter);CHECK(Completions==1);
}
static void Recovery(void){
 SCSI_REQUEST_BLOCK s;
 /* Idle ticks must continue without a pending SRB until the 27-second stop. */
 Reset();Adapter.RecoveryStopAt.QuadPart=MockTime+270000000;
 Poll(&Adapter);CHECK(!Completions && Timers==1 && LastTimerUs==10000 && !WriteCount && !Retains);
 MockTime=Adapter.RecoveryStopAt.QuadPart-1;Poll(&Adapter);CHECK(Timers==2 && !Adapter.Dead && !WriteCount);
 MockTime++;Poll(&Adapter);CHECK(Adapter.Dead && !Adapter.Ready && !Adapter.Pending);
 CHECK(!Completions && Timers==2 && Retains==2 && !strcmp(LastRetain,"pre-reset requests submitted blocked"));
 CHECK(WriteCount==4 && Writes[0].offset==0x24 && Writes[1].offset==0x60 && Writes[2].offset==0x80 && Writes[3].offset==0x34);
 CHECK(!Registers[0x34/4] && !Registers[0x60/4] && !Registers[0x80/4]);
 CHECK(Cancellations==1);
 /* A live request is canceled once, even if the UFS doorbell is still set. */
 BeginRead(&s);Adapter.RecoveryStopAt.QuadPart=MockTime;Registers[0x58/4]=1;
 Adapter.Completed=9;Adapter.Timeouts=2;Poll(&Adapter);
 CHECK(Completions==1 && s.SrbStatus==SRB_STATUS_BUS_RESET && !s.DataTransferLength && !Adapter.Pending && Adapter.Dead);
 CHECK(QuiesceValues[0]==9 && QuiesceValues[1]==2 && QuiesceValues[2]==1);
 CHECK(Timers==1 && Cancellations==1);unsigned stoppedWrites=WriteCount;
 Poll(&Adapter);CHECK(Completions==1 && !Adapter.Pending && Timers==1);
 CHECK(WriteCount==stoppedWrites && Cancellations==1);
 SCSI_REQUEST_BLOCK next=Read(0x28,2,0,1);CHECK(StartIo(&Adapter,&next));
 CHECK(next.SrbStatus==SRB_STATUS_NO_DEVICE && !Adapter.Pending && Timers==1);
 /* Completion polling can replace the idle tick with a 1ms request tick. */
 BeginRead(&s);Adapter.RecoveryStopAt.QuadPart=MockTime+270000000;Registers[0x58/4]=1;
 Poll(&Adapter);CHECK(!Completions && Timers==3 && LastTimerUs==1000 && Adapter.Pending==&s);
 Registers[0x58/4]=0;Poll(&Adapter);CHECK(Completions==1 && Timers==4 && LastTimerUs==10000);
 CHECK(s.SrbStatus==SRB_STATUS_SUCCESS && Adapter.RecoveryStopAt.QuadPart>MockTime);Guards();
 /* A failed command is already quiesced; its previously queued idle tick
  * must not create new requests or rearm itself once the adapter is dead. */
 BeginRead(&s);Adapter.RecoveryStopAt.QuadPart=MockTime+270000000;Registers[0x58/4]=1;MockTime=Adapter.Deadline.QuadPart;
 Poll(&Adapter);CHECK(Completions==1 && s.SrbStatus==SRB_STATUS_TIMEOUT && Adapter.Dead);
 unsigned timers=Timers;Poll(&Adapter);CHECK(Timers==timers && Completions==1);
 Reset();Adapter.Dead=TRUE;Adapter.RecoveryStopAt.QuadPart=MockTime+270000000;
 Poll(&Adapter);CHECK(!Timers && !WriteCount && !Completions);
 /* Model released MMIO after ScsiStopAdapter: a late callback must return
  * before deriving or dereferencing any HCI address, even after deadline. */
 Reset();Adapter.RecoveryStopAt.QuadPart=MockTime;CHECK(Stop(&Adapter));CHECK(Cancellations==1);
 Adapter.Reg=NULL;stoppedWrites=WriteCount;Poll(&Adapter);
 CHECK(WriteCount==stoppedWrites && !Completions && !Timers && !Retains);
}
static void WriteQualification(void){
 SCSI_REQUEST_BLOCK s;
#if !DEGAS_UFS_INSTALL_MODE
 for(unsigned lun=0;lun<4;lun++)for(unsigned blocks=0;blocks<4;blocks++)for(int delta=-2;delta<=2;delta++){
  Reset();s=Read(0x28,(UCHAR)lun,DEGAS_TEST_LBA+delta,blocks);s.Cdb[0]=0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;
  CHECK(Allowed(&s)==(lun==2 && blocks>=1 && delta>=0));
 }
 for(unsigned blocks=1;blocks<=15;blocks++)for(int tail=-16;tail<=2;tail++){
  Reset();s=Read(0x28,2,DEGAS_TEST_LBA+DEGAS_TEST_BLOCKS+tail,blocks);s.Cdb[0]=0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;
  CHECK(Allowed(&s)==(blocks<=MAX_DATA/4096 && tail<0 && blocks<=(unsigned)-tail));
 }
#endif
 Reset();s=Read(0x28,2,DEGAS_TEST_LBA,1);s.Cdb[0]=0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;
 CHECK(BuildIo(&Adapter,&s));CHECK(StartIo(&Adapter,&s));
 CHECK(((PULONG)Arena)[0]==0x13000000u && Arena[0x1001]==0x20);
 CHECK(!memcmp(Arena+0x2000,Buffer+16,4096));
 memset(Arena+0x2000,0x99,4096);Registers[0x58/4]=0;((PULONG)Arena)[2]=0;
 PUCHAR r=Arena+0x1080;r[0]=0x21;r[2]=2;r[3]=Adapter.Tag;
 Poll(&Adapter);CHECK(s.SrbStatus==SRB_STATUS_SUCCESS && Buffer[16]==0x5a);
 Reset();s=Command(0x35,10,0,2);CHECK(Allowed(&s));CHECK(StartIo(&Adapter,&s));
 CHECK(((PULONG)Arena)[0]==0x11000000u && Arena[0x1001]==0);
 s.Cdb[1]=2;CHECK(!Allowed(&s));s.Cdb[1]=0;s.Lun=0;CHECK(!Allowed(&s));
}
static void HardwareGood(void){
 PUCHAR r=Arena+0x1080;memset(r,0,64);r[0]=0x21;r[2]=Adapter.WireLun;r[3]=Adapter.Tag;
 ((PULONG)Arena)[2]=0;Registers[0x58/4]=0;
}
static void FlushRequests(void){
 for(unsigned function=SRB_FUNCTION_SHUTDOWN;function<=SRB_FUNCTION_FLUSH;function++){
  Reset();SCSI_REQUEST_BLOCK s=Command(0xff,255,UINT32_MAX,2);
  s.Function=(UCHAR)function;s.SrbExtension=(PVOID)(uintptr_t)1;s.SenseInfoBuffer=(PVOID)(uintptr_t)1;
  CHECK(StartIo(&Adapter,&s));CHECK(Adapter.Pending==&s && !Completions);
  CHECK(Adapter.WireBytes==0 && Arena[0x1010]==0x35 && Adapter.WireOpcode==0x35);
  HardwareGood();Poll(&Adapter);CHECK(Completions==1 && s.SrbStatus==SRB_STATUS_SUCCESS);
  Reset();s=Command(0xff,255,UINT32_MAX,2);s.Function=(UCHAR)function;s.SrbExtension=(PVOID)(uintptr_t)1;
  CHECK(StartIo(&Adapter,&s));HardwareGood();Arena[0x1087]=2;Arena[0x10a1]=18;Arena[0x108b]=20;
  s.SenseInfoBuffer=(PVOID)(uintptr_t)1;Poll(&Adapter);CHECK(s.SrbStatus==SRB_STATUS_ERROR);
  Reset();s=Command(0xff,255,UINT32_MAX,2);s.Function=(UCHAR)function;
  CHECK(StartIo(&Adapter,&s));MockTime=Adapter.Deadline.QuadPart;Poll(&Adapter);
  CHECK(s.SrbStatus==SRB_STATUS_TIMEOUT && Adapter.Dead && Completions==1);
 }
}
static void InstallWrites(void){
#if DEGAS_UFS_INSTALL_MODE
 const uint64_t starts[]={DEGAS_WINDOWS_FIRST,DEGAS_ESP_FIRST};
 const uint64_t ends[]={DEGAS_WINDOWS_END,DEGAS_ESP_END};
 SCSI_REQUEST_BLOCK s;
 for(unsigned part=0;part<2;part++)for(unsigned op=0;op<2;op++){
  for(int delta=-1;delta<=1;delta++){
   Reset();s=Read(op?0x88:0x28,2,starts[part]+delta,1);s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;
   CHECK(Allowed(&s)==(delta>=0));
   s=Read(op?0x88:0x28,2,ends[part]+delta,1);s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;
   CHECK(Allowed(&s)==(delta<0));
  }
  s=Read(op?0x88:0x28,2,ends[part]-1,2);s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;CHECK(!Allowed(&s));
  s=Read(op?0x88:0x28,2,starts[part],14);s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;CHECK(Allowed(&s));
  s.Cdb[1]=8;CHECK(Allowed(&s));s.Cdb[1]=16;CHECK(!Allowed(&s));
  for(unsigned fault=0;fault<4;fault++){
   Reset();s=Read(op?0x88:0x28,2,starts[part],1);s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;s.Cdb[1]=8;
   CHECK(StartIo(&Adapter,&s));CHECK(Adapter.FlushAfterWrite && !Adapter.FlushPhase && !Completions);
   CHECK(Arena[0x1011]==0 && s.Cdb[1]==8); /* original CDB unchanged */
   HardwareGood();Poll(&Adapter);CHECK(Adapter.Pending==&s && Adapter.FlushPhase && !Completions);
   CHECK(Arena[0x1010]==0x35 && Adapter.WireBytes==0);
   HardwareGood();
   if(fault==1)Arena[0x1087]=2;
   if(fault==2){Registers[0x58/4]=1;MockTime=Adapter.Deadline.QuadPart;}
   if(fault==3)Arena[0x1080]=0;
   Poll(&Adapter);CHECK(Completions==1 && !Adapter.Pending);
   CHECK((s.SrbStatus==SRB_STATUS_SUCCESS)==(fault==0));
   CHECK(s.DataTransferLength==(fault==0?4096u:0u));Guards();
  }
 }
 CHECK(!DegasUfsWritableRange(2,0,1));CHECK(!DegasUfsWritableRange(2,64577535,1));
 CHECK(!DegasUfsWritableRange(2,DEGAS_ESP_END,4096));CHECK(!DegasUfsWritableRange(0,DEGAS_WINDOWS_FIRST,1));
 CHECK(!DegasUfsWritableRange(2,UINT64_MAX,2));
#endif
}
static void FastResponse(void){
 if(StallUs!=3)return;
 PUCHAR r=Arena+0x1080;memset(r,0,64);r[0]=0x21;r[2]=2;r[3]=Adapter.Tag;
 ((PULONG)Arena)[2]=0;Registers[0x58/4]=0;memset(Arena+0x2000,0x37,4096);
}
static void LargestTransfer(void){
 SCSI_REQUEST_BLOCK s;
 for(unsigned op=0;op<2;op++){
  Reset();s=Read(op?0x88:0x28,2,1234,MAX_DATA/4096);
  CHECK(Allowed(&s));CHECK(StartIo(&Adapter,&s));
  CHECK(((PULONG)(Arena+0x1100))[3]==MAX_DATA-1);
  CHECK(((PULONG)(Arena+0x1100))[3]<=0x3ffff);
  for(unsigned i=0;i<MAX_DATA;i++)Arena[0x2000+i]=(UCHAR)(i^(i>>8));
  HardwareGood();Poll(&Adapter);
  CHECK(s.SrbStatus==SRB_STATUS_SUCCESS && s.DataTransferLength==MAX_DATA);
  for(unsigned i=0;i<MAX_DATA;i++)CHECK(Buffer[16+i]==(UCHAR)(i^(i>>8)));
  Guards();
#if DEGAS_UFS_INSTALL_MODE
  Reset();s=Read(op?0x88:0x28,2,DEGAS_WINDOWS_END-MAX_DATA/4096,MAX_DATA/4096);
  s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;s.Cdb[1]=8;
  for(unsigned i=0;i<MAX_DATA;i++)Buffer[16+i]=(UCHAR)(i^(i>>7));
  CHECK(Allowed(&s));CHECK(StartIo(&Adapter,&s));
  CHECK(!memcmp(Arena+0x2000,Buffer+16,MAX_DATA));
  HardwareGood();Poll(&Adapter);CHECK(!Completions && Adapter.FlushPhase);
  HardwareGood();Poll(&Adapter);CHECK(Completions==1 && s.SrbStatus==SRB_STATUS_SUCCESS && s.DataTransferLength==MAX_DATA);
  Guards();
  s=Read(op?0x88:0x28,2,DEGAS_WINDOWS_END-MAX_DATA/4096+1,MAX_DATA/4096);
  s.Cdb[0]=op?0x8a:0x2a;s.SrbFlags=SRB_FLAGS_DATA_OUT;CHECK(!Allowed(&s));
#endif
 }
}
static void FastCompletion(void){
 SCSI_REQUEST_BLOCK s;
 Reset();s=Read(0x28,2,0,1);StallHook=FastResponse;
 CHECK(StartIo(&Adapter,&s));CHECK(Completions==1 && !Timers && StallUs==3);
 CHECK(s.SrbStatus==SRB_STATUS_SUCCESS && !Adapter.Pending && Buffer[16]==0x37);
 Reset();s=Read(0x28,2,0,1);CHECK(StartIo(&Adapter,&s));
 CHECK(!Completions && Timers==1 && StallUs==50 && Adapter.Pending==&s);
}
int main(void){
 Arena=malloc(DMA_SIZE);Buffer=malloc(MAX_DATA+32);SenseBuffer=malloc(50);
 CHECK(Arena && Buffer && SenseBuffer);Reset();Whitelist();Descriptors();CallerGates();Responses();Recovery();WriteQualification();FastCompletion();FlushRequests();InstallWrites();LargestTransfer();
 free(SenseBuffer);free(Buffer);free(Arena);printf("PASS: %u checks (actual Allowed/Send/StartIo/Poll, mock Storport/HCI)\n",Checks);return 0;
}
