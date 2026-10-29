/* Exercise the production retained_log.h verbatim against an ASan heap ring.
 * Mock MMIO and interlocked APIs only; no physical memory access is possible. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

typedef uint8_t UCHAR;
typedef uint32_t ULONG;
typedef int32_t LONG;
typedef void *PVOID;
typedef struct {int64_t QuadPart;} PHYSICAL_ADDRESS;
typedef struct DRIVER_OBJECT {unsigned unused;} DRIVER_OBJECT,*PDRIVER_OBJECT;
typedef void (*PDRIVER_UNLOAD)(PDRIVER_OBJECT);
enum {MmNonCached};
enum {RING_BYTES=0x40000,CAP=RING_BYTES-12};
static unsigned Checks,CompareCalls,ExchangeCalls,Barriers,MapCalls,UnmapCalls,PortCalls;
static unsigned FailMap;
static UCHAR *Memory;
#define CHECK(x) do {Checks++;if(!(x)){fprintf(stderr,"CHECK failed %s:%d: %s\n",__FILE__,__LINE__,#x);exit(1);}}while(0)
static LONG InterlockedCompareExchange(volatile LONG *p,LONG desired,LONG compare){
 CompareCalls++;LONG original=compare;
 __atomic_compare_exchange_n(p,&original,desired,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return original;
}
static LONG InterlockedExchange(volatile LONG *p,LONG value){ExchangeCalls++;return __atomic_exchange_n(p,value,__ATOMIC_SEQ_CST);}
static void KeMemoryBarrier(void){Barriers++;atomic_thread_fence(memory_order_seq_cst);}
static PVOID MmMapIoSpace(PHYSICAL_ADDRESS p,ULONG bytes,int cache){
 CHECK(p.QuadPart==0x480af000 && bytes==RING_BYTES && cache==MmNonCached);MapCalls++;return FailMap?NULL:Memory;
}
static void MmUnmapIoSpace(PVOID p,ULONG bytes){CHECK(p==Memory && bytes==RING_BYTES);UnmapCalls++;}
#include "retained-log-under-test.inc"

static void Reset(void){
 memset(Memory,0xa5,RING_BYTES);Retained=(volatile ULONG*)Memory;Retained[0]=0x43474244;Retained[1]=Retained[2]=0;
 RetainedBusy=0;PortUnload=NULL;FailMap=0;
 CompareCalls=ExchangeCalls=Barriers=MapCalls=UnmapCalls=PortCalls=0;
}
static void Golden(void){
 const char expected[]="\nWINUFS event 01234567 89ABCDEF FFFFFFFF\n";
 Reset();Retain("event",0x01234567,0x89abcdef,0xffffffff);
 CHECK(Retained[0]==0x43474244 && Retained[1]==sizeof expected-1 && Retained[2]==sizeof expected-1);
 CHECK(!memcmp(Memory+12,expected,sizeof expected-1));
 for(unsigned i=12+sizeof expected-1;i<RING_BYTES;i++)CHECK(Memory[i]==0xa5);
 CHECK(!RetainedBusy && CompareCalls==1 && ExchangeCalls==1 && Barriers==2);
 char event[200],wanted[112];memset(event,'x',sizeof event);event[sizeof event-1]=0;
 Reset();Retain(event,0,1,2);snprintf(wanted,sizeof wanted,"\nWINUFS %.48s 00000000 00000001 00000002\n",event);
 CHECK(Retained[1]==strlen(wanted) && !memcmp(Memory+12,wanted,strlen(wanted)));
}
static void Invalid(void){
 UCHAR *before=malloc(RING_BYTES);CHECK(before);
 for(unsigned bad=0;bad<7;bad++){
  Reset();switch(bad){case 0:Retained[0]=0;break;case 1:Retained[1]=CAP;break;
   case 2:Retained[1]=UINT32_MAX;break;case 3:Retained[2]=CAP+1;break;
   case 4:Retained[2]=UINT32_MAX;break;case 5:RetainedBusy=1;break;case 6:Retained=NULL;break;}
  memcpy(before,Memory,RING_BYTES);Retain("must not write",1,2,3);
  CHECK(!memcmp(before,Memory,RING_BYTES) && !Barriers);
  CHECK(CompareCalls==(bad==6?0:1));
  CHECK(ExchangeCalls==(bad<5?1:0));CHECK(RetainedBusy==(bad==5?1:0));
 }
 free(before);
}
static void Wrap(void){
 const char expected[]="\nWINUFS wrap 00000001 00000002 00000003\n";
 const unsigned length=sizeof expected-1;
 Reset();Retained[1]=CAP-10;Retained[2]=CAP-3;Retain("wrap",1,2,3);
 CHECK(Retained[1]==length-10 && Retained[2]==CAP);
 CHECK(!memcmp(Memory+12+CAP-10,expected,10));CHECK(!memcmp(Memory+12,expected+10,length-10));
 CHECK(Retained[0]==0x43474244 && !RetainedBusy);
 /* Compare a full ring with the tail of an independently formatted stream. */
 Reset();const unsigned records=9000;size_t total=0;char *stream=malloc(records*100),*linear=malloc(CAP);
 CHECK(stream && linear);
 for(unsigned i=0;i<records;i++){
  int count=snprintf(stream+total,100,"\nWINUFS record %08X %08X %08X\n",i,i^0xa5a5a5a5u,~i);
  CHECK(count>0 && count<100);total+=(size_t)count;Retain("record",i,i^0xa5a5a5a5u,~i);
 }
 CHECK(total>CAP && Retained[1]==total%CAP && Retained[2]==CAP);
 unsigned pos=Retained[1];memcpy(linear,Memory+12+pos,CAP-pos);memcpy(linear+CAP-pos,Memory+12,pos);
 CHECK(!memcmp(linear,stream+total-CAP,CAP));CHECK(!RetainedBusy && CompareCalls==records && ExchangeCalls==records);
 free(linear);free(stream);
}
static void MockPortUnload(PDRIVER_OBJECT driver){CHECK(driver && Retained== (volatile ULONG*)Memory && !UnmapCalls);PortCalls++;}
static void Lifetime(void){
 DRIVER_OBJECT driver={0};Reset();Retained=NULL;RetainedInit();
 CHECK(MapCalls==1 && Retained==(volatile ULONG*)Memory && Retained[2]>0);
 PortUnload=MockPortUnload;DiagnosticUnload(&driver);CHECK(PortCalls==1 && UnmapCalls==1 && !Retained);
 RetainedClose();CHECK(UnmapCalls==1);
 Reset();Retained=NULL;FailMap=1;RetainedInit();CHECK(MapCalls==1 && !Retained && !CompareCalls);
 DiagnosticUnload(&driver);CHECK(!UnmapCalls && !PortCalls);
 Reset();PortUnload=NULL;DiagnosticUnload(&driver);CHECK(!PortCalls && UnmapCalls==1 && !Retained);
}
int main(void){
 Memory=malloc(RING_BYTES);CHECK(Memory);Golden();Invalid();Wrap();Lifetime();free(Memory);
 printf("PASS: %u checks (actual retained_log.h; headers/wrap/trylock/lifetime)\n",Checks);return 0;
}
