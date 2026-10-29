/* Two alternating raw journal slots inside the qualified Windows scratch
 * extent. Payload is flushed before the CRC-protected commit header. This
 * is a user-mode diagnostic journal, not a kernel crash dump facility. */
static DWORD PersistentError=ERROR_IO_PENDING,PersistentSaves;
#define JOURNAL_BLOCKS 16u
#define JOURNAL_PAYLOAD (15u*4096u)
#if defined(_M_ARM64)
static ULONGLONG PersistentSession;
static DWORD AppendLogTail(BYTE *dst,DWORD cap,const WCHAR *path,const char *title){
 HANDLE f;LARGE_INTEGER size,pos;DWORD used=0,got=0,n=0;
 while(title[n])n++;
 if(cap<=n)return 0;memcpy(dst,title,n);used=n;
 f=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
 if(f==INVALID_HANDLE_VALUE)return used;
 if(GetFileSizeEx(f,&size)){
  pos.QuadPart=size.QuadPart>(LONGLONG)(cap-used)?size.QuadPart-(cap-used):0;
  if(SetFilePointerEx(f,pos,0,FILE_BEGIN) && ReadFile(f,dst+used,cap-used,&got,0))used+=got;
 }
 CloseHandle(f);return used;
}
#endif
static void SavePersistentLog(void){
#if defined(_M_ARM64)
 BYTE *b;HANDLE h;DWORD e=0,n=0,lba,i,take,got;FILETIME ft;SCSI_ADDRESS sc;
 if(Selftest || ReadMask!=7 || BindResult || !WritePath[0])return;
 if(Log!=INVALID_HANDLE_VALUE)FlushFileBuffers(Log);
 b=VirtualAlloc(0,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!b){PersistentError=GetLastError();return;}
 n+=AppendLogTail(b+4096+n,24576,L"X:\\DegasLogs\\inventory.txt","\r\n=== inventory ===\r\n");
 n+=AppendLogTail(b+4096+n,28672,L"X:\\Windows\\INF\\setupapi.dev.log","\r\n=== setupapi tail ===\r\n");
 n+=AppendLogTail(b+4096+n,JOURNAL_PAYLOAD-n,L"X:\\Windows\\System32\\wpeinit.log","\r\n=== wpeinit ===\r\n");
 if(!PersistentSession){GetSystemTimeAsFileTime(&ft);PersistentSession=((ULONGLONG)ft.dwHighDateTime<<32)|ft.dwLowDateTime;}
 memcpy(b,"DGSLOG01",8);((DWORD*)b)[2]=1;((DWORD*)b)[3]=n;
 ((ULONGLONG*)b)[2]=PersistentSession;((DWORD*)b)[6]=PersistentSaves+1;
 ((DWORD*)b)[7]=Crc(b+4096,n);((DWORD*)b)[8]=Crc(b,32);
 lba=DEGAS_TEST_LBA+(PersistentSaves%2)*JOURNAL_BLOCKS;
 h=CreateFileW(WritePath,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
 if(h==INVALID_HANDLE_VALUE)e=GetLastError();
 else {
  memset(&sc,0,sizeof sc);sc.Length=sizeof sc;
  if(!DeviceIoControl(h,IOCTL_SCSI_GET_ADDRESS,0,0,&sc,sizeof sc,&got,0) ||
     got<sizeof sc || sc.PathId || sc.TargetId || sc.Lun!=2)e=ERROR_INVALID_DATA;
  for(i=0;i<15 && !e;i+=take){take=15-i;if(take>14)take=14;e=Transfer(h,0x2a,b+4096+i*4096,lba+1+i,take);}
  if(!e)e=Transfer(h,0x35,0,0,0);
  if(!e)e=Transfer(h,0x2a,b,lba,1);
  if(!e)e=Transfer(h,0x35,0,0,0);
  CloseHandle(h);
 }
 VirtualFree(b,0,MEM_RELEASE);PersistentError=e;if(!e)PersistentSaves++;
#endif
}
