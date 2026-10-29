// Exercise the selected, actual FrameBufferBltLib with a snapshot-based oracle.
#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

VOID *EFIAPI CopyMem(VOID *Destination,CONST VOID *Source,UINTN Length){
 return memmove(Destination,Source,Length);
}

#include "../../mu/Mu_Basecore/MdeModulePkg/Library/FrameBufferBltLib/FrameBufferBltLib.c"

enum { W=8,H=6,P=10,GUARD=64,WORDS=GUARD+P*H+GUARD };

static int check_copy(const char *name,UINTN sx,UINTN sy,UINTN dx,UINTN dy,
 UINTN width,UINTN height){
 UINT32 memory[WORDS],original[WORDS],expected[WORDS];
 FRAME_BUFFER_CONFIGURE config={0};
 for(UINTN i=0;i<WORDS;i++)memory[i]=0x8b000000U+(UINT32)i*0x101U;
 memcpy(original,memory,sizeof(memory));memcpy(expected,memory,sizeof(memory));
 // Copy from an immutable source snapshot: independent of copy direction.
 for(UINTN y=0;y<height;y++)for(UINTN x=0;x<width;x++)
  expected[GUARD+(dy+y)*P+dx+x]=original[GUARD+(sy+y)*P+sx+x];
 config.Width=W;config.Height=H;config.PixelsPerScanLine=P;
 config.BytesPerPixel=4;config.FrameBuffer=(UINT8 *)(memory+GUARD);
 if(FrameBufferBltLibVideoToVideo(&config,sx,sy,dx,dy,width,height)!=RETURN_SUCCESS){
  fprintf(stderr,"FAIL %s: valid rectangle rejected\n",name);return 0;
 }
 for(UINTN i=0;i<WORDS;i++)if(memory[i]!=expected[i]){
  fprintf(stderr,"FAIL %s: word %zu differs (surface starts %u, ends %u); actual=%08x expected=%08x\n",
   name,(size_t)i,GUARD,GUARD+P*H,memory[i],expected[i]);return 0;
 }
 return 1;
}

static int check_reject(const char *name,UINTN sx,UINTN sy,UINTN dx,UINTN dy,
 UINTN width,UINTN height){
 UINT32 memory[WORDS],original[WORDS];FRAME_BUFFER_CONFIGURE config={0};
 memset(memory,0xa5,sizeof(memory));memcpy(original,memory,sizeof(memory));
 config.Width=W;config.Height=H;config.PixelsPerScanLine=P;
 config.BytesPerPixel=4;config.FrameBuffer=(UINT8 *)(memory+GUARD);
 if(FrameBufferBltLibVideoToVideo(&config,sx,sy,dx,dy,width,height)!=RETURN_INVALID_PARAMETER ||
    memcmp(memory,original,sizeof(memory))){
  fprintf(stderr,"FAIL %s: invalid rectangle accepted or memory changed\n",name);return 0;
 }
 return 1;
}

int main(void){
 if(!check_copy("last visible row",0,0,0,H-1,W,1) ||
    !check_copy("downward overlapping rows",1,0,1,1,W-2,H-1) ||
    !check_copy("rightward same-row overlap",0,2,2,2,W-2,1) ||
    !check_copy("rightward overlap at bottom",0,H-1,2,H-1,W-2,1) ||
    !check_copy("upward overlapping rows",0,1,0,0,W,H-1) ||
    !check_copy("leftward same-row overlap",2,2,0,2,W-2,1) ||
    !check_copy("identical full rectangle",0,0,0,0,W,H) ||
    !check_reject("source X overflow",MAX_UINTN,0,0,0,2,1) ||
    !check_reject("source Y overflow",0,MAX_UINTN,0,0,1,2) ||
    !check_reject("destination X overflow",0,0,MAX_UINTN,0,2,1) ||
    !check_reject("destination Y overflow",0,0,0,MAX_UINTN,1,2) ||
    !check_reject("source edge exceeded",W-1,0,0,0,2,1) ||
    !check_reject("destination edge exceeded",0,0,0,H-1,1,2) ||
    !check_reject("zero width",0,0,0,0,0,1) ||
    !check_reject("zero height",0,0,0,0,1,0))return 1;
 puts("PASS: actual framebuffer library last-row, overlap in both axes, padding/canaries, overflow rejection");
 return 0;
}
