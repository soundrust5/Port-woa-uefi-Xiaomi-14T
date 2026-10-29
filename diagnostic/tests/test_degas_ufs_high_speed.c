#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define STATIC static
#define CONST const
#define VOID void
typedef uint32_t UINT32;typedef uint16_t UINT16;typedef unsigned long UINTN;typedef int BOOLEAN;
#define TRUE 1
#define FALSE 0
#define ARRAY_SIZE(a) (sizeof(a)/sizeof((a)[0]))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define DEBUG(x) ((void)0)
static UINT32 attrs[2][65536],is,arg1,arg2,arg3,mode_count,mode_values[2];
static int fail_attr,drop_power,bad_status,busy,drop_uic,readback_bad;
static UINT32 fatal;
static void MicroSecondDelay(UINT32 n){(void)n;}
static void MemoryFence(void){}
static UINT32 MmioRead32(UINT32 a){switch(a){
case 0x112b0030:return 15|(bad_status?0:0x100);
case 0x112b0058:return busy;case 0x112b0060:return 0;
case 0x112b0020:return is;case 0x112b0098:return arg2;case 0x112b009c:return arg3;
default:assert(0);return 0;}}
static void MmioWrite32(UINT32 a,UINT32 v){switch(a){
case 0x112b0020:is&=~v;break;case 0x112b0094:arg1=v;break;
case 0x112b0098:arg2=v;break;case 0x112b009c:arg3=v;break;
case 0x112b0090:{UINT32 at=arg1>>16,peer=v>=3;
assert(v>=1 && v<=4);if(!drop_uic)is|=0x400;is|=fatal;
if((int)at==fail_attr){arg2=1;break;}
if(v==1 || v==3){arg3=attrs[peer][at];if(readback_bad && mode_count==2 && at==0x1568)arg3=0;}else{
 attrs[peer][at]=arg3;
 if(at==0x1571){assert(mode_count<2);mode_values[mode_count++]=arg3;if(!drop_power)is|=0x10;}
}break;}
default:assert(0);}}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UfsHighSpeed.h"
static void setup(void){memset(attrs,0,sizeof(attrs));is=mode_count=0;fail_attr=-1;drop_power=bad_status=busy=drop_uic=readback_bad=0;fatal=0;
attrs[0][0x1587]=5;attrs[1][0x1587]=4;attrs[0][0x1581]=attrs[0][0x1561]=2;attrs[0][0x15a9]=5;}
int main(void){setup();assert(UfsHighSpeed());assert(mode_count==2 && mode_values[0]==0x44 && mode_values[1]==0x11);
assert(attrs[0][0x1583]==4 && attrs[0][0x1568]==4 && attrs[0][0x15d4]==1);
assert(attrs[1][0x15d6]==0x48 && attrs[0][0xd042]==65535);
setup();fail_attr=0x1552;assert(!UfsHighSpeed() && !mode_count);
setup();drop_power=1;assert(!UfsHighSpeed() && mode_count==1);
setup();bad_status=1;assert(!UfsHighSpeed() && mode_count==1);
setup();busy=1;assert(!UfsHighSpeed() && !mode_count);
setup();attrs[1][0x1587]=0;assert(!UfsHighSpeed() && !mode_count);
setup();attrs[0][0x1587]=attrs[1][0x1587]=3;assert(UfsHighSpeed() && attrs[0][0x15d4]==3);
setup();drop_uic=1;assert(!UfsHighSpeed() && !mode_count);
setup();fatal=0x800;assert(!UfsHighSpeed() && !mode_count);
setup();fatal=0x80;assert(!UfsHighSpeed() && !mode_count);
setup();readback_bad=1;assert(!UfsHighSpeed() && mode_count==2);
setup();attrs[0][0x1587]=attrs[1][0x1587]=7;assert(UfsHighSpeed() && attrs[0][0x1583]==5 && attrs[0][0x1568]==5);
puts("UFS HS sequencing/failure tests PASS");}
