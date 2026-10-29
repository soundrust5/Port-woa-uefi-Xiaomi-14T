// Host execution of the actual firmware snapshot; no writable display changes.
#define _GNU_SOURCE
#include <assert.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uintptr_t UINTN;
typedef char CHAR8;
#define STATIC static
#define CONST const
#define VOID void
static unsigned char Output[140000];
static size_t Used;
static UINTN AsciiSPrint(char *b,UINTN n,const char *f,...){
 va_list a;va_start(a,f);int r=vsnprintf(b,n,f,a);va_end(a);
 assert(r>=0 && (UINTN)r<n);return (UINTN)r;
}
static UINTN SerialPortWrite(UINT8 *p,UINTN n){
 assert(n<=sizeof(Output)-Used);memcpy(Output+Used,p,n);Used+=n;return n;
}
#include "../../mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/FrameThumbnail.h"
int main(int argc,char **argv){
 const size_t bytes=0xcea000;
 assert(argc==2);
 uint32_t *fb=mmap((void *)0xfcc2f000,bytes,PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(fb==(void *)0xfcc2f000);
 memset(fb,0,bytes);
 for(unsigned y=0;y<2712;y++)for(unsigned x=1220;x<1248;x++)
  fb[y*1248+x]=0xdeadbeef; // Padding must not enter the image.
 fb[0]=0x00ff0000;fb[2711*1248+1219]=0xff0000ff;
 unsigned char *before=malloc(bytes);assert(before);memcpy(before,fb,bytes);
 // Prove the firmware routine only reads the framebuffer, including padding.
 assert(mprotect(fb,bytes,PROT_READ)==0);
 FrameThumbnail();
 assert(memcmp(before,fb,bytes)==0 && Used<140000);
 FILE *f=fopen(argv[1],"wb");assert(f);
 assert(fwrite(Output,1,Used,f)==Used);assert(fclose(f)==0);
 free(before);assert(munmap(fb,bytes)==0);
 printf("Frame thumbnail PASS: %zu bytes, read-only FB including padding\n",Used);
 return 0;
}
