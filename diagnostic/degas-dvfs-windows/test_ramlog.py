"""Compile the actual RAM callback with isolated resident MMIO/kernel substitutes."""
from pathlib import Path
import json,subprocess
r=Path(__file__).resolve().parents[2];s=r/'diagnostic/degas-dvfs-windows';o=r/'artifacts/degas/dvfs-windows/ramlog-test';o.mkdir(parents=True,exist_ok=True)
prefix=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <wchar.h>
#include <assert.h>
#include <stdio.h>
typedef uint8_t UCHAR,BOOLEAN,*PUCHAR; typedef uint32_t ULONG,*PULONG; typedef uint64_t ULONGLONG; typedef uintptr_t ULONG_PTR; typedef int32_t LONG,NTSTATUS; typedef int64_t LONG64; typedef void VOID,*PVOID; typedef struct {long long QuadPart;} PHYSICAL_ADDRESS; typedef struct {const wchar_t *Buffer;} UNICODE_STRING; typedef struct {int initialized;} KBUGCHECK_CALLBACK_RECORD;
typedef void KBUGCHECK_CALLBACK_ROUTINE(PVOID,ULONG);
#define TRUE 1
#define FALSE 0
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xc00000a3)
#define STATUS_INSUFFICIENT_RESOURCES ((NTSTATUS)0xc000009a)
#define STATUS_NOT_SUPPORTED ((NTSTATUS)0xc00000bb)
#define STATUS_SUCCESS 0
#define PAGE_READWRITE 4
#define PAGE_NOCACHE 0x200
#define UNREFERENCED_PARAMETER(x) (void)(x)
static ULONG zone[1024],watchdog[1024]; static ULONG_PTR bugs[5]; static int data_ok=1,registered_ok=1,external_calls; static KBUGCHECK_CALLBACK_ROUTINE *callback;
static LONG64 InterlockedIncrement64(volatile LONG64*p){return ++*p;} static LONG InterlockedExchange(volatile LONG*p,LONG n){LONG old=*p;*p=n;return old;} static LONG InterlockedCompareExchange(volatile LONG*p,LONG n,LONG cmp){LONG old=*p;if(old==cmp)*p=n;return old;}
static void KeMemoryBarrier(void){__sync_synchronize();}
static void* MmMapIoSpaceEx(PHYSICAL_ADDRESS p,size_t n,int flags){external_calls++;(void)n;(void)flags;return p.QuadPart==0x480ae000?zone:watchdog;}
static void RtlInitUnicodeString(UNICODE_STRING*s,const wchar_t*p){external_calls++;s->Buffer=p;}
static void* MmGetSystemRoutineAddress(UNICODE_STRING*s){external_calls++;if(!wcscmp(s->Buffer,L"KiBugCheckData"))return data_ok?bugs:NULL;return (void*)((uintptr_t)bugs-0xb5e1e0);}
static int MmIsAddressValid(void*p){external_calls++;return p!=NULL;}
static uint64_t KeQueryInterruptTime(void){external_calls++;return 123456;}
static void KeInitializeCallbackRecord(KBUGCHECK_CALLBACK_RECORD*p){external_calls++;p->initialized=1;}
static int KeRegisterBugCheckCallback(KBUGCHECK_CALLBACK_RECORD*r,KBUGCHECK_CALLBACK_ROUTINE*f,void*b,ULONG n,PUCHAR id){external_calls++;assert(r->initialized && !b && !n && id);callback=f;return registered_ok;}
#define READ_REGISTER_ULONG(p) (*(p))
#define WRITE_REGISTER_ULONG(p,v) (*(p)=(v))
'''
body=(s/'degas_ramlog.c').read_text().replace('#include "degas_ramlog.h"','')
cases=r'''
static uint64_t rd64(const UCHAR*p){uint64_t v=0;for(int i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static void verify(void){UCHAR*p=(UCHAR*)zone,*b=p+64;assert(zone[0]==0x43474244 && zone[1]==0x7f4 && zone[2]==0x7f4);assert(!memcmp(p+12,"====0.000000-D\n",15));assert(!memcmp(b,"DGBUG001",8));ULONG stored;memcpy(&stored,b+240,4);assert(stored==Crc(b+8,232));ULONG inv;memcpy(&inv,b+244,4);assert((stored^inv)==0xffffffff);for(int i=512;i<1024;i++)assert(zone[i]==0xa55a1234);}
int main(void){for(int i=0;i<1024;i++)zone[i]=0xa55a1234;assert(DegasRamInitialize()==STATUS_SUCCESS);verify();assert(gDataValid && gRegistered);DegasRamMark(0x10,3,0x12345678);verify();assert(rd64((UCHAR*)zone+64+32)==0x12345678);bugs[0]=0xa0;bugs[1]=0x605;bugs[2]=3;bugs[3]=0xfeed;bugs[4]=0;int before=external_calls;callback(NULL,0);assert(before==external_calls);verify();for(int i=0;i<5;i++)assert(rd64((UCHAR*)zone+64+40+i*8)==bugs[i]);for(int i=0;i<1024;i++)assert(watchdog[i]==0);ULONG saved[1024];memcpy(saved,zone,sizeof zone);DegasRamMark(99,99,99);assert(!memcmp(saved,zone,sizeof zone));gCrashing=0;gWriter=0;data_ok=0;assert(DegasRamInitialize()==STATUS_NOT_SUPPORTED);verify();before=external_calls;callback(NULL,0);assert(before==external_calls);verify();for(int i=0;i<5;i++)assert(rd64((UCHAR*)zone+64+40+i*8)==0);puts("PASS actual RAM callback: CRC, exact parameters, no external calls during crash, watchdog untouched, route flag preserved, post-crash marks blocked, missing export rejected");}
'''
(o/'unit.c').write_text(prefix+body+cases)
subprocess.run(['wsl','-d','Ubuntu','--','bash','-c','clang -std=c11 -Wall -Wextra -Werror -fsanitize=undefined,address artifacts/degas/dvfs-windows/ramlog-test/unit.c -o artifacts/degas/dvfs-windows/ramlog-test/test && artifacts/degas/dvfs-windows/ramlog-test/test'],cwd=r,check=True)
(o/'result.json').write_text(json.dumps(dict(passed=True,actual_source=True,host_kernel_and_mmio_substitutes=True),indent=2)+'\n')
