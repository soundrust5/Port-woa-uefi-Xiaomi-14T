/* Read-only storage probe, with phone-only installation of the test miniport.
 * /selftest runs one inventory to stdout, without a window or wpeinit.
 * Normal WinPE mode binds the packaged driver in RAM and compares disk reads.
 */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <winioctl.h>
#include <ntddscsi.h>
#include <wincrypt.h>
#include <stdarg.h>
#include <initguid.h>
#include <devpkey.h>
#include "../touch/windows/degastouch_diag.h"
C_ASSERT(sizeof(DEGAS_TOUCH_DIAGNOSTIC)==18*sizeof(DWORD));

void *memset(void *p,int c,size_t n) { volatile BYTE *q=p;while(n--)*q++=(BYTE)c;return p; }
void *memcpy(void *p,const void *s,size_t n) { BYTE *q=p;const BYTE *r=s;while(n--)*q++=*r++;return p; }

#define ROWS 26
#define CONTROLLERS 8
#define DISKS 16
#define ROW_CHARS 192
static const GUID DiskInterface={0x53f56307,0xb6bf,0x11d0,{0x94,0xf2,0x00,0xa0,0xc9,0x1e,0xfb,0x8b}};
typedef struct {
  WCHAR id[256],hw[256],name[180],service[80],inf[100];
  DWORD flags,problem,status,propertyError;
  CONFIGRET cm;
} CONTROLLER;
typedef struct { WCHAR text[ROW_CHARS]; } DISK_ROW;
typedef struct {
  DWORD pass,devices,problems,controllers,disks,diskErrors,enumError;
  DWORD touches,touchHid,touchDiagError,touchDiagBytes;
  DWORD touchDiag[18];
  CONTROLLER touch;
  CONTROLLER controller[CONTROLLERS];
  DISK_ROW disk[DISKS];
} SNAPSHOT;
static SNAPSHOT Collect,Shown;
static WCHAR Lines[ROWS][ROW_CHARS];
static CRITICAL_SECTION Lock;
static HWND Window;
static HANDLE Log=INVALID_HANDLE_VALUE,WpeProcess;
static BOOL Selftest;
static DWORD LogError,WpeResult=ERROR_IO_PENDING,Pass;
static HFONT Font;
static DWORD BindResult=ERROR_IO_PENDING,ReadMask,ReadError;
static BOOL BindAttempted;
static DWORD TouchBindResult=ERROR_IO_PENDING,TouchChildResult=ERROR_IO_PENDING;
static BOOL TouchBindAttempted;
static volatile LONG WindowsInputEvents,WindowsLastX,WindowsLastY;
#define TRAIL_POINTS 2048u
typedef struct { POINT point; UINT32 id; } TOUCH_POINT;
static TOUCH_POINT Trail[TRAIL_POINTS];
static UINT32 ActivePointers[10];
static volatile LONG PointerCount,PointerPeak;
static DWORD TrailCount,LastPointerTick;
static BOOL MouseDown;

static BOOL Has(const WCHAR *s,const WCHAR *needle) {
  int n=lstrlenW(needle),remain=lstrlenW(s);
  for(;remain>=n;s++,remain--)if(CompareStringOrdinal(s,n,needle,n,TRUE)==CSTR_EQUAL)return TRUE;
  return FALSE;
}
static void Record(const WCHAR *fmt,...) {
  static WCHAR wide[2048];static char utf8[8192];
  DWORD wrote;int n;va_list args;
  va_start(args,fmt);wvsprintfW(wide,fmt,args);va_end(args);
  n=WideCharToMultiByte(CP_UTF8,0,wide,-1,utf8,sizeof utf8,0,0);
  if(n>1 && Log!=INVALID_HANDLE_VALUE && !WriteFile(Log,utf8,(DWORD)n-1,&wrote,0))LogError=GetLastError();
}
static void Decimal(ULONGLONG value,WCHAR *out) {
  WCHAR reverse[21];int n=0,i;
  do {reverse[n++]=(WCHAR)(L'0'+value%10);value/=10;}while(value);
  for(i=0;i<n;i++)out[i]=reverse[n-i-1];out[n]=0;
}
static void Property(HDEVINFO set,SP_DEVINFO_DATA *d,DWORD prop,WCHAR *out,DWORD chars) {
  DWORD type=0;memset(out,0,chars*sizeof(WCHAR));
  if(!SetupDiGetDeviceRegistryPropertyW(set,d,prop,&type,(BYTE*)out,chars*sizeof(WCHAR),0))out[0]=0;
  out[chars-1]=0;
}
static void MultiRecord(const WCHAR *label,const WCHAR *s,DWORD chars) {
  DWORD at=0,n;
  while(at<chars && s[at]) {
    for(n=0;at+n<chars && s[at+n];n++) {}
    if(at+n==chars)break;
    Record(L" %s=%.255s\r\n",label,s+at);at+=n+1;
  }
}
static BOOL TouchDiagValid(const BYTE *data,DWORD size,DWORD type) {
  const DWORD *d=(const DWORD*)data;
  return type==REG_BINARY && size==18*sizeof(DWORD) &&
    d[0]==DEGAS_TOUCH_DIAG_MAGIC && d[1]==size && d[2]==1;
}
static void TouchRegistry(HDEVINFO set,SP_DEVINFO_DATA *d) {
  HKEY key;DWORD type=0,size=sizeof Collect.touchDiag,error;
  key=SetupDiOpenDevRegKey(set,d,DICS_FLAG_GLOBAL,0,DIREG_DEV,KEY_QUERY_VALUE);
  if(key==INVALID_HANDLE_VALUE){Collect.touchDiagError=GetLastError();return;}
  error=RegQueryValueExW(key,L"TouchDiagnostic",0,&type,(BYTE*)Collect.touchDiag,&size);
  RegCloseKey(key);Collect.touchDiagBytes=size;
  if(!error && !TouchDiagValid((BYTE*)Collect.touchDiag,size,type))error=ERROR_INVALID_DATA;
  Collect.touchDiagError=error;
  Record(L" TouchDiagnostic error=%lu bytes=%lu type=%lu\r\n",error,size,type);
  if(!error){
    Record(L" Touch stage=%lu start=%08lX io=%lu cleanup=%lu reads=%lu good=%lu bad=%lu\r\n",
      Collect.touchDiag[3],Collect.touchDiag[4],Collect.touchDiag[5],Collect.touchDiag[6],
      Collect.touchDiag[7],Collect.touchDiag[8],Collect.touchDiag[9]);
    Record(L" Touch reports=%lu polls=%lu active=%lu raw=%02lX id=%04lX x=%lu y=%lu event=%lu\r\n",
      Collect.touchDiag[10],Collect.touchDiag[11],Collect.touchDiag[12],Collect.touchDiag[13],
      Collect.touchDiag[14],Collect.touchDiag[15],Collect.touchDiag[16],Collect.touchDiag[17]);
  }
}
static void PnpInventory(void) {
  HDEVINFO set=SetupDiGetClassDevsW(0,0,0,DIGCF_PRESENT|DIGCF_ALLCLASSES);
  SP_DEVINFO_DATA d;DWORD i,bytes;DEVPROPTYPE type;
  static CONTROLLER c;
  static WCHAR cls[80],hardware[2048],compatible[2048];BOOL storage;
  if(set==INVALID_HANDLE_VALUE) {Collect.enumError=GetLastError();Record(L"PnP enumeration error=%lu\r\n",Collect.enumError);return;}
  memset(&d,0,sizeof d);d.cbSize=sizeof d;
  for(i=0;SetupDiEnumDeviceInfo(set,i,&d);i++) {
    memset(&c,0,sizeof c);
    SetupDiGetDeviceInstanceIdW(set,&d,c.id,256,0);c.id[255]=0;
    Property(set,&d,SPDRP_DEVICEDESC,c.name,180);
    Property(set,&d,SPDRP_SERVICE,c.service,80);
    Property(set,&d,SPDRP_CLASS,cls,80);
    Property(set,&d,SPDRP_HARDWAREID,hardware,2048);
    Property(set,&d,SPDRP_COMPATIBLEIDS,compatible,2048);
    lstrcpynW(c.hw,hardware,256);
    c.cm=CM_Get_DevNode_Status(&c.flags,&c.problem,d.DevInst,0);
    bytes=sizeof c.status;
    if(!SetupDiGetDevicePropertyW(set,&d,&DEVPKEY_Device_ProblemStatus,&type,(BYTE*)&c.status,bytes,0,0))c.propertyError=GetLastError();
    if(!SetupDiGetDevicePropertyW(set,&d,&DEVPKEY_Device_DriverInfPath,&type,(BYTE*)c.inf,sizeof c.inf,0,0))c.inf[0]=0;
    c.inf[99]=0;
    Collect.devices++;if(c.problem || c.cm!=CR_SUCCESS)Collect.problems++;
    storage=lstrcmpiW(cls,L"SCSIAdapter")==0 || lstrcmpiW(cls,L"HDC")==0 ||
      Has(c.service,L"storufs") || Has(c.service,L"storahci") || Has(c.service,L"stornvme") ||
      Has(c.id,L"ACPI\\DGS0001\\") || Has(hardware,L"ACPI\\DGS0001");
    /* Log every present devnode: an unbound ACPI controller can have no class. */
    Record(L"ID=%.255s\r\n Name=%.179s\r\n Class=%.79s Service=%.79s INF=%.99s\r\n",c.id,c.name,cls,c.service,c.inf);
    Record(L" CM=%lu Flags=%08lX Started=%lu Problem=%lu NTSTATUS=%08lX StatusPropertyError=%lu StorageController=%lu\r\n",
      c.cm,c.flags,(c.flags&DN_STARTED)!=0,c.problem,c.status,c.propertyError,storage);
    MultiRecord(L"HardwareID",hardware,2048);MultiRecord(L"CompatibleID",compatible,2048);
    if(Has(c.id,L"ACPI\\DGS0002\\") || Has(hardware,L"ACPI\\DGS0002")) {
      Collect.touches++;Collect.touch=c;TouchRegistry(set,&d);
    }
    if(Has(hardware,L"DEGAS\\TOUCH_VHF") || Has(c.id,L"DEGAS\\TOUCH_VHF") || Has(compatible,L"HID_DEVICE_SYSTEM_VHF"))Collect.touchHid++;
    if(storage) {
      if(Collect.controllers<CONTROLLERS)Collect.controller[Collect.controllers]=c;
      Collect.controllers++;
    }
  }
  if(GetLastError()!=ERROR_NO_MORE_ITEMS)Collect.enumError=GetLastError();
  SetupDiDestroyDeviceInfoList(set);
}
#include "ufs_probe.h"
#include "touch_probe.h"
#include "ufs_write_probe.h"
#include "ufs_persistent_log.h"
static void DiskInventory(void) {
  HDEVINFO set=SetupDiGetClassDevsW(&DiskInterface,0,0,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
  SP_DEVICE_INTERFACE_DATA iface;SP_DEVICE_INTERFACE_DETAIL_DATA_W *detail;
  DWORD i,needed,got,number,error;HANDLE h;
  STORAGE_DEVICE_NUMBER deviceNumber;GET_DISK_ATTRIBUTES attributes;
  static BYTE geometryBuffer[1024];DISK_GEOMETRY_EX *geometry=(DISK_GEOMETRY_EX*)geometryBuffer;
  static WCHAR row[ROW_CHARS],size[21];
  if(set==INVALID_HANDLE_VALUE) {Collect.diskErrors++;Record(L"Disk interface enumeration error=%lu\r\n",GetLastError());return;}
  memset(&iface,0,sizeof iface);iface.cbSize=sizeof iface;
  for(i=0;SetupDiEnumDeviceInterfaces(set,0,&DiskInterface,i,&iface);i++) {
    needed=0;SetupDiGetDeviceInterfaceDetailW(set,&iface,0,0,&needed,0);
    if(needed<sizeof(*detail) || needed>65536) {Collect.diskErrors++;continue;}
    detail=HeapAlloc(GetProcessHeap(),HEAP_ZERO_MEMORY,needed);
    if(!detail) {Collect.diskErrors++;break;}
    detail->cbSize=sizeof(*detail);
    if(!SetupDiGetDeviceInterfaceDetailW(set,&iface,detail,needed,0,0)) {
      Record(L"Disk interface detail error=%lu\r\n",GetLastError());Collect.diskErrors++;HeapFree(GetProcessHeap(),0,detail);continue;
    }
    /* DesiredAccess=0: metadata queries only; no raw reads or write access. */
    h=CreateFileW(detail->DevicePath,0,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
    error=h==INVALID_HANDLE_VALUE?GetLastError():0;
    Record(L"DiskInterface=%.255s OpenError=%lu\r\n",detail->DevicePath,error);
    number=0xffffffffU;
    if(h==INVALID_HANDLE_VALUE) {Collect.diskErrors++;HeapFree(GetProcessHeap(),0,detail);continue;}
    memset(&deviceNumber,0,sizeof deviceNumber);
    if(DeviceIoControl(h,IOCTL_STORAGE_GET_DEVICE_NUMBER,0,0,&deviceNumber,sizeof deviceNumber,&got,0))number=deviceNumber.DeviceNumber;
    memset(geometryBuffer,0,sizeof geometryBuffer);
    if(DeviceIoControl(h,IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,0,0,geometryBuffer,sizeof geometryBuffer,&got,0) && got>=FIELD_OFFSET(DISK_GEOMETRY_EX,Data)) {
      if(!Selftest && (ULONGLONG)geometry->DiskSize.QuadPart==264509587456ULL && geometry->Geometry.BytesPerSector==4096)lstrcpynW(WritePath,detail->DevicePath,1024);
      Decimal((ULONGLONG)geometry->DiskSize.QuadPart,size);
      wsprintfW(row,L"Disk %lu: %lu MiB / sector %lu",number,(DWORD)((ULONGLONG)geometry->DiskSize.QuadPart>>20),geometry->Geometry.BytesPerSector);
      Record(L" DiskNumber=%lu Bytes=%s SectorBytes=%lu\r\n",number,size,geometry->Geometry.BytesPerSector);
      if(!Selftest && BindAttempted && BindResult==0)ReadCheck(h,detail->DevicePath,(ULONGLONG)geometry->DiskSize.QuadPart,geometry->Geometry.BytesPerSector);
    } else {
      error=GetLastError();wsprintfW(row,L"Disk %lu: size query error %lu",number,error);
      Record(L" DiskNumber=%lu geometry error=%lu\r\n",number,error);Collect.diskErrors++;
    }
    memset(&attributes,0,sizeof attributes);attributes.Version=sizeof attributes;
    if(DeviceIoControl(h,IOCTL_DISK_GET_DISK_ATTRIBUTES,0,0,&attributes,sizeof attributes,&got,0))
      Record(L" Offline=%lu ReadOnly=%lu (queried only)\r\n",(attributes.Attributes&DISK_ATTRIBUTE_OFFLINE)!=0,(attributes.Attributes&DISK_ATTRIBUTE_READ_ONLY)!=0);
    else Record(L" Disk attributes query error=%lu\r\n",GetLastError());
    CloseHandle(h);
    HeapFree(GetProcessHeap(),0,detail);
    if(Collect.disks<DISKS)lstrcpynW(Collect.disk[Collect.disks].text,row,ROW_CHARS);
    Collect.disks++;
  }
  if(GetLastError()!=ERROR_NO_MORE_ITEMS)Record(L"Disk enumeration end error=%lu\r\n",GetLastError());
  SetupDiDestroyDeviceInfoList(set);
}
static void Snapshot(void) {
  SYSTEMTIME time;
  memset(&Collect,0,sizeof Collect);Collect.pass=++Pass;GetSystemTime(&time);
  Record(L"\r\n=== Degas storage inventory %lu UTC %04u-%02u-%02u %02u:%02u:%02u ===\r\n",
    Pass,time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);
  Record(L"UFS qualification: 256MiB scratch extent in Windows partition; no disk online or Setup.\r\n");
  if(Selftest)Record(L"Selftest: wpeinit and UI skipped; output is stdout only.\r\n");
  PnpInventory();DiskInventory();
  Record(L"Touch summary devices=%lu vhf=%lu bind=%lu childBind=%lu WindowsInput=%ld lastX=%ld lastY=%ld\r\n",
    Collect.touches,Collect.touchHid,TouchBindResult,TouchChildResult,
    InterlockedCompareExchange(&WindowsInputEvents,0,0),InterlockedCompareExchange(&WindowsLastX,0,0),
    InterlockedCompareExchange(&WindowsLastY,0,0));
  (void)WriteTest; /* Stress is opt-in, never run while journaling. */
  Record(L"Summary: devices=%lu problems=%lu controllers=%lu disks=%lu diskErrors=%lu pnpError=%lu wpeinit=%lu\r\n",
    Collect.devices,Collect.problems,Collect.controllers,Collect.disks,Collect.diskErrors,Collect.enumError,WpeResult);
  if(!Selftest) {EnterCriticalSection(&Lock);Shown=Collect;LeaveCriticalSection(&Lock);PostMessageW(Window,WM_APP,0,0);}
}
static void StartWpeinit(void) {
  STARTUPINFOW si;PROCESS_INFORMATION pi;WCHAR command[MAX_PATH+8],system[MAX_PATH];
  memset(&si,0,sizeof si);memset(&pi,0,sizeof pi);si.cb=sizeof si;
  si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
  if(!GetSystemDirectoryW(system,MAX_PATH)) {WpeResult=GetLastError();return;}
  wsprintfW(command,L"\"%.250s\\wpeinit.exe\"",system);
  if(!CreateProcessW(0,command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&si,&pi)) {WpeResult=GetLastError();return;}
  CloseHandle(pi.hThread);WpeProcess=pi.hProcess;
}
static DWORD WINAPI Worker(void *unused) {
  LARGE_INTEGER length;DWORD exitCode;
  (void)unused;
  /* Offline-installed boot driver is expected to be running already. */
  PnpInventory();
  for(DWORD i=0;i<Collect.controllers && i<CONTROLLERS;i++)
    if(Has(Collect.controller[i].id,L"ACPI\\DGS0001\\") &&
       Has(Collect.controller[i].service,L"degasufs") &&
       (Collect.controller[i].flags&DN_STARTED) && !Collect.controller[i].problem){
      BindAttempted=TRUE;BindResult=0;
    }
  if(!BindAttempted){BindAttempted=TRUE;BindResult=BindDriver();}
  Snapshot();SavePersistentLog();
  if(Collect.touches && !TouchBindAttempted){
    TouchBindAttempted=TRUE;TouchBindResult=BindTouchDriver();Snapshot();SavePersistentLog();
  }
  StartWpeinit();
  for(;;) {
    if(WpeProcess && WaitForSingleObject(WpeProcess,0)==WAIT_OBJECT_0) {
      if(GetExitCodeProcess(WpeProcess,&exitCode))WpeResult=exitCode;else WpeResult=GetLastError();
      CloseHandle(WpeProcess);WpeProcess=0;
    }
    /* Keep two bounded RAM-only logs. Never open a disk or volume for writing. */
    if(Log!=INVALID_HANDLE_VALUE && GetFileSizeEx(Log,&length) && length.QuadPart>8*1024*1024) {
      CloseHandle(Log);Log=INVALID_HANDLE_VALUE;
      MoveFileExW(L"X:\\DegasLogs\\inventory.txt",L"X:\\DegasLogs\\inventory-previous.txt",MOVEFILE_REPLACE_EXISTING);
      Log=CreateFileW(L"X:\\DegasLogs\\inventory.txt",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
      if(Log==INVALID_HANDLE_VALUE)LogError=GetLastError();
    }
    Snapshot();
    if(!BindAttempted && !WpeProcess && WpeResult==0){
      BindAttempted=TRUE;BindResult=BindDriver();Snapshot();
    }
    CopyFileW(L"X:\\Windows\\System32\\wpeinit.log",L"X:\\DegasLogs\\wpeinit.log",FALSE);
    CopyFileW(L"X:\\Windows\\INF\\setupapi.dev.log",L"X:\\DegasLogs\\setupapi.dev.log",FALSE);
    SavePersistentLog();
    PostMessageW(Window,WM_APP,0,0);
    Sleep(5000);
  }
}
static void Panel(void) {
  DWORD i,at=7,page=(Shown.pass?Shown.pass-1:0),count,diskStart,diskCount;
  CONTROLLER *c;
  memset(Lines,0,sizeof Lines);
  if(Shown.touches){
    DWORD *t=Shown.touchDiag;c=&Shown.touch;
    lstrcpyW(Lines[0],L"DEGAS / WINDOWS TOUCH TEST");
    wsprintfW(Lines[1],L"Install %lu / HID install %lu / VHF %lu",TouchBindResult,TouchChildResult,Shown.touchHid);
    wsprintfW(Lines[2],L"Started %lu / problem %lu / status %08lX",(c->flags&DN_STARTED)!=0,c->problem,c->status);
    wsprintfW(Lines[3],L"Diagnostic error %lu / stage %lu / ID %04lX",Shown.touchDiagError,t[3],t[14]);
    wsprintfW(Lines[4],L"Start %08lX / SPI error %lu / cleanup %lu",t[4],t[5],t[6]);
    wsprintfW(Lines[5],L"Reads %lu / good %lu / bad %lu",t[7],t[8],t[9]);
    wsprintfW(Lines[6],L"Reports %lu / polls %lu / active %lu",t[10],t[11],t[12]);
    wsprintfW(Lines[7],L"Raw mode %02lX / sensor %lu,%lu / event %lu",t[13],t[15],t[16],t[17]);
    wsprintfW(Lines[8],L"Windows input %ld / fingers %ld / peak %ld",
      InterlockedCompareExchange(&WindowsInputEvents,0,0),InterlockedCompareExchange(&PointerCount,0,0),InterlockedCompareExchange(&PointerPeak,0,0));
    wsprintfW(Lines[9],L"UFS disks %lu / READ %s / error %lu",Shown.disks,ReadMask==7?L"PASS":L"pending",ReadError);
    wsprintfW(Lines[10],L"UFS journal saves %lu / error %lu",PersistentSaves,PersistentError);
    lstrcpyW(Lines[11],L"Draw with TWO fingers in the black area below.");
    return;
  }
  lstrcpyW(Lines[0],L"DEGAS / WINDOWS PE");
  lstrcpyW(Lines[1],L"STORAGE INVENTORY");
  wsprintfW(Lines[2],L"Snapshot %lu / refresh 5 seconds",Shown.pass);
  wsprintfW(Lines[3],L"PnP: %lu devices / %lu problems",Shown.devices,Shown.problems);
  wsprintfW(Lines[4],L"Controllers %lu / disks %lu",Shown.controllers,Shown.disks);
  wsprintfW(Lines[5],L"wpeinit %lu (997 = running)",WpeResult);
  wsprintfW(Lines[6],L"Driver install %lu / read mask %lX",BindResult,ReadMask);
  count=Shown.controllers<CONTROLLERS?Shown.controllers:CONTROLLERS;
  if(!count) {lstrcpyW(Lines[at++],L"No storage controller devnode found.");lstrcpyW(Lines[at++],L"All present PnP IDs are in the RAM log.");}
  else {
    c=&Shown.controller[page%count];
    wsprintfW(Lines[at++],L"Controller %lu / %lu",page%count+1,Shown.controllers);
    wsprintfW(Lines[at++],L"%.70s",c->name);
    wsprintfW(Lines[at++],L"ID: %.100s",c->id);
    wsprintfW(Lines[at++],L"Service: %.70s",c->service[0]?c->service:L"(unbound)");
    wsprintfW(Lines[at++],L"INF: %.90s",c->inf[0]?c->inf:L"(none)");
    wsprintfW(Lines[at++],L"Started %lu / problem %lu / CM %lu",(c->flags&DN_STARTED)!=0,c->problem,c->cm);
    wsprintfW(Lines[at++],L"Status %08lX / query error %lu",c->status,c->propertyError);
    wsprintfW(Lines[at++],L"HW: %.100s",c->hw);
  }
  diskCount=Shown.disks<DISKS?Shown.disks:DISKS;
  diskStart=diskCount?(page%((diskCount+4)/5))*5:0;
  wsprintfW(Lines[16],L"Disk inventory / %lu total",Shown.disks);
  at=17;
  if(!Shown.disks)lstrcpyW(Lines[at++],L"No disk interfaces found.");
  for(i=diskStart;i<diskCount && i<diskStart+5;i++)lstrcpynW(Lines[at++],Shown.disk[i].text,ROW_CHARS);
  wsprintfW(Lines[23],L"Disk errors %lu / log error %lu",Shown.diskErrors,LogError);
  wsprintfW(Lines[22],L"UFS READ %s / error %lu",ReadMask==7?L"PASS":L"pending",ReadError);
  wsprintfW(Lines[24],L"UFS journal saves %lu / error %lu",PersistentSaves,PersistentError);
  lstrcpyW(Lines[25],L"Two CRC-protected slots; export from Android.");
}
static void DrawPoint(HWND h,LONG x,LONG y,UINT32 id) {
  RECT r;GetClientRect(h,&r);
  InterlockedIncrement(&WindowsInputEvents);InterlockedExchange(&WindowsLastX,x);InterlockedExchange(&WindowsLastY,y);
  if(x>=0 && x<r.right && y>=r.bottom/2 && y<r.bottom){
    Trail[TrailCount%TRAIL_POINTS].point.x=x;Trail[TrailCount%TRAIL_POINTS].point.y=y;
    Trail[TrailCount%TRAIL_POINTS].id=id;TrailCount++;
  }
  InvalidateRect(h,0,FALSE);
}
static LRESULT CALLBACK Proc(HWND h,UINT message,WPARAM wp,LPARAM lp) {
  PAINTSTRUCT paint;HDC dc;RECT r,line;int i,x,y,height,width,fontSize;
  if(message==WM_POINTERDOWN || message==WM_POINTERUPDATE || message==WM_POINTERUP){
    POINTER_INPUT_TYPE type;POINTER_INFO info;UINT32 id=GET_POINTERID_WPARAM(wp);
    if(GetPointerType(id,&type) && type==PT_TOUCH && GetPointerInfo(id,&info)){
      UINT32 color=0;POINT point=info.ptPixelLocation;ScreenToClient(h,&point);LastPointerTick=GetTickCount();
      { unsigned k,freeSlot=10;LONG active=0;
        for(k=0;k<10;k++){if(ActivePointers[k]==id)break;if(!ActivePointers[k] && freeSlot==10)freeSlot=k;}
        color=k<10?k:freeSlot;
        if(message==WM_POINTERUP || !(info.pointerFlags&POINTER_FLAG_INCONTACT)){if(k<10)ActivePointers[k]=0;}
        else if(k==10 && freeSlot<10)ActivePointers[freeSlot]=id;
        for(k=0;k<10;k++)if(ActivePointers[k])active++;
        InterlockedExchange(&PointerCount,active);if(active>PointerPeak)InterlockedExchange(&PointerPeak,active);
      }
      if(message!=WM_POINTERUPDATE || (info.pointerFlags&POINTER_FLAG_INCONTACT))DrawPoint(h,point.x,point.y,color);
      return 0;
    }
  }
  if(message==WM_LBUTTONDOWN || message==WM_MOUSEMOVE || message==WM_LBUTTONUP){
    /* A touch pointer may also promote mouse messages. Suppress that duplicate,
     * but retain ordinary mouse promotion in PE builds without pointer input. */
    ULONG_PTR extra=(ULONG_PTR)GetMessageExtraInfo();
    BOOL promoted=(extra&0xffffff00u)==0xff515700u;
    if(!(promoted && LastPointerTick && GetTickCount()-LastPointerTick<500)){
      if(message==WM_LBUTTONDOWN){MouseDown=TRUE;SetCapture(h);}
      if(message!=WM_MOUSEMOVE || (MouseDown && (wp&MK_LBUTTON)))
        DrawPoint(h,(SHORT)LOWORD(lp),(SHORT)HIWORD(lp),0);
      if(message==WM_LBUTTONUP){MouseDown=FALSE;ReleaseCapture();}
    }
    return 0;
  }
  if(message==WM_CAPTURECHANGED){MouseDown=FALSE;return 0;}
  if(message==WM_PAINT) {
    dc=BeginPaint(h,&paint);GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(BLACK_BRUSH));
    width=r.right;height=r.bottom;x=width/12;y=height/12;
    fontSize=(height-2*y)/(ROWS+2);if(fontSize>(width-2*x)/35)fontSize=(width-2*x)/35;if(fontSize<10)fontSize=10;
    Font=CreateFontW(-fontSize,0,0,0,FW_MEDIUM,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
    SelectObject(dc,Font);SetTextColor(dc,RGB(255,255,255));SetBkMode(dc,TRANSPARENT);
    EnterCriticalSection(&Lock);Panel();
    for(i=0;i<(Shown.touches?12:ROWS);i++) {
      line.left=x;line.right=width-x;line.top=y+i*(height-2*y)/ROWS;line.bottom=line.top+(height-2*y)/ROWS;
      DrawTextW(dc,Lines[i],-1,&line,DT_LEFT|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX);
    }
    LeaveCriticalSection(&Lock);
    { HBRUSH brushes[2]={CreateSolidBrush(RGB(0,220,80)),CreateSolidBrush(RGB(0,160,255))};DWORD n,total=TrailCount<TRAIL_POINTS?TrailCount:TRAIL_POINTS;
      for(n=0;n<total;n++){POINT point=Trail[n].point;RECT dot={point.x-3,point.y-3,point.x+4,point.y+4};
        if(dot.top<height/2)dot.top=height/2;FillRect(dc,&dot,brushes[Trail[n].id&1]);
      }
      DeleteObject(brushes[0]);DeleteObject(brushes[1]);
    }
    SelectObject(dc,GetStockObject(SYSTEM_FONT));DeleteObject(Font);Font=0;EndPaint(h,&paint);return 0;
  }
  if(message==WM_APP) {InvalidateRect(h,0,FALSE);return 0;}
  if(message==WM_CLOSE)return 0;
  return DefWindowProcW(h,message,wp,lp);
}
void Entry(void) {
  HKEY key;WCHAR windows[MAX_PATH];WNDCLASSW wc;MSG msg;HANDLE worker;
  Selftest=Has(GetCommandLineW(),L"/selftest");
  if(Selftest) {
    DWORD good[18]={0x31544744u,72,1};
    if(!TouchDiagValid((BYTE*)good,sizeof good,REG_BINARY) ||
       TouchDiagValid((BYTE*)good,sizeof good-4,REG_BINARY) ||
       TouchDiagValid((BYTE*)good,sizeof good,REG_SZ))ExitProcess(21);
    good[2]=2;if(TouchDiagValid((BYTE*)good,sizeof good,REG_BINARY))ExitProcess(22);
    Log=GetStdHandle(STD_OUTPUT_HANDLE);Snapshot();ExitProcess(Collect.enumError?20:0);
  }
  /* Normal mode is restricted to the X: WinPE RAM installation. */
  if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",0,KEY_READ,&key)!=ERROR_SUCCESS)ExitProcess(10);
  RegCloseKey(key);
  if(!GetWindowsDirectoryW(windows,MAX_PATH) || CompareStringOrdinal(windows,3,L"X:\\",3,TRUE)!=CSTR_EQUAL)ExitProcess(11);
  CreateDirectoryW(L"X:\\DegasLogs",0);
  Log=CreateFileW(L"X:\\DegasLogs\\inventory.txt",GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);
  if(Log==INVALID_HANDLE_VALUE)LogError=GetLastError();
  InitializeCriticalSection(&Lock);SetProcessDPIAware();
  memset(&wc,0,sizeof wc);wc.lpfnWndProc=Proc;wc.hInstance=GetModuleHandleW(0);wc.lpszClassName=L"DegasStorageInventory";
  if(!RegisterClassW(&wc))ExitProcess(12);
  Window=CreateWindowExW(WS_EX_TOPMOST,wc.lpszClassName,L"Degas storage inventory",WS_POPUP|WS_VISIBLE,0,0,
    GetSystemMetrics(SM_CXSCREEN),GetSystemMetrics(SM_CYSCREEN),0,0,wc.hInstance,0);
  if(!Window)ExitProcess(13);
  worker=CreateThread(0,0,Worker,0,0,0);if(!worker)ExitProcess(14);CloseHandle(worker);
  while(GetMessageW(&msg,0,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
  ExitProcess(0);
}
