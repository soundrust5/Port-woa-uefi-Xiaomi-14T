/* Explicit prototype OOBE bypass, online LocalSystem only. Account first,
 * verify membership, then complete setup. Never edits an offline SAM. */
#define UNICODE
#define _UNICODE
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <lm.h>
void *memset(void *p,int v,size_t n){volatile BYTE *q=p;while(n--)*q++=(BYTE)v;return p;}
void *memcpy(void *p,const void *s,size_t n){BYTE *q=p;const BYTE *r=s;while(n--)*q++=*r++;return p;}
static HANDLE Log=INVALID_HANDLE_VALUE;
static void Note(const char *event,DWORD code){char b[256];DWORD n;int count=wsprintfA(b,"%s=%lu\r\n",event,code);WriteFile(GetStdHandle(STD_OUTPUT_HANDLE),b,count,&n,0);if(Log!=INVALID_HANDLE_VALUE){WriteFile(Log,b,count,&n,0);FlushFileBuffers(Log);}}
static DWORD Set(const WCHAR *path,const WCHAR *name,DWORD type,const void *value,DWORD size){HKEY h;DWORD e=RegCreateKeyExW(HKEY_LOCAL_MACHINE,path,0,0,0,KEY_SET_VALUE,0,&h,0);if(e)return e;e=RegSetValueExW(h,name,0,type,value,size);if(!e)e=RegFlushKey(h);RegCloseKey(h);return e;}
static DWORD Dword(const WCHAR *path,const WCHAR *name,DWORD value){return Set(path,name,REG_DWORD,&value,4);}
static DWORD String(const WCHAR *path,const WCHAR *name,const WCHAR *value){return Set(path,name,REG_SZ,value,(lstrlenW(value)+1)*2);}
static BOOL System(void){BYTE buf[256],sid[SECURITY_MAX_SID_SIZE];DWORD n=sizeof buf,s=sizeof sid;HANDLE t;BOOL ok=FALSE;
 if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&t))return FALSE;
 if(GetTokenInformation(t,TokenUser,buf,sizeof buf,&n) && CreateWellKnownSid(WinLocalSystemSid,0,sid,&s))ok=EqualSid(((TOKEN_USER*)buf)->User.Sid,sid);CloseHandle(t);return ok;
}
static DWORD Account(void){USER_INFO_1 u,*existing=0;USER_INFO_1008 flags;USER_INFO_1003 password;DWORD parameter=0,status,read=0,total=0;BOOL found=FALSE,existed;
 BYTE sid[SECURITY_MAX_SID_SIZE];DWORD bytes=sizeof sid,n=256,d=256;WCHAR group[256],domain[256];SID_NAME_USE use;LOCALGROUP_MEMBERS_INFO_3 member;LOCALGROUP_USERS_INFO_0 *groups=0;
 ZeroMemory(&u,sizeof u);u.usri1_name=L"Xiaomi";u.usri1_password=L"";u.usri1_priv=USER_PRIV_USER;u.usri1_flags=UF_SCRIPT|UF_NORMAL_ACCOUNT|UF_DONT_EXPIRE_PASSWD|UF_PASSWD_NOTREQD;
 status=NetUserAdd(0,1,(BYTE*)&u,&parameter);Note("NetUserAdd",status);if(status && status!=NERR_UserExists)return status;
 existed=status==NERR_UserExists;
 status=NetUserGetInfo(0,L"Xiaomi",1,(BYTE**)&existing);if(status)return status;
 if(!(existing->usri1_flags&UF_NORMAL_ACCOUNT)){NetApiBufferFree(existing);return ERROR_INVALID_ACCOUNT_NAME;}
 flags.usri1008_flags=(existing->usri1_flags|UF_DONT_EXPIRE_PASSWD|UF_PASSWD_NOTREQD)&~UF_ACCOUNTDISABLE;NetApiBufferFree(existing);
 if(existed){password.usri1003_password=L"";status=NetUserSetInfo(0,L"Xiaomi",1003,(BYTE*)&password,&parameter);if(status)return status;}
 status=NetUserSetInfo(0,L"Xiaomi",1008,(BYTE*)&flags,&parameter);if(status)return status;
 if(!CreateWellKnownSid(WinBuiltinAdministratorsSid,0,sid,&bytes))return GetLastError();
 if(!LookupAccountSidW(0,sid,group,&n,domain,&d,&use))return GetLastError();
 member.lgrmi3_domainandname=L"Xiaomi";status=NetLocalGroupAddMembers(0,group,3,(BYTE*)&member,1);Note("Admin membership",status);
 if(status && status!=ERROR_MEMBER_IN_ALIAS)return status;
 status=NetUserGetLocalGroups(0,L"Xiaomi",0,LG_INCLUDE_INDIRECT,(BYTE**)&groups,MAX_PREFERRED_LENGTH,&read,&total);if(status)return status;
 for(DWORD i=0;i<read;i++)if(!lstrcmpiW(groups[i].lgrui0_name,group))found=TRUE;NetApiBufferFree(groups);
 return found?0:ERROR_ACCESS_DENIED;
}
void Entry(void){WCHAR windows[MAX_PATH],path[MAX_PATH],computer[MAX_COMPUTERNAME_LENGTH+1];DWORD e=0,n=MAX_COMPUTERNAME_LENGTH+1;HKEY h;HANDLE done;
 if(!System())ExitProcess(10);
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Control\\MiniNT",0,KEY_READ,&h)==0){RegCloseKey(h);ExitProcess(11);}
 if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SYSTEM\\CurrentControlSet\\Enum\\ACPI\\DGS0001",0,KEY_READ,&h))ExitProcess(12);RegCloseKey(h);
 if(!GetWindowsDirectoryW(windows,MAX_PATH) || lstrlenW(windows)>180)ExitProcess(13);
 lstrcpyW(path,windows);lstrcatW(path,L"\\Setup\\Scripts\\degas-finish-authorized.txt");if(GetFileAttributesW(path)==INVALID_FILE_ATTRIBUTES)ExitProcess(14);
 lstrcpyW(path,windows);lstrcatW(path,L"\\Setup\\Scripts\\DegasFinish.log");Log=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ,0,OPEN_ALWAYS,0,0);
 if(Log==INVALID_HANDLE_VALUE)ExitProcess(15);
 // SAM/RPC may still be starting during early setup. Retry for at most2min;
 // failure never clears setup flags or automatically reboots.
 for(DWORD attempt=0;attempt<60;attempt++){e=Account();if(!e)break;Note("Account retry error",e);Sleep(2000);}if(e)goto done;
 if(!GetComputerNameW(computer,&n)){e=GetLastError();goto done;}
#define TRY(call) do{e=(call);if(e){Note(#call,e);goto done;}}while(0)
 TRY(String(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"DefaultUserName",L"Xiaomi"));
 TRY(String(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"DefaultDomainName",computer));
 TRY(String(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"DefaultPassword",L""));
 TRY(String(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"AutoAdminLogon",L"1"));
 TRY(Dword(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"AutoLogonCount",1));
 TRY(String(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",L"Shell",L"explorer.exe"));
 TRY(String(L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Setup\\State",L"ImageState",L"IMAGE_STATE_COMPLETE"));
 TRY(Dword(L"SYSTEM\\Setup\\Status\\ChildCompletion",L"setup.exe",3));
 TRY(Dword(L"SYSTEM\\Setup",L"SetupPhase",0));
 TRY(Dword(L"SYSTEM\\Setup",L"OOBEInProgress",0));
 TRY(Dword(L"SYSTEM\\Setup",L"RestartSetup",0));
 TRY(Dword(L"SYSTEM\\Setup",L"AuditInProgress",0));
 TRY(Dword(L"SYSTEM\\Setup",L"Respecialize",0));
 TRY(Dword(L"SYSTEM\\Setup",L"SetupType",0));
 TRY(Dword(L"SYSTEM\\Setup",L"SystemSetupInProgress",0));
 TRY(String(L"SYSTEM\\Setup",L"CmdLine",L""));
 lstrcpyW(path,windows);lstrcatW(path,L"\\Setup\\Scripts\\DegasFinish.done");done=CreateFileW(path,GENERIC_WRITE,FILE_SHARE_READ,0,CREATE_ALWAYS,0,0);if(done!=INVALID_HANDLE_VALUE){FlushFileBuffers(done);CloseHandle(done);}
done:Note("finish result",e);if(Log!=INVALID_HANDLE_VALUE)CloseHandle(Log);ExitProcess(e);
}
