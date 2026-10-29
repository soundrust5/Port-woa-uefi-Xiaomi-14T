/* Native ARM64 PE-only guards. No disk writes or host registry changes.
 * Caller assigns W:/S:; actual partition identities, ranges and LUN are checked.
 * "boot" temporarily loads only W:\Windows\...\SYSTEM into the PE registry. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winioctl.h>
#include <ntddscsi.h>
void *memset(void *p,int v,size_t n){volatile BYTE *q=p;while(n--)*q++=(BYTE)v;return p;}
static const GUID WinId={0xea93c01d,0x2628,0x46d6,{0xb6,0xac,0x77,0xac,0xc0,0xa4,0x13,0x11}};
static const GUID EspId={0x3dce8afa,0x741e,0x4cc5,{0xba,0x5f,0xa2,0x04,0x4d,0xb3,0xba,0x74}};
static const GUID Basic={0xebd0a0a2,0xb9e5,0x4433,{0x87,0xc0,0x68,0xb6,0xb7,0x26,0x99,0xc7}};
static const GUID EspType={0xc12a7328,0xf81f,0x11d2,{0xba,0x4b,0x00,0xa0,0xc9,0x3e,0xc9,0x3b}};
static BOOL Equal(const GUID *a,const GUID *b){const BYTE *x=(const BYTE*)a,*y=(const BYTE*)b;DWORD i;for(i=0;i<16;i++)if(x[i]!=y[i])return FALSE;return TRUE;}
static BOOL Has(const WCHAR *s,const WCHAR *q){for(;*s;s++){const WCHAR *a=s,*b=q;while(*a && *b && *a==*b){a++;b++;}if(!*b)return TRUE;}return FALSE;}
static DWORD Volume(const WCHAR *raw,const WCHAR *root,const WCHAR *fs,const GUID *id,const GUID *type,ULONGLONG start,ULONGLONG bytes,DWORD *disk){
 HANDLE h;PARTITION_INFORMATION_EX p;VOLUME_DISK_EXTENTS e;DWORD got;WCHAR actual[32];BOOL ok;
 h=CreateFileW(raw,0,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return 20;
 ZeroMemory(&p,sizeof p);ZeroMemory(&e,sizeof e);
 ok=DeviceIoControl(h,IOCTL_DISK_GET_PARTITION_INFO_EX,0,0,&p,sizeof p,&got,0) && got>=sizeof p && p.PartitionStyle==PARTITION_STYLE_GPT && Equal(&p.Gpt.PartitionId,id) && Equal(&p.Gpt.PartitionType,type) && p.StartingOffset.QuadPart==(LONGLONG)start && p.PartitionLength.QuadPart==(LONGLONG)bytes;
 if(ok)ok=DeviceIoControl(h,IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,0,0,&e,sizeof e,&got,0) && got>=FIELD_OFFSET(VOLUME_DISK_EXTENTS,Extents)+sizeof(DISK_EXTENT) && e.NumberOfDiskExtents==1 && e.Extents[0].StartingOffset.QuadPart==(LONGLONG)start && e.Extents[0].ExtentLength.QuadPart==(LONGLONG)bytes;
 CloseHandle(h);if(!ok)return 21;
 if(!GetVolumeInformationW(root,0,0,0,0,0,actual,32) || lstrcmpiW(actual,fs))return 22;
 *disk=e.Extents[0].DiskNumber;return 0;
}
static DWORD Targets(void){
 DWORD a,b,e,got;WCHAR path[64];HANDLE h;SCSI_ADDRESS s;BYTE g[256];DISK_GEOMETRY_EX *geometry=(DISK_GEOMETRY_EX*)g;
 e=Volume(L"\\\\.\\W:",L"W:\\",L"NTFS",&WinId,&Basic,29796352ULL*4096,142446919680ULL,&a);if(e)return e;
 e=Volume(L"\\\\.\\S:",L"S:\\",L"FAT32",&EspId,&EspType,29267968ULL*4096,2147483648ULL,&b);if(e)return e+10;
 if(a!=b)return 40;wsprintfW(path,L"\\\\.\\PhysicalDrive%lu",a);
 h=CreateFileW(path,0,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);if(h==INVALID_HANDLE_VALUE)return 41;
 ZeroMemory(&s,sizeof s);s.Length=sizeof s;ZeroMemory(g,sizeof g);
 e=(!DeviceIoControl(h,IOCTL_SCSI_GET_ADDRESS,0,0,&s,sizeof s,&got,0) || got<sizeof s || s.PathId || s.TargetId || s.Lun!=2)?42:0;
 if(!e && (!DeviceIoControl(h,IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,0,0,g,sizeof g,&got,0) || got<FIELD_OFFSET(DISK_GEOMETRY_EX,Data) || geometry->Geometry.BytesPerSector!=4096 || geometry->DiskSize.QuadPart!=264509587456ULL))e=43;
 CloseHandle(h);return e;
}
static BOOL Privilege(const WCHAR *name){HANDLE token;TOKEN_PRIVILEGES t;BOOL ok;
 if(!OpenProcessToken(GetCurrentProcess(),TOKEN_ADJUST_PRIVILEGES|TOKEN_QUERY,&token))return FALSE;
 t.PrivilegeCount=1;ok=LookupPrivilegeValueW(0,name,&t.Privileges[0].Luid);t.Privileges[0].Attributes=SE_PRIVILEGE_ENABLED;
 if(ok){SetLastError(0);ok=AdjustTokenPrivileges(token,FALSE,&t,0,0,0) && GetLastError()==ERROR_SUCCESS;}CloseHandle(token);return ok;
}
static DWORD Boot(void){
 HKEY h=0,k=0;DWORD e,t,n,v;WCHAR image[512],path[520];
 if(!Privilege(SE_RESTORE_NAME) || !Privilege(SE_BACKUP_NAME))return 50;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline",0,KEY_READ,&h)==ERROR_SUCCESS){RegCloseKey(h);return 51;}
 e=RegLoadKeyW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline",L"W:\\Windows\\System32\\config\\SYSTEM");if(e)return 52;
 e=53;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline\\ControlSet001\\Services\\degasufs",0,KEY_READ,&k)==ERROR_SUCCESS){
  n=4;if(RegQueryValueExW(k,L"Start",0,&t,(BYTE*)&v,&n)==0 && t==REG_DWORD && n==4 && v==0){
   n=sizeof image;if(RegQueryValueExW(k,L"ImagePath",0,&t,(BYTE*)image,&n)==0 && (t==REG_SZ || t==REG_EXPAND_SZ) && n>=2 && n<=sizeof image && !(n&1) && !image[n/2-1]){
    const WCHAR *tail=image;
    if(CompareStringOrdinal(tail,12,L"\\SystemRoot\\",12,TRUE)==CSTR_EQUAL)tail+=12;
    if(CompareStringOrdinal(tail,9,L"System32\\",9,TRUE)==CSTR_EQUAL &&
       !Has(tail,L"..") && !Has(tail,L":") && lstrlenW(tail)>=12 &&
       !lstrcmpiW(tail+lstrlenW(tail)-12,L"degasufs.sys")){
     lstrcpyW(path,L"W:\\Windows\\");lstrcatW(path,tail);if(GetFileAttributesW(path)!=INVALID_FILE_ATTRIBUTES)e=0;
    }
   }
  }RegCloseKey(k);
 }
 if(RegUnLoadKeyW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline"))return 54;
 return e;
}
static DWORD RepairSetup(void){
 HKEY h=0,g=0,c=0;DWORD e,t,n,v,oldGeneral=6,oldChild=1;
 if(!Privilege(SE_RESTORE_NAME) || !Privilege(SE_BACKUP_NAME))return 60;
 e=RegLoadKeyW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline",L"W:\\Windows\\System32\\config\\SYSTEM");if(e)return 61;
 e=62;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline",0,KEY_READ|KEY_WRITE,&h))goto done;
 if(RegOpenKeyExW(h,L"Setup\\Status\\SysprepStatus",0,KEY_READ|KEY_WRITE,&g))goto done;
 if(RegOpenKeyExW(h,L"Setup\\Status\\ChildCompletion",0,KEY_READ|KEY_WRITE,&c))goto done;
 n=4;if(RegQueryValueExW(g,L"GeneralizationState",0,&t,(BYTE*)&v,&n) || t!=REG_DWORD || n!=4 || v!=oldGeneral)goto done;
 n=4;if(RegQueryValueExW(c,L"setup.exe",0,&t,(BYTE*)&v,&n) || t!=REG_DWORD || n!=4 || v!=oldChild)goto done;
 v=4;e=RegSetValueExW(g,L"GeneralizationState",0,REG_DWORD,(BYTE*)&v,4);if(e)goto done;
 v=0;e=RegSetValueExW(c,L"setup.exe",0,REG_DWORD,(BYTE*)&v,4);if(e){RegSetValueExW(g,L"GeneralizationState",0,REG_DWORD,(BYTE*)&oldGeneral,4);goto done;}
 e=RegFlushKey(h);
done:if(c)RegCloseKey(c);if(g)RegCloseKey(g);if(h)RegCloseKey(h);
 if(RegUnLoadKeyW(HKEY_LOCAL_MACHINE,L"DEGASVerifyOffline"))return 63;return e;
}
void Entry(void){HKEY key;DWORD e,n;char text[64];WCHAR *cmd=GetCommandLineW();
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",0,KEY_READ,&key))ExitProcess(10);RegCloseKey(key);
 e=Targets();if(!e && Has(cmd,L" boot"))e=Boot();
 if(!e && Has(cmd,L" repairsetup"))e=RepairSetup();
 wsprintfA(text,"DEGAS install guard result=%lu\r\n",e);WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),text,lstrlenA(text),&n,0);ExitProcess(e);
}
