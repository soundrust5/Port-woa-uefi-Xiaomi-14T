/* One command-mode frame using the verified single-DSI LK branch.
 * No panel power/PLL/timing changes. Watchdog remains armed. */
#define diagnostic_ramoops diagnostic_ramoops_base
#include "ramoops_probe.c"
#undef diagnostic_ramoops
static uint32_t rd(uint32_t a){return *(volatile uint32_t *)(uintptr_t)a;}
static void wr(uint32_t a,uint32_t v){*(volatile uint32_t *)(uintptr_t)a=v;__asm__ volatile("dsb sy" ::: "memory");}
static void logword(const char *s,uint64_t v){char b[96];size_t n=text(b,0,s);n=hex64(b,n,v);b[n++]='\n';ramoops_console_append((volatile void *)(uintptr_t)CONSOLE_BASE,CONSOLE_LENGTH,b,n);__asm__ volatile("dsb sy" ::: "memory");}
static int idle(void){for(unsigned i=0;i<100000;i++)if(!(rd(0x1400d00c)&0x80000000))return 1;return 0;}

static void box(unsigned x,unsigned y,unsigned w,unsigned h,uint32_t c){
 if(x>=1220 || y>=2712 || w>1220-x || h>2712-y)return;
 for(unsigned j=y;j<y+h;j++)for(unsigned i=x;i<x+w;i++)
  *(volatile uint32_t *)(uintptr_t)(0xfcc2f000ULL+j*4992+i*4)=c;
}
static const uint8_t letters[26][7]={
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{15,16,16,16,16,16,15},
{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
{15,16,16,19,17,17,15},{17,17,17,31,17,17,17},{31,4,4,4,4,4,31},
{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},
{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},
{17,17,10,4,4,4,4},{31,1,2,4,8,16,31}};
static void label(unsigned x,unsigned y,unsigned scale,const char *s,uint32_t c){
 for(;*s;s++,x+=6*scale)if(*s>='A' && *s<='Z')
  for(unsigned r=0;r<7;r++)for(unsigned col=0;col<5;col++)
   if(letters[*s-'A'][r]&(1u<<(4-col)))box(x+col*scale,y+r*scale,scale,scale,c);
}
static void screen(void){
 box(0,0,1220,2712,0xff000000);
 label(100,300,9,"MEDIATEK",0xffffffff);
 __asm__ volatile("dsb sy" ::: "memory");
}

unsigned diagnostic_ramoops(const uint64_t *record){
 if(diagnostic_ramoops_base(record))return 1;
 logword("FRAME_BEGIN ",1);
 if(rd(0x50746e5c)!=0xa9bd7bfd || rd(0x50746e60)!=0xf9000bf5 ||
    rd(0x5082e570)!=1 || rd(0x5082e548)!=0 || rd(0x5082eaa8)!=0 ||
    rd(0x14402f40)!=0xfcc2f000 || rd(0x14402044)!=4992 ||
    rd(0x14402038)!=0x0a98026d || rd(0x14602f40)!=0xfcc2f95c ||
    rd(0x14602044)!=4992 || rd(0x14602038)!=0x0a98026d ||
    rd(0x1400d014)!=0 || rd(0x1400d100)!=0x55 ||
    rd(0x14001030)!=0xbc7423ff || rd(0x14401030)!=0x8007 ||
    rd(0x14201030)!=0x9c6083ff || rd(0x14601030)!=0x8007){logword("FRAME_GUARD_FAIL ",1);return 1;}
 if(!idle()){logword("FRAME_BUSY_BEFORE ",rd(0x1400d00c));return 1;}
 screen();
 logword("FULLSCREEN_WRITTEN ",1220*2712);
 /* Exact LK single-DSI start callback. */
 wr(0x1400d190,rd(0x1400d190)|2);wr(0x1400d190,rd(0x1400d190)|4);
 wr(0x1400d008,0);wr(0x1400d014,rd(0x1400d014)&~3u);
 wr(0x1400d100,0x55);wr(0x1400d104,rd(0x1400d104)|1);
 /* Test LK scenario18 mutex helper on both configured pipes, id0.
  * This changes only mutex activation; DSI remains single, mode1. */
 wr(0x14201008,0);wr(0x14201020,1);wr(0x14601008,0);wr(0x14601020,1);
 wr(0x14001008,0);wr(0x14001020,1);wr(0x14401008,0);wr(0x14401020,1);
 if(!idle()){logword("FRAME_BUSY_QUEUE ",rd(0x1400d00c));return 1;}
 wr(0x1400dd00,0x002c3909);wr(0x1400d060,0x8001);
 wr(0x1400d000,rd(0x1400d000)&~1u);wr(0x1400d000,rd(0x1400d000)|1);
 logword("FRAME_TRIGGERED ",rd(0x1400d00c));
 logword("FRAME_IDLE_AFTER ",idle());
 logword("FRAME_STATUS_AFTER ",rd(0x1400d00c));
 logword("FRAME_DONE ",1);
 if(record[14]!=0x40000000 || record[10]<0x44480000){logword("MU_HANDOFF_GUARD_FAIL ",record[10]);return 1;}
 logword("MU_SEC_JUMP ",0x40200000);
 __asm__ volatile("dsb sy; isb" ::: "memory");
 ((void (*)(void))(uintptr_t)0x40200000)();
 logword("MU_SEC_RETURNED ",1);return 1;
}
