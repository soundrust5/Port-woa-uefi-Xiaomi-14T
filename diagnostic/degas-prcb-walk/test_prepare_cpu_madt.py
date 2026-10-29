"""Compile the actual current PrepareCpuMadt body with host firmware stubs."""
from pathlib import Path
import subprocess,tempfile
root=Path(__file__).resolve().parents[2]
src=(root/'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/DegasAcpiDxe.c').read_text()
body=src[src.index('STATIC VOID PrepareCpuMadt('):src.index('STATIC BOOLEAN TableValid(')]
base=root/'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe'
preamble=r'''
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef uint64_t UINT64;typedef uint32_t UINT32;typedef uint8_t UINT8;typedef size_t UINTN;typedef int BOOLEAN;typedef void VOID;
#define CONST const
#define STATIC static
#define TRUE 1
#define FALSE 0
#define MAX_UINT64 UINT64_MAX
#define ASSERT assert
#define DEBUG(x) do{}while(0)
#define CopyMem memcpy
#define ZeroMem(p,n) memset(p,0,n)
#define ARM_SMC_ID_PSCI_FEATURES 0x8400000a
#define ARM_SMC_ID_PSCI_CPU_ON_AARCH64 0xc4000003
#define ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64 0xc4000004
#define ARM_SMC_ID_PSCI_AFFINITY_INFO_ON 0
#define ARM_SMC_ID_PSCI_AFFINITY_INFO_OFF 1
typedef struct {UINT64 Arg0,Arg1,Arg2;} ARM_SMC_ARGS;
static UINT32 states[8],features;static int badGic,reverseGic;
static UINT64 ReadUnaligned64(CONST UINT64*p){UINT64 x;memcpy(&x,p,8);return x;}
static void WriteUnaligned64(UINT64*p,UINT64 x){memcpy(p,&x,8);}
static void WriteUnaligned32(UINT32*p,UINT32 x){memcpy(p,&x,4);}
static UINT32 read32(CONST UINT8*p){UINT32 x;memcpy(&x,p,4);return x;}
static UINT8 CalculateSum8(CONST UINT8*p,UINTN n){UINT8 s=0;while(n--)s+=*p++;return s;}
static UINT8 CalculateCheckSum8(CONST UINT8*p,UINTN n){return (UINT8)-CalculateSum8(p,n);}
static void ArmCallSmc(ARM_SMC_ARGS*s){if(s->Arg0==ARM_SMC_ID_PSCI_FEATURES)s->Arg0=features;else {assert(s->Arg0==ARM_SMC_ID_PSCI_AFFINITY_INFO_AARCH64);s->Arg0=states[s->Arg1>>8];}}
static UINT64 CpuReadGicr(VOID*c,UINT64 a){(void)c;UINT64 i=(a-0xc440008)/0x20000;assert(i<8);return (((badGic?0:(reverseGic && i?8-i:i))<<8)<<32)|(i==7?0x10:0);}
'''
preamble+='\n#include "'+str(base/'AcpiTablesGenerated.h')+'"\n#include "'+str(base/'CpuTopologyCore.h')+'"\n'
preamble+=r'''
static UINT8 mRuntimeMadt[sizeof(mDegasMadt)];static UINT32 mEnabledCpuCount;
static struct {UINT32 PublishedCount,DiscoveryStatus,PsciCpuOnFeature,StateMismatchMask;}mCpuStatus;
static struct {CONST UINT8*Data;}Tables[3];
'''
main=r'''
static void reset(void){memset(&mCpuStatus,0,sizeof(mCpuStatus));features=0;badGic=0;reverseGic=0;mEnabledCpuCount=1;Tables[2].Data=NULL;for(int i=0;i<8;i++)states[i]=(i==DEGAS_BOOT_CORE?0:1);}
int main(void){reset();PrepareCpuMadt();assert(mEnabledCpuCount==(DEGAS_BOOT_CORE==7?1:DEGAS_CPU_TEST_LIMIT));assert(Tables[2].Data==mRuntimeMadt);assert(!CalculateSum8(mRuntimeMadt,sizeof(mRuntimeMadt)));
for(int i=0;i<8;i++){UINT8*e=mRuntimeMadt+44+i*80;int enabled=DEGAS_BOOT_CORE==7?i==7:i<DEGAS_CPU_TEST_LIMIT;assert(read32(e+8)==(UINT32)i);assert(ReadUnaligned64((UINT64*)(e+68))==((UINT64)i<<8));assert(read32(e+12)==(UINT32)enabled);assert(ReadUnaligned64((UINT64*)(e+60))==(enabled?0xc440000ULL+i*0x20000ULL:0));if(DEGAS_BOOT_CORE==7){assert(read32(e+16)==0);assert(ReadUnaligned64((UINT64*)(e+24))==0);}}
for(int i=0;i<8;i++){reset();states[i]^=1;PrepareCpuMadt();if(DEGAS_BOOT_CORE==7){assert(!mEnabledCpuCount && !Tables[2].Data);assert(mCpuStatus.StateMismatchMask==(1U<<i));}else assert(mEnabledCpuCount==1);}
reset();reverseGic=1;PrepareCpuMadt();assert(mEnabledCpuCount==(DEGAS_BOOT_CORE==7?1:DEGAS_CPU_TEST_LIMIT));assert(ReadUnaligned64((UINT64*)(mRuntimeMadt+44+7*80+60))==((DEGAS_BOOT_CORE==7 || DEGAS_CPU_TEST_LIMIT==8)?0xc460000ULL:0));
reset();features=0xffffffff;PrepareCpuMadt();assert(mEnabledCpuCount==(DEGAS_BOOT_CORE==7?0:1));
reset();badGic=1;PrepareCpuMadt();assert(mEnabledCpuCount==(DEGAS_BOOT_CORE==7?0:1));
printf("actual PrepareCpuMadt core%d: success +8 affinity mismatches +feature failure +GIC failure PASS\n",DEGAS_BOOT_CORE);return 0;}
'''
with tempfile.TemporaryDirectory() as td:
 p=Path(td)/'test.c';p.write_text(preamble+body+main)
 for core,limit in ((0,8),(0,2),(0,3),(7,8)):
  exe=Path(td)/f'test{core}-{limit}'
  subprocess.run(['cc','-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-variable','-fsanitize=address,undefined',f'-DDEGAS_BOOT_CORE={core}',f'-DDEGAS_CPU_TEST_LIMIT={limit}',str(p),'-o',str(exe)],check=True)
  subprocess.run([str(exe)],check=True)
# ACPI namespace must contain UID7 and C007; compare actual generated AML bytes.
s=(base/'AcpiTablesGenerated.h').read_text();import re
arr=s.split('mDegasDsdt[] = {',1)[1].split('};',1)[0]
raw=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',arr))
assert b'C007' in raw and b'_UID\x0a\x07' in raw
print('actual generated DSDT C007 / UID7 PASS')
