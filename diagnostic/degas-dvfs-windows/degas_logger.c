#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "degas_ioctl.h"
void *memset(void *p,int v,size_t n){volatile BYTE*q=p;while(n--)*q++=(BYTE)v;return p;}
void *memcpy(void*p,const void*s,size_t n){BYTE*q=p;const BYTE*r=s;while(n--)*q++=*r++;return p;}
static HANDLE gLog=INVALID_HANDLE_VALUE;
static SERVICE_STATUS_HANDLE gStatus;
static void Number(const char*key,DWORD value){char b[160];DWORD written;int n=wsprintfA(b,"%s=%lu\r\n",key,value);if(gLog!=INVALID_HANDLE_VALUE)WriteFile(gLog,b,n,&written,0);}
/* Two explicit DWORDs preserve every raw counter bit without CRT formatting. */
static void Number64(const char*key,ULONGLONG value){char b[180];DWORD written;int n=wsprintfA(b,"%s_hi=%lu\r\n%s_lo=%lu\r\n",key,(DWORD)(value>>32),key,(DWORD)value);if(gLog!=INVALID_HANDLE_VALUE)WriteFile(gLog,b,n,&written,0);}
static void State(DWORD state){SERVICE_STATUS s;ZeroMemory(&s,sizeof s);s.dwServiceType=SERVICE_WIN32_OWN_PROCESS;s.dwCurrentState=state;SetServiceStatus(gStatus,&s);}
static void WINAPI Control(DWORD code){(void)code;}
static void Capture(void){
 WCHAR path[MAX_PATH];UINT n=GetWindowsDirectoryW(path,MAX_PATH);DWORD round,i,got;HANDLE dev;BOOL amuLogged=FALSE;
 if(!n || n+30>=MAX_PATH)return;
 lstrcatW(path,L"\\Temp\\DegasDvfs.log");gLog=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,0);
 Number("capture_version",4);
 {SC_HANDLE manager=OpenSCManagerW(0,0,SC_MANAGER_CONNECT);SC_HANDLE service=manager?OpenServiceW(manager,L"DegasDvfs",SERVICE_START|SERVICE_QUERY_STATUS):0;
  if(service){BOOL ok=StartServiceW(service,0,0);Number("driver_start_error",ok?0:GetLastError());CloseServiceHandle(service);}
  else Number("driver_open_error",GetLastError());if(manager)CloseServiceHandle(manager);}
 for(round=0;round<120;round++){
  DEGAS_DVFS_SNAPSHOT s;ZeroMemory(&s,sizeof s);got=0;
  Number("sample",round);Number("uptime_ms",GetTickCount());
  dev=CreateFileW(DEGAS_DVFS_WIN32_PATH,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,0,0);
  if(dev==INVALID_HANDLE_VALUE)Number("open_error",GetLastError());
  else{
   if(!DeviceIoControl(dev,IOCTL_DEGAS_DVFS_SNAPSHOT,0,0,&s,sizeof s,&got,0))Number("ioctl_error",GetLastError());
   else if(got!=sizeof s || s.Size!=sizeof s || s.Version!=DEGAS_DVFS_SNAPSHOT_VERSION)Number("bad_snapshot_size",got);
   else{
    Number("ram_logger_status",s.Reserved);Number("backend_status",(DWORD)s.BackendInitStatus);Number("pep_status",(DWORD)s.PepRegistrationStatus);Number("pep_registered",s.PepRegistered);Number("processor_mask",s.RegisteredProcessorMask);
    Number("dpm_prepare_count",s.DpmPrepareCount);Number("dpm_register_count",s.DpmRegisterCount);Number("ppm_notifications",s.PpmNotificationCount);Number("perf_set_count",s.PerfSetCount);Number("last_accepted_uid",s.LastAcceptedUid);
    {char b[512];DWORD written;int len; s.LastRejectedDeviceId[127]=0;
     len=WideCharToMultiByte(CP_UTF8,0,s.LastRejectedDeviceId,-1,b,sizeof b,0,0);
     if(len>0 && gLog!=INVALID_HANDLE_VALUE){WriteFile(gLog,"rejected_device=",16,&written,0);WriteFile(gLog,b,len-1,&written,0);WriteFile(gLog,"\r\n",2,&written,0);}}
    for(i=0;i<3;i++){
     DEGAS_DVFS_DOMAIN_STATE*d=&s.Domains[i];Number("domain",i);Number("lowest_khz",d->LowestKhz);Number("highest_khz",d->HighestKhz);Number("request_khz",d->RequestKhz);Number("effective_ceiling_khz",d->CeilingKhz);Number("ready",d->Ready);Number("raw_domain_temperature_millic",(DWORD)d->TemperatureMilliC);Number("thermal_status",d->ThermalStatus);Number("thermal_valid",d->ThermalValid);
    }
    Number("raw_thermal_valid",s.RawThermalValid);Number("freshness_qualified",s.FreshnessQualified);Number("raw_utc",s.RawUtcCount);
    for(i=0;i<8;i++){Number("raw_thermal_cpu",i);Number("raw_temperature_millic",(DWORD)s.RawCoreTemperatureMilliC[i]);}
    for(i=0;i<3;i++){Number("raw_ceiling_domain",i);Number("raw_firmware_ceiling_khz",s.RawCeilingKhz[i]);}
    /* Immutable boot probe: write all eight records once per capture. */
    if(!amuLogged && s.Amu.ProbeComplete){
     amuLogged=TRUE;
     Number("amu_init_status",(DWORD)s.Amu.InitStatus);Number("amu_qualified_mask",s.Amu.QualifiedMask);Number("amu_processor_count",s.Amu.ProcessorCount);
     for(i=0;i<8;i++){
      DEGAS_AMU_CPU_STATE*a=&s.Amu.Cpus[i];Number("amu_windows_index",i);Number("amu_uid",a->Uid);Number("amu_status",a->Status);Number("amu_attempts",a->Attempts);Number("amu_measured_khz",a->MeasuredKhz);
      Number64("amu_mpidr",a->Mpidr);Number64("amu_pfr0",a->Pfr0);Number64("amu_enable",a->Enable);Number64("amu_reference_hz",a->ReferenceHz);
      Number64("amu_core_before",a->CoreBefore);Number64("amu_core_after",a->CoreAfter);Number64("amu_constant_before",a->ConstantBefore);Number64("amu_constant_after",a->ConstantAfter);Number64("amu_timer_before",a->TimerBefore);Number64("amu_timer_after",a->TimerAfter);
     }
    }
   }
   CloseHandle(dev);
  }
  if(gLog!=INVALID_HANDLE_VALUE)FlushFileBuffers(gLog);
  Sleep(1000);
 }
 Number("capture_complete",1);if(gLog!=INVALID_HANDLE_VALUE){FlushFileBuffers(gLog);CloseHandle(gLog);gLog=INVALID_HANDLE_VALUE;}
}
static void WINAPI Main(DWORD argc,WCHAR**argv){(void)argc;(void)argv;gStatus=RegisterServiceCtrlHandlerW(L"DegasDvfsLog",Control);if(!gStatus)return;State(SERVICE_RUNNING);Capture();State(SERVICE_STOPPED);}
void Entry(void){SERVICE_TABLE_ENTRYW t[]={{L"DegasDvfsLog",Main},{0,0}};if(!StartServiceCtrlDispatcherW(t)&&GetLastError()==ERROR_FAILED_SERVICE_CONTROLLER_CONNECT)Capture();ExitProcess(0);}
