/* Read-only runtime telemetry; writes only its own log. No setup changes. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
typedef struct {ULONG Number,MaxMhz,CurrentMhz,MhzLimit,MaxIdleState,CurrentIdleState;} DEGAS_PROCESSOR_POWER;
__declspec(dllimport) LONG WINAPI CallNtPowerInformation(POWER_INFORMATION_LEVEL,PVOID,ULONG,PVOID,ULONG);
void *memset(void *p,int v,size_t n){volatile BYTE *q=p;while(n--)*q++=(BYTE)v;return p;}
void *memcpy(void *p,const void *s,size_t n){BYTE *q=p;const BYTE *r=s;while(n--)*q++=*r++;return p;}
static SERVICE_STATUS_HANDLE StatusHandle;
static HANDLE Log=INVALID_HANDLE_VALUE;
static void Number(const char *key,DWORD value){char b[160];DWORD written;int n=wsprintfA(b,"%s=%lu\r\n",key,value);if(Log!=INVALID_HANDLE_VALUE){WriteFile(Log,b,n,&written,0);FlushFileBuffers(Log);}}
static void Status(DWORD state){SERVICE_STATUS s;ZeroMemory(&s,sizeof s);s.dwServiceType=SERVICE_WIN32_OWN_PROCESS;s.dwCurrentState=state;SetServiceStatus(StatusHandle,&s);}
static void WINAPI Control(DWORD code){(void)code;}
static void Snapshot(void){SYSTEM_INFO si;MEMORYSTATUSEX m;GetNativeSystemInfo(&si);ZeroMemory(&m,sizeof m);m.dwLength=sizeof m;
 Number("uptime_seconds",(DWORD)(GetTickCount64()/1000));
 Number("active_processor_groups",GetActiveProcessorGroupCount());
 Number("active_processors_all_groups",GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
 Number("native_system_processors",si.dwNumberOfProcessors);
 for(WORD g=0;g<GetActiveProcessorGroupCount();g++){Number("group",g);Number("group_active_processors",GetActiveProcessorCount(g));}
 {DWORD count=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);DEGAS_PROCESSOR_POWER info[8];
  if(count && count<=8){LONG status;ZeroMemory(info,sizeof info);status=CallNtPowerInformation(ProcessorInformation,0,0,info,count*sizeof(info[0]));Number("OS_reported_power_query_status",(DWORD)status);
   if(status==0)for(DWORD i=0;i<count;i++){Number("OS_reported_CPU_number",info[i].Number);Number("OS_reported_max_MHz",info[i].MaxMhz);Number("OS_reported_current_MHz",info[i].CurrentMhz);Number("OS_reported_limit_MHz",info[i].MhzLimit);}
  }
 }
 if(GlobalMemoryStatusEx(&m)){Number("physical_total_MiB",(DWORD)(m.ullTotalPhys>>20));Number("physical_available_MiB",(DWORD)(m.ullAvailPhys>>20));Number("memory_load_percent",m.dwMemoryLoad);}
}
static void WINAPI Main(DWORD argc,WCHAR **argv){WCHAR path[MAX_PATH];UINT n;(void)argc;(void)argv;
 StatusHandle=RegisterServiceCtrlHandlerW(L"DegasRuntime",Control);if(!StatusHandle)return;Status(SERVICE_RUNNING);
 n=GetWindowsDirectoryW(path,MAX_PATH);if(n && n+32<MAX_PATH){lstrcatW(path,L"\\Temp\\DegasRuntime.log");
 Log=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);}
 Number("snapshot_version",1);Snapshot();Sleep(10000);Snapshot();Number("capture_complete",1);
 if(Log!=INVALID_HANDLE_VALUE)CloseHandle(Log);
 Status(SERVICE_STOPPED);
}
void Entry(void){SERVICE_TABLE_ENTRYW table[]={{L"DegasRuntime",Main},{0,0}};BOOL ok=StartServiceCtrlDispatcherW(table);DWORD error=ok?0:GetLastError();
 if(error==ERROR_FAILED_SERVICE_CONTROLLER_CONNECT){WCHAR path[MAX_PATH];UINT n=GetWindowsDirectoryW(path,MAX_PATH);if(n && n+32<MAX_PATH){lstrcatW(path,L"\\Temp\\DegasRuntime.log");Log=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);}
 Number("snapshot_version",1);Snapshot();Sleep(10000);Snapshot();Number("capture_complete",1);if(Log!=INVALID_HANDLE_VALUE)CloseHandle(Log);error=0;}
 ExitProcess(error);}
