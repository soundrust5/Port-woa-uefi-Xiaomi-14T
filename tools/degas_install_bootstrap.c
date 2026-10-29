/* Dedicated WinPE installer. No raw journal, no format/GPT changes.
 * Online/mount only exact captured LUN2 partitions, qualify filesystem flush,
 * then launch separately reviewed deployment script. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <setupapi.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <wincrypt.h>
#include <stdarg.h>
void *memset(void *p,int v,size_t n){volatile BYTE *q=p;while(n--)*q++=(BYTE)v;return p;}
void *memcpy(void *p,const void *s,size_t n){BYTE *q=p;const BYTE *r=s;while(n--)*q++=*r++;return p;}
static HANDLE Log=INVALID_HANDLE_VALUE;
static const GUID WinId={0xea93c01d,0x2628,0x46d6,{0xb6,0xac,0x77,0xac,0xc0,0xa4,0x13,0x11}};
static const GUID EspId={0x3dce8afa,0x741e,0x4cc5,{0xba,0x5f,0xa2,0x04,0x4d,0xb3,0xba,0x74}};
static const GUID Basic={0xebd0a0a2,0xb9e5,0x4433,{0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7}};
static const GUID EspType={0xc12a7328,0xf81f,0x11d2,{0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b}};
static BOOL Equal(const GUID *a,const GUID *b){const BYTE *x=(const BYTE*)a,*y=(const BYTE*)b;DWORD i;for(i=0;i<16;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
static void Say(const WCHAR *format,...){WCHAR text[1024];DWORD n,done;va_list a;va_start(a,format);wvsprintfW(text,format,a);va_end(a);n=(DWORD)lstrlenW(text);WriteConsoleW(GetStdHandle(STD_OUTPUT_HANDLE),text,n,&done,0);if(Log!=INVALID_HANDLE_VALUE){WriteFile(Log,text,n*2,&done,0);FlushFileBuffers(Log);}}
static DWORD Run(const WCHAR *command,DWORD timeout){WCHAR mutable[1024];STARTUPINFOW si;PROCESS_INFORMATION pi;DWORD e=0;
 if(lstrlenW(command)>=1024)return ERROR_BUFFER_OVERFLOW;lstrcpyW(mutable,command);ZeroMemory(&si,sizeof si);ZeroMemory(&pi,sizeof pi);si.cb=sizeof si;
 if(!CreateProcessW(0,mutable,0,0,TRUE,0,0,0,&si,&pi))return GetLastError();CloseHandle(pi.hThread);
 if(WaitForSingleObject(pi.hProcess,timeout)!=WAIT_OBJECT_0)e=WAIT_TIMEOUT;else if(!GetExitCodeProcess(pi.hProcess,&e))e=GetLastError();CloseHandle(pi.hProcess);return e;
}
static DWORD Cert(void){
 static const BYTE expected[32]={0x05,0x15,0xa9,0x33,0x4e,0x3d,0xfc,0x3c,0x4a,0x90,0xa5,0x2d,0xfc,0x8a,0x5f,0xc6,0xa0,0xe9,0x95,0xd1,0x99,0xd7,0x7e,0x92,0xa1,0xa9,0x0f,0x3e,0xf3,0x79,0x20,0xd6};
 static BYTE data[8192],hash[32];DWORD got,n=32,i,e=0;HANDLE f;HCERTSTORE store;const WCHAR *names[]={L"ROOT",L"TrustedPublisher"};
 f=CreateFileW(L"X:\\Windows\\System32\\DegasUfs\\development.cer",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);if(f==INVALID_HANDLE_VALUE)return GetLastError();
 if(GetFileSize(f,0)>sizeof data){CloseHandle(f);return ERROR_FILE_TOO_LARGE;}if(!ReadFile(f,data,sizeof data,&got,0))e=GetLastError();CloseHandle(f);if(e)return e;
 if(!CryptHashCertificate2(L"SHA256",0,0,data,got,hash,&n) || n!=32)return ERROR_INVALID_DATA;for(i=0;i<32;i++)if(hash[i]!=expected[i])return ERROR_INVALID_DATA;
 for(i=0;i<2;i++){store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE,names[i]);if(!store)return GetLastError();if(!CertAddEncodedCertificateToStore(store,X509_ASN_ENCODING,data,got,CERT_STORE_ADD_REPLACE_EXISTING,0))e=GetLastError();CertCloseStore(store,0);if(e)return e;}return 0;
}
static BOOL DeviceExists(const WCHAR *wanted){HDEVINFO set;SP_DEVINFO_DATA d;WCHAR id[256];DWORD i;BOOL yes=FALSE;
 set=SetupDiGetClassDevsW(0,0,0,DIGCF_ALLCLASSES|DIGCF_PRESENT);if(set==INVALID_HANDLE_VALUE)return FALSE;ZeroMemory(&d,sizeof d);d.cbSize=sizeof d;
 for(i=0;SetupDiEnumDeviceInfo(set,i,&d);i++)if(SetupDiGetDeviceInstanceIdW(set,&d,id,256,0) && CompareStringOrdinal(id,lstrlenW(wanted),wanted,lstrlenW(wanted),TRUE)==CSTR_EQUAL){yes=TRUE;break;}
 SetupDiDestroyDeviceInfoList(set);return yes;
}
static BOOL Part(const PARTITION_INFORMATION_EX *p,BOOL windows){return p->PartitionStyle==PARTITION_STYLE_GPT &&
 Equal(&p->Gpt.PartitionId,windows?&WinId:&EspId) && Equal(&p->Gpt.PartitionType,windows?&Basic:&EspType) &&
 p->StartingOffset.QuadPart==(windows?29796352LL:29267968LL)*4096 && p->PartitionLength.QuadPart==(windows?142446919680LL:2147483648LL);}
static DWORD FindDisk(DWORD *number){DWORD i,got,matches=0;static BYTE geometry[256],layout[32768];WCHAR path[64];
 for(i=0;i<16;i++){
  HANDLE h;SCSI_ADDRESS a;DISK_GEOMETRY_EX *g=(DISK_GEOMETRY_EX*)geometry;DRIVE_LAYOUT_INFORMATION_EX *l=(DRIVE_LAYOUT_INFORMATION_EX*)layout;DWORD j,w=0,s=0;
  wsprintfW(path,L"\\\\.\\PhysicalDrive%lu",i);h=CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)continue;
  ZeroMemory(&a,sizeof a);a.Length=sizeof a;ZeroMemory(geometry,sizeof geometry);ZeroMemory(layout,sizeof layout);
  if(!DeviceIoControl(h,IOCTL_SCSI_GET_ADDRESS,0,0,&a,sizeof a,&got,0) || got<sizeof a || a.PathId || a.TargetId || a.Lun!=2 ||
     !DeviceIoControl(h,IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,0,0,geometry,sizeof geometry,&got,0) || got<FIELD_OFFSET(DISK_GEOMETRY_EX,Data) || g->Geometry.BytesPerSector!=4096 || g->DiskSize.QuadPart!=264509587456LL ||
     !DeviceIoControl(h,IOCTL_DISK_GET_DRIVE_LAYOUT_EX,0,0,layout,sizeof layout,&got,0) || got<FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry) || l->PartitionStyle!=PARTITION_STYLE_GPT || l->PartitionCount>128 || got<FIELD_OFFSET(DRIVE_LAYOUT_INFORMATION_EX,PartitionEntry)+l->PartitionCount*sizeof(PARTITION_INFORMATION_EX)){CloseHandle(h);continue;}
  for(j=0;j<l->PartitionCount;j++){if(Part(&l->PartitionEntry[j],TRUE))w++;if(Part(&l->PartitionEntry[j],FALSE))s++;}
  CloseHandle(h);if(w==1 && s==1){matches++;*number=i;}
 }return matches==1?0:ERROR_NOT_FOUND;
}
static DWORD Online(DWORD disk){WCHAR path[64];HANDLE h;SET_DISK_ATTRIBUTES a;DWORD got,e=0;
 wsprintfW(path,L"\\\\.\\PhysicalDrive%lu",disk);h=CreateFileW(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return GetLastError();
 ZeroMemory(&a,sizeof a);a.Version=sizeof a;a.Persist=FALSE;a.AttributesMask=DISK_ATTRIBUTE_OFFLINE|DISK_ATTRIBUTE_READ_ONLY;
 if(!DeviceIoControl(h,IOCTL_DISK_SET_DISK_ATTRIBUTES,&a,sizeof a,0,0,&got,0))e=GetLastError();
 if(!e && !DeviceIoControl(h,IOCTL_DISK_UPDATE_PROPERTIES,0,0,0,0,&got,0))e=GetLastError();CloseHandle(h);return e;
}
static DWORD MountOne(DWORD disk,BOOL windows){WCHAR volume[128],raw[128],chosen[128]={0},existing[128];HANDLE find,h;DWORD matches=0,got,e;PARTITION_INFORMATION_EX p;VOLUME_DISK_EXTENTS x;
 const WCHAR *letter=windows?L"W:\\":L"S:\\";
 find=FindFirstVolumeW(volume,128);if(find==INVALID_HANDLE_VALUE)return GetLastError();
 do{
  lstrcpyW(raw,volume);raw[lstrlenW(raw)-1]=0;h=CreateFileW(raw,0,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)continue;
  ZeroMemory(&p,sizeof p);ZeroMemory(&x,sizeof x);
  if(DeviceIoControl(h,IOCTL_DISK_GET_PARTITION_INFO_EX,0,0,&p,sizeof p,&got,0) && got>=sizeof p && Part(&p,windows) &&
     DeviceIoControl(h,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,0,0,&x,sizeof x,&got,0) && got>=FIELD_OFFSET(VOLUME_DISK_EXTENTS,Extents)+sizeof(DISK_EXTENT) && x.NumberOfDiskExtents==1 && x.Extents[0].DiskNumber==disk && x.Extents[0].StartingOffset.QuadPart==p.StartingOffset.QuadPart && x.Extents[0].ExtentLength.QuadPart==p.PartitionLength.QuadPart){matches++;lstrcpyW(chosen,volume);}
  CloseHandle(h);
 }while(FindNextVolumeW(find,volume,128));FindVolumeClose(find);if(matches!=1)return ERROR_NOT_FOUND;
 if(GetVolumeNameForVolumeMountPointW(letter,existing,128))return lstrcmpiW(existing,chosen)?ERROR_ALREADY_ASSIGNED:0;
 lstrcpyW(raw,chosen);raw[lstrlenW(raw)-1]=0;h=CreateFileW(raw,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return GetLastError();
 e=DeviceIoControl(h,IOCTL_VOLUME_ONLINE,0,0,0,0,&got,0)?0:GetLastError();CloseHandle(h);if(e)return e;
 return SetVolumeMountPointW(letter,chosen)?0:GetLastError();
}
#if !DEGAS_MAINTENANCE && !DEGAS_RUNTIME_DIAG
static DWORD Qualify(void){BYTE *b,*r;DWORD i,got,e=0;HANDLE h;const WCHAR *path=L"W:\\DegasInstall\\ufs-write-qualification.bin";
 b=VirtualAlloc(0,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);r=VirtualAlloc(0,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!b || !r){e=ERROR_NOT_ENOUGH_MEMORY;goto done;}
 for(i=0;i<65536;i++)b[i]=(BYTE)((i*73+0x39)^(i>>7));
 h=CreateFileW(path,GENERIC_WRITE,0,0,CREATE_NEW,FILE_FLAG_NO_BUFFERING,0);if(h==INVALID_HANDLE_VALUE){e=GetLastError();goto done;}
 if(!WriteFile(h,b,65536,&got,0))e=GetLastError();else if(got!=65536)e=ERROR_WRITE_FAULT;
 if(!e && !FlushFileBuffers(h))e=GetLastError();CloseHandle(h);if(e)goto done;
 h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING,0);if(h==INVALID_HANDLE_VALUE){e=GetLastError();goto done;}
 if(!ReadFile(h,r,65536,&got,0))e=GetLastError();else if(got!=65536)e=ERROR_HANDLE_EOF;CloseHandle(h);
 if(!e)for(i=0;i<65536;i++)if(b[i]!=r[i]){e=ERROR_CRC;break;}
done:if(b)VirtualFree(b,0,MEM_RELEASE);if(r)VirtualFree(r,0,MEM_RELEASE);return e;
}
#endif
void Entry(void){HKEY key;DWORD e=0,i,disk=0;WCHAR fs[32];BOOL windowsMounted=FALSE,espMounted=FALSE;
 Log=CreateFileW(L"X:\\degas-installer-bootstrap.log",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
 Say(L"\r\nXiaomi 14T - Windows deployment\r\nNo raw UFS journal. No automatic reboot.\r\n");
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",0,KEY_READ,&key)){e=10;goto done;}RegCloseKey(key);
 if(GetFileAttributesW(L"X:\\Windows\\System32\\degas-install-authorized.txt")==INVALID_FILE_ATTRIBUTES || !DeviceExists(L"ACPI\\DGS0001\\")){e=11;goto done;}
 e=Cert();if(e)goto done;
 Say(L"Loading storage driver...\r\n");e=Run(L"X:\\Windows\\System32\\drvload.exe X:\\Windows\\System32\\DegasUfs\\degasufs.inf",90000);if(e)goto done;
 e=Run(L"X:\\Windows\\System32\\wpeinit.exe",120000);if(e)goto done;
 if(DeviceExists(L"ACPI\\DGS0002\\")){
  DWORD touch=Run(L"X:\\Windows\\System32\\drvload.exe X:\\Windows\\System32\\DegasTouch\\degastouch.inf",90000);
  if(!touch)touch=Run(L"X:\\Windows\\System32\\drvload.exe X:\\Windows\\System32\\DegasTouch\\degasvhf.inf",90000);
  Say(L"Touch driver result=%lu (storage installation does not depend on touch)\r\n",touch);
 }
 for(i=0;i<30;i++){e=FindDisk(&disk);if(!e)break;Sleep(1000);}if(e)goto done;
 Say(L"Verified LUN2 disk %lu; bringing only that disk online.\r\n",disk);e=Online(disk);if(e)goto done;
 for(i=0;i<15;i++){e=MountOne(disk,TRUE);if(!e)break;Sleep(1000);}if(e)goto done;windowsMounted=TRUE;
 e=MountOne(disk,FALSE);if(e)goto done;espMounted=TRUE;
 CopyFileW(L"X:\\degas-installer-bootstrap.log",L"S:\\degas-installer-bootstrap.log",FALSE);
 if(!GetVolumeInformationW(L"W:\\",0,0,0,0,0,fs,32) || lstrcmpiW(fs,L"NTFS")){e=12;goto done;}
 e=Run(L"W:\\DegasInstall\\degas-install-verify.exe targets",30000);if(e)goto done;
 CopyFileW(L"X:\\degas-installer-bootstrap.log",L"W:\\DegasInstall\\bootstrap-before.log",FALSE);
#if DEGAS_RUNTIME_DIAG
 /* CopyFileW includes hidden/system hive logs, unlike CMD COPY. Keep a
  * complete pre-edit hive set; an absent optional log is the only tolerated
  * copy failure. Never overwrite an earlier attempt's backup. */
 if(!CreateDirectoryW(L"W:\\DegasInstall\\runtime-backup-pe-v2",0)){e=GetLastError();goto done;}
 {
  const WCHAR *names[]={L"SYSTEM",L"SYSTEM.LOG1",L"SYSTEM.LOG2"};
  WCHAR src[256],dst[256];
  for(i=0;i<3;i++){
   wsprintfW(src,L"W:\\Windows\\System32\\config\\%s",names[i]);
   wsprintfW(dst,L"W:\\DegasInstall\\runtime-backup-pe-v2\\%s",names[i]);
   if(!CopyFileW(src,dst,TRUE)){
    e=GetLastError();
    if(i && e==ERROR_FILE_NOT_FOUND){e=0;continue;}
    Say(L"Hive backup failed: %s error=%lu\r\n",names[i],e);goto done;
   }
  }
 }
 Say(L"Installing CPU logger and BSOD parameter display only.\r\n");
 e=Run(L"X:\\Windows\\System32\\cmd.exe /d /c X:\\Windows\\System32\\degas-runtime-maintenance.cmd > W:\\DegasInstall\\cpu-diag-maintenance.log 2>&1",INFINITE);
 if(!e)Say(L"SUCCESS - CPU logger installed, BSOD parameters enabled. Return to fastboot.\r\n");
#elif DEGAS_MAINTENANCE
 Say(L"Maintenance: UFS update and failed setup recovery. No image application.\r\n");
 e=Run(L"X:\\Windows\\System32\\cmd.exe /d /c W:\\DegasInstall\\maintenance.cmd",INFINITE);
#else
 Say(L"Qualifying NTFS write / flush / close / readback...\r\n");e=Qualify();Say(L"NTFS qualification result=%lu\r\n",e);if(e)goto done;
 e=Run(L"X:\\Windows\\System32\\cmd.exe /d /c W:\\DegasInstall\\install.cmd",INFINITE);
#endif
done:
 Say(L"\r\nBootstrap result=%lu. Report this result; do not retry automatically.\r\n",e);
 if(Log!=INVALID_HANDLE_VALUE){CloseHandle(Log);Log=INVALID_HANDLE_VALUE;}
 /* File copies only; never write a raw sector. */
 if(windowsMounted && GetFileAttributesW(L"W:\\DegasInstall")!=INVALID_FILE_ATTRIBUTES)CopyFileW(L"X:\\degas-installer-bootstrap.log",L"W:\\DegasInstall\\bootstrap.log",FALSE);
 if(espMounted)CopyFileW(L"X:\\degas-installer-bootstrap.log",L"S:\\degas-installer-bootstrap.log",FALSE);
 for(;;)Sleep(1000);
}
