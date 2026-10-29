#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/SerialPortLib.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
static BOOLEAN enabled=TRUE,pending;
static UINTN fences;
BOOLEAN EFIAPI SaveAndDisableInterrupts(VOID){BOOLEAN old=enabled;enabled=FALSE;return old;}
BOOLEAN EFIAPI SetInterruptState(BOOLEAN state){
 enabled=state;
 if(enabled&&pending){pending=FALSE;assert(SerialPortWrite((UINT8 *)"IRQ",3)==3);}
 return state;
}
VOID EFIAPI MemoryFence(VOID){assert(!enabled);fences++;}
#include "../../mu/Platforms/Xiaomi/degasPkg/Library/RamSerialPortLib/RamSerialPortLib.c"
int main(void){
 UINT32 *h=mmap((void *)0x480af000,0x40000,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0);
 assert(h==(void *)0x480af000);UINT8 *d=(UINT8 *)h+12;UINT32 cap=0x40000-12;
 h[0]=0x43474244;h[1]=h[2]=0;pending=TRUE;
 assert(SerialPortWrite((UINT8 *)"OUTER",5)==5);
 assert(enabled&&!pending&&h[1]==8&&h[2]==8&&memcmp(d,"OUTERIRQ",8)==0&&fences==4);
 enabled=FALSE;assert(SerialPortWrite((UINT8 *)"X",1)==1&&!enabled);
 h[1]=cap-2;h[2]=cap;enabled=TRUE;
 assert(SerialPortWrite((UINT8 *)"ABCD",4)==4&&enabled);
 assert(h[1]==2&&h[2]==cap&&d[cap-2]=='A'&&d[cap-1]=='B'&&d[0]=='C'&&d[1]=='D');
 h[1]=cap;assert(SerialPortWrite((UINT8 *)"bad",3)==0&&enabled);
 enabled=FALSE;assert(SerialPortWrite((UINT8 *)"bad",3)==0&&!enabled);
 assert(SerialPortWrite(NULL,9)==0&&!enabled);
 munmap(h,0x40000);
 puts("PASS: RAM serial commits before pending IRQ, preserves masked IRQ, wraps, rejects invalid header");
}
