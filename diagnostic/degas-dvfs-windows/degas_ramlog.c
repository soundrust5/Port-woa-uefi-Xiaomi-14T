#include "degas_ramlog.h"
// Existing reserved ramoops slot30. Only its first2KiB; +F00 route flag intact.
static volatile ULONG *gZone;
static volatile ULONG_PTR *gBugData;
static PVOID gBugEx;
static KBUGCHECK_CALLBACK_RECORD gCallback;
static ULONG gNormal[512],gCrash[512];
static volatile LONG gWriter,gCrashing;
static volatile LONG64 gSequence;
static volatile ULONG gStage,gEvent;
static volatile ULONG_PTR gContext;
static ULONGLONG gEpoch;
static BOOLEAN gRegistered,gDataValid;
static NTSTATUS gStatus=STATUS_DEVICE_NOT_READY;
static ULONG Crc(const UCHAR*p,ULONG n){ULONG c=0xffffffff,i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320);}return c^0xffffffff;}
static VOID Put32(UCHAR*p,ULONG v){ULONG i;for(i=0;i<4;i++)p[i]=(UCHAR)(v>>(i*8));}
static VOID Put64(UCHAR*p,ULONGLONG v){ULONG i;for(i=0;i<8;i++)p[i]=(UCHAR)(v>>(i*8));}
static VOID Publish(ULONG *Words,ULONG Stage,ULONG Event,ULONG_PTR Context,BOOLEAN Crash){
 UCHAR*p=(UCHAR*)Words,*b=p+64;ULONG i,c;ULONGLONG sequence=(ULONGLONG)InterlockedIncrement64(&gSequence);
 // Pure resident stores, no allocation, locks, disk, formatting or system calls.
 for(i=0;i<512;i++)Words[i]=0;
 for(i=0;i<15;i++)p[12+i]=(UCHAR)"====0.000000-D\n"[i];
 for(i=0;i<8;i++)b[i]=(UCHAR)"DGBUG001"[i];
 Put32(b+8,1);Put32(b+12,256);Put64(b+16,sequence);
 Put32(b+24,Stage);Put32(b+28,Event);Put64(b+32,Context);
 if(Crash && gDataValid)for(i=0;i<5;i++)Put64(b+40+i*8,gBugData[i]);
 Put32(b+80,gDataValid);Put32(b+84,gRegistered);Put64(b+88,(ULONG_PTR)gBugData);
 Put64(b+96,(ULONG_PTR)gBugEx);Put32(b+104,(ULONG)gStatus);Put32(b+108,Crash);
 for(i=0;i<15;i++)b[112+i]=(UCHAR)"DEGAS-PEP-A0-V1"[i];
 Put32(b+128,0);Put32(b+132,gStage);Put64(b+136,gEpoch);
 c=Crc(b+8,232);Put32(b+240,c);Put32(b+244,~c);
 Words[0]=0x43474244;Words[1]=Words[2]=0x7f4;
 if(!Crash && gCrashing)return;
 gZone[0]=0;KeMemoryBarrier();
 for(i=3;i<512;i++)gZone[i]=Words[i];
 gZone[1]=Words[1];gZone[2]=Words[2];KeMemoryBarrier();gZone[0]=Words[0];KeMemoryBarrier();
}
KBUGCHECK_CALLBACK_ROUTINE DegasBugCallback;
VOID DegasBugCallback(PVOID Buffer,ULONG Length){
 UNREFERENCED_PARAMETER(Buffer);UNREFERENCED_PARAMETER(Length);
 InterlockedExchange(&gCrashing,1);
 if(gZone)Publish(gCrash,0xb0,gEvent,gContext,TRUE);
 // Retain the BSOD on screen. User explicitly disabled automatic recovery.
}
VOID DegasRamMark(ULONG Stage,ULONG Event,ULONG_PTR Context){
 gStage=Stage;gEvent=Event;gContext=Context;
 if(!gZone || gCrashing || InterlockedCompareExchange(&gWriter,1,0))return;
 Publish(gNormal,Stage,Event,Context,FALSE);InterlockedExchange(&gWriter,0);
}
NTSTATUS DegasRamInitialize(VOID){
 PHYSICAL_ADDRESS pa;UNICODE_STRING name;
 pa.QuadPart=0x480ae000;gZone=(volatile ULONG*)MmMapIoSpaceEx(pa,4096,PAGE_READWRITE|PAGE_NOCACHE);
 if(!gZone)return STATUS_INSUFFICIENT_RESOURCES;
 RtlInitUnicodeString(&name,L"KiBugCheckData");gBugData=(volatile ULONG_PTR*)MmGetSystemRoutineAddress(&name);
 RtlInitUnicodeString(&name,L"KeBugCheckEx");gBugEx=MmGetSystemRoutineAddress(&name);
 // Pinned NT5bb49f... exported data ABI; refuse unknown layout rather than scan.
 gDataValid=gBugData && gBugEx && ((ULONG_PTR)gBugData-(ULONG_PTR)gBugEx)==0xb5e1e0 &&
  !((ULONG_PTR)gBugData&7) && MmIsAddressValid((PVOID)gBugData) && MmIsAddressValid((PVOID)(gBugData+4));
 gEpoch=KeQueryInterruptTime();KeInitializeCallbackRecord(&gCallback);
 gRegistered=KeRegisterBugCheckCallback(&gCallback,DegasBugCallback,NULL,0,(PUCHAR)"DegasDvfs");
 gStatus=gRegistered && gDataValid?STATUS_SUCCESS:STATUS_NOT_SUPPORTED;
 DegasRamMark(1,0,0);return gStatus;
}
