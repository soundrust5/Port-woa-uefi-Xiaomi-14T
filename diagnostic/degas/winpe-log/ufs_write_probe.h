/* Bounded 256MiB/10-minute raw stress inside the Windows partition.
 * Miniport enforces the same exact block independently. No filesystem mount. */
#include "../ufs/write_policy.h"
static WCHAR WritePath[1024];
static DWORD WriteStage,WriteError=ERROR_IO_PENDING,StressSeconds,StressWritten,StressRead;
static ULONGLONG StressStarted,LastProgress;
static BOOL WriteAttempted;
typedef struct {SCSI_PASS_THROUGH_DIRECT p;ULONG pad;BYTE sense[32];} WRITE_SPT;
#if defined(_M_ARM64)
static DWORD Transfer(HANDLE h,BYTE op,BYTE *data,DWORD lba,DWORD blocks){
 WRITE_SPT q;DWORD got;
 memset(&q,0,sizeof q);q.p.Length=sizeof q.p;q.p.PathId=0;q.p.TargetId=0;q.p.Lun=2;
 q.p.CdbLength=10;q.p.SenseInfoLength=32;q.p.SenseInfoOffset=FIELD_OFFSET(WRITE_SPT,sense);
 q.p.TimeOutValue=10;q.p.Cdb[0]=op;q.p.DataIn=op==0x35?SCSI_IOCTL_DATA_UNSPECIFIED:(op==0x2a?SCSI_IOCTL_DATA_OUT:SCSI_IOCTL_DATA_IN);
 if(op!=0x35){
  q.p.DataBuffer=data;q.p.DataTransferLength=blocks*4096;
  q.p.Cdb[2]=(BYTE)(lba>>24);q.p.Cdb[3]=(BYTE)(lba>>16);
  q.p.Cdb[4]=(BYTE)(lba>>8);q.p.Cdb[5]=(BYTE)lba;q.p.Cdb[7]=(BYTE)(blocks>>8);q.p.Cdb[8]=(BYTE)blocks;
 }
 if(!DeviceIoControl(h,IOCTL_SCSI_PASS_THROUGH_DIRECT,&q,sizeof q,&q,sizeof q,&got,0))return GetLastError();
 if(got<sizeof(q.p) || q.p.ScsiStatus || (op!=0x35 && q.p.DataTransferLength!=blocks*4096))return ERROR_IO_DEVICE;
 return 0;
}
#endif

#if defined(_M_ARM64)
static void Progress(BOOL force){
 ULONGLONG now=GetTickCount64();
 if(!force && now-LastProgress<1000)return;
 LastProgress=now;StressSeconds=(DWORD)((now-StressStarted)/1000);
 EnterCriticalSection(&Lock);Shown=Collect;LeaveCriticalSection(&Lock);
 PostMessageW(Window,WM_APP,0,0);
 Record(L"STRESS stage=%lu seconds=%lu writtenBlocks=%lu readBlocks=%lu error=%lu\r\n",WriteStage,StressSeconds,StressWritten,StressRead,WriteError);
}
static BYTE Pattern(DWORD lba,DWORD offset){
 if(offset<4)return (BYTE)(lba>>(offset*8));
 if(offset<8)return (BYTE)(~lba>>((offset-4)*8));
 return (BYTE)((offset*73+41)^(offset>>4));
}
static DWORD Chunk(HANDLE h,BYTE op,BYTE *data,DWORD lba,DWORD blocks){
 DWORD i,e;
 if(op==0x2a)for(i=0;i<blocks*4096;i++)data[i]=Pattern(lba+i/4096,i%4096);
 else memset(data,0,blocks*4096);
 e=Transfer(h,op,data,lba,blocks);if(e)return e;
 if(op==0x2a)StressWritten+=blocks;
 else {
  StressRead+=blocks;
  for(i=0;i<blocks*4096;i++)if(data[i]!=Pattern(lba+i/4096,i%4096))return ERROR_CRC;
 }
 Progress(FALSE);return 0;
}
static DWORD Sweep(HANDLE h,BYTE *data,BYTE op){
 DWORD offset,n,e;
 for(offset=0;offset<DEGAS_TEST_BLOCKS;offset+=n){
  n=DEGAS_TEST_BLOCKS-offset;if(n>14)n=14;
  e=Chunk(h,op,data,DEGAS_TEST_LBA+offset,n);if(e)return e;
  if(op==0x2a && !(offset%1024)){e=Transfer(h,0x35,0,0,0);if(e)return e;}
 }
 return 0;
}
#endif
static void WriteTest(void){
#if defined(_M_ARM64)
 HANDLE h;BYTE *data;DWORD e,scGot,seed=0x76a1932b,n,lba,iteration=0,mixedWrites=0;
 static const DWORD sizes[]={1,2,4,8,14};SCSI_ADDRESS sc;
 if(Selftest || ReadMask!=7 || BindResult || !WritePath[0] || WriteAttempted)return;
 WriteAttempted=TRUE;WriteStage=1;StressStarted=GetTickCount64();Progress(TRUE);
 h=CreateFileW(WritePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
 if(h==INVALID_HANDLE_VALUE){WriteError=GetLastError();return;}
 memset(&sc,0,sizeof sc);sc.Length=sizeof sc;
 if(!DeviceIoControl(h,IOCTL_SCSI_GET_ADDRESS,0,0,&sc,sizeof sc,&scGot,0) || scGot<sizeof sc || sc.PathId || sc.TargetId || sc.Lun!=2){WriteError=ERROR_INVALID_DATA;CloseHandle(h);return;}
 data=VirtualAlloc(0,14*4096,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!data){WriteError=GetLastError();CloseHandle(h);return;}
 WriteStage=2;e=Sweep(h,data,0x2a);
 if(!e)e=Transfer(h,0x35,0,0,0);
 if(!e){WriteStage=3;e=Sweep(h,data,0x28);}
 WriteStage=e?WriteStage:4;
 while(!e && GetTickCount64()-StressStarted<DEGAS_STRESS_MS){
  seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
  n=sizes[iteration%5];lba=DEGAS_TEST_LBA+seed%(DEGAS_TEST_BLOCKS-n+1);
  if(mixedWrites+n<=DEGAS_MIXED_WRITE_BLOCKS){
   e=Chunk(h,0x2a,data,lba,n);if(!e)mixedWrites+=n;
  }
  if(!e && !(iteration%128))e=Transfer(h,0x35,0,0,0);
  if(!e)e=Chunk(h,0x28,data,lba,n);
  iteration++;
 }
 if(!e){WriteStage=5;e=Transfer(h,0x35,0,0,0);}
 if(!e)e=Sweep(h,data,0x28);
 if(!e)WriteStage=6;
 WriteError=e;Progress(TRUE);
 Record(L"UFS STRESS finished stage=%lu error=%lu iterations=%lu\r\n",WriteStage,e,iteration);
 VirtualFree(data,0,MEM_RELEASE);CloseHandle(h);
#endif
}
