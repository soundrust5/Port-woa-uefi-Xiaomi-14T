/* Phone-only first driver test. All mutations are in WinPE RAM, never host. */
#include "ufs_expected.h"
static DWORD Crc(const BYTE *p,DWORD n){DWORD c=~0u,i;while(n--){c^=*p++;for(i=0;i<8;i++)c=(c>>1)^((0u-(c&1))&0xedb88320u);}return ~c;}
static void ReadCheck(HANDLE meta,const WCHAR *path,ULONGLONG size,DWORD block){
 SCSI_ADDRESS sc;DWORD got,first,last,error=0;BYTE *b;HANDLE h;LARGE_INTEGER pos;
 memset(&sc,0,sizeof sc);sc.Length=sizeof sc;
 if(!DeviceIoControl(meta,IOCTL_SCSI_GET_ADDRESS,0,0,&sc,sizeof sc,&got,0) || got<sizeof sc || sc.Lun>2)return;
 if(ReadMask&(1u<<sc.Lun))return;
 if(block!=4096 || size!=(sc.Lun==2?264509587456ULL:4194304ULL))return;
 h=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,0,OPEN_EXISTING,FILE_FLAG_NO_BUFFERING,0);
 if(h==INVALID_HANDLE_VALUE){ReadError=GetLastError();return;}
 b=VirtualAlloc(0,65536,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);
 if(!b){ReadError=GetLastError();CloseHandle(h);return;}
 if(!ReadFile(h,b,65536,&got,0))error=GetLastError();else if(got!=65536)error=ERROR_HANDLE_EOF;
 first=error?0:Crc(b,65536);
 pos.QuadPart=(LONGLONG)(size-32768);
 if(!error && !SetFilePointerEx(h,pos,0,FILE_BEGIN))error=GetLastError();
 if(!error){if(!ReadFile(h,b,32768,&got,0))error=GetLastError();else if(got!=32768)error=ERROR_HANDLE_EOF;}
 last=error?0:Crc(b,32768);
 if(!error && (first!=UfsFirstCrc[sc.Lun] || last!=UfsLastCrc[sc.Lun]))error=ERROR_CRC;
 Record(L"UFS READ LUN=%u firstCRC=%08lX lastCRC=%08lX error=%lu\r\n",sc.Lun,first,last,error);
 if(!error)ReadMask|=1u<<sc.Lun;else ReadError=error;
 VirtualFree(b,0,MEM_RELEASE);CloseHandle(h);
}
static DWORD BindDriver(void){
#if defined(_M_ARM64)
 HKEY key;DWORD value,size,type,result=0,i,got;static BYTE data[8192],hash[32];HANDLE f;
 HCERTSTORE store;STARTUPINFOW si;PROCESS_INFORMATION pi;
 const WCHAR *stores[]={L"ROOT",L"TrustedPublisher"};BOOL found=FALSE;
 WCHAR command[]=L"X:\\Windows\\System32\\drvload.exe X:\\Windows\\System32\\DegasUfs\\degasufs.inf";
 if(Selftest)return ERROR_ACCESS_DENIED;
 for(i=0;i<Collect.controllers && i<CONTROLLERS;i++)if(Has(Collect.controller[i].id,L"ACPI\\DGS0001\\"))found=TRUE;
 if(!found)return ERROR_NOT_FOUND;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\partmgr\\Parameters",0,KEY_READ,&key))return ERROR_ACCESS_DENIED;
 size=4;result=RegQueryValueExW(key,L"SanPolicy",0,&type,(BYTE*)&value,&size);RegCloseKey(key);
 if(result || type!=REG_DWORD || size!=4 || value!=3)return ERROR_ACCESS_DENIED;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\mountmgr",0,KEY_READ,&key))return ERROR_ACCESS_DENIED;
 size=4;result=RegQueryValueExW(key,L"NoAutoMount",0,&type,(BYTE*)&value,&size);RegCloseKey(key);
 if(result || type!=REG_DWORD || size!=4 || value!=1)return ERROR_ACCESS_DENIED;
 f=CreateFileW(L"X:\\Windows\\System32\\DegasUfs\\development.cer",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
 if(f==INVALID_HANDLE_VALUE)return GetLastError();
 if(GetFileSize(f,0)>sizeof data){CloseHandle(f);return ERROR_FILE_TOO_LARGE;}
 if(!ReadFile(f,data,sizeof data,&got,0)){result=GetLastError();CloseHandle(f);return result;}CloseHandle(f);
 size=sizeof hash;if(!CryptHashCertificate2(L"SHA256",0,0,data,got,hash,&size))return GetLastError();
 if(size!=32)return ERROR_INVALID_DATA;
 for(i=0;i<32;i++)if(hash[i]!=UfsCertSha256[i])return ERROR_INVALID_DATA;
 for(i=0;i<2;i++){
  store=CertOpenStore(CERT_STORE_PROV_SYSTEM_W,0,0,CERT_SYSTEM_STORE_LOCAL_MACHINE,stores[i]);if(!store)return GetLastError();
  if(!CertAddEncodedCertificateToStore(store,X509_ASN_ENCODING,data,got,CERT_STORE_ADD_REPLACE_EXISTING,0))result=GetLastError();
  CertCloseStore(store,0);if(result)return result;
 }
 memset(&si,0,sizeof si);memset(&pi,0,sizeof pi);si.cb=sizeof si;si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
 if(!CreateProcessW(0,command,0,0,FALSE,CREATE_NO_WINDOW,0,0,&si,&pi))return GetLastError();
 CloseHandle(pi.hThread);
 if(WaitForSingleObject(pi.hProcess,90000)!=WAIT_OBJECT_0)result=WAIT_TIMEOUT;
 else if(!GetExitCodeProcess(pi.hProcess,&result))result=GetLastError();
 CloseHandle(pi.hProcess);Record(L"degasufs drvload exit=%lu\r\n",result);return result;
#else
 return ERROR_NOT_SUPPORTED;
#endif
}
