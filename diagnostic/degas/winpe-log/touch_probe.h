/* Optional exact-device touch binding in the WinPE RAM environment.
 * The test certificate is pinned to the same verified UFS development key. */
static DWORD BindTouchDriver(void){
#if defined(_M_ARM64)
 HKEY key;DWORD value,size,type,result=0,i,got;static BYTE data[8192],hash[32];HANDLE f;
 HCERTSTORE store;STARTUPINFOW si;PROCESS_INFORMATION pi;
 const WCHAR *stores[]={L"ROOT",L"TrustedPublisher"};BOOL found=FALSE;
 WCHAR childCommand[]=L"X:\\Windows\\System32\\drvload.exe X:\\Windows\\System32\\DegasTouch\\degasvhf.inf";
 WCHAR command[]=L"X:\\Windows\\System32\\drvload.exe X:\\Windows\\System32\\DegasTouch\\degastouch.inf";
 if(Selftest)return ERROR_ACCESS_DENIED;
 if(Collect.touches==1 && Has(Collect.touch.id,L"ACPI\\DGS0002\\"))found=TRUE;
 if(!found)return ERROR_NOT_FOUND;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\partmgr\\Parameters",0,KEY_READ,&key))return ERROR_ACCESS_DENIED;
 size=4;result=RegQueryValueExW(key,L"SanPolicy",0,&type,(BYTE*)&value,&size);RegCloseKey(key);
 if(result || type!=REG_DWORD || size!=4 || value!=3)return ERROR_ACCESS_DENIED;
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Services\\mountmgr",0,KEY_READ,&key))return ERROR_ACCESS_DENIED;
 size=4;result=RegQueryValueExW(key,L"NoAutoMount",0,&type,(BYTE*)&value,&size);RegCloseKey(key);
 if(result || type!=REG_DWORD || size!=4 || value!=1)return ERROR_ACCESS_DENIED;
 f=CreateFileW(L"X:\\Windows\\System32\\DegasTouch\\development.cer",GENERIC_READ,FILE_SHARE_READ,0,OPEN_EXISTING,0,0);
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
 CloseHandle(pi.hProcess);Record(L"degastouch drvload exit=%lu\r\n",result);
 if(!result){
  /* WinPE omits the generic VHF child binding. The unchanged Microsoft binary
   * plus this phone-test INF supplies it, as in the proven OP12 PE image. */
  Sleep(500);memset(&pi,0,sizeof pi);
  if(!CreateProcessW(0,childCommand,0,0,FALSE,CREATE_NO_WINDOW,0,0,&si,&pi))TouchChildResult=GetLastError();
  else {
   CloseHandle(pi.hThread);
   if(WaitForSingleObject(pi.hProcess,90000)!=WAIT_OBJECT_0)TouchChildResult=WAIT_TIMEOUT;
   else if(!GetExitCodeProcess(pi.hProcess,&TouchChildResult))TouchChildResult=GetLastError();
   CloseHandle(pi.hProcess);
  }
  Record(L"degasvhf drvload exit=%lu\r\n",TouchChildResult);
 }
 return result;
#else
 return ERROR_NOT_SUPPORTED;
#endif
}
