/* One command-mode frame using the verified single-DSI LK branch.
 * No panel power/PLL/timing changes. Watchdog remains armed. */
#define diagnostic_ramoops diagnostic_ramoops_base
#include "ramoops_probe.c"
#undef diagnostic_ramoops
static uint32_t rd(uint32_t a){return *(volatile uint32_t *)(uintptr_t)a;}
static void wr(uint32_t a,uint32_t v){*(volatile uint32_t *)(uintptr_t)a=v;__asm__ volatile("dsb sy" ::: "memory");}
static void logword(const char *s,uint64_t v){char b[96];size_t n=text(b,0,s);n=hex64(b,n,v);b[n++]='\n';ramoops_console_append((volatile void *)(uintptr_t)CONSOLE_BASE,CONSOLE_LENGTH,b,n);__asm__ volatile("dsb sy" ::: "memory");}
static int idle(void){for(unsigned i=0;i<100000;i++)if(!(rd(0x1400d00c)&0x80000000))return 1;return 0;}
unsigned diagnostic_ramoops(const uint64_t *record){
 if(diagnostic_ramoops_base(record))return 1;
 logword("FRAME_BEGIN ",1);
 if(rd(0x50746e5c)!=0xa9bd7bfd || rd(0x50746e60)!=0xf9000bf5 ||
    rd(0x5082e570)!=1 || rd(0x5082e548)!=0 || rd(0x5082eaa8)!=0 ||
    rd(0x14402f40)!=0xfcc2f000 || rd(0x14402044)!=4992 ||
    rd(0x14402038)!=0x0a98026d || rd(0x14602f40)!=0xfcc2f95c ||
    rd(0x14602044)!=4992 || rd(0x14602038)!=0x0a98026d ||
    rd(0x1400d014)!=0 || rd(0x1400d100)!=0x55 ||
    rd(0x14001030)!=0xbc7423ff || rd(0x14401030)!=0x8007){logword("FRAME_GUARD_FAIL ",1);return 1;}
 if(!idle()){logword("FRAME_BUSY_BEFORE ",rd(0x1400d00c));return 1;}
 /* 5x7 glyph rows, MEDIA TEK without gap. 8x scaling; safe central marker. */
 static const uint8_t glyphs[8][7]={
 {17,27,21,21,17,17,17},{31,16,16,30,16,16,31},
 {30,17,17,17,17,17,30},{31,4,4,4,4,4,31},
 {14,17,17,31,17,17,17},{31,4,4,4,4,4,4},
 {31,16,16,30,16,16,31},{17,18,20,24,20,18,17}};
 for(unsigned y=0;y<112;y++)for(unsigned x=0;x<480;x++){
  uint32_t color=0xffff9000;
  if(y>=28 && y<84 && x>=48 && x<432){unsigned g=(x-48)/48,c=((x-48)%48)/8,r=(y-28)/8;
   if(c<5 && (glyphs[g][r]&(1u<<(4-c))))color=0xffffffff;}
  wr(0xfcc2f000+(300+y)*4992+(370+x)*4,color);
 }
 logword("FRAME_PIXELS_WRITTEN ",480*112);
 /* Exact LK single-DSI start callback. */
 wr(0x1400d190,rd(0x1400d190)|2);wr(0x1400d190,rd(0x1400d190)|4);
 wr(0x1400d008,0);wr(0x1400d014,rd(0x1400d014)&~3u);
 wr(0x1400d100,0x55);wr(0x1400d104,rd(0x1400d104)|1);
 /* Scenario 3 mutex helper, mutex id 0; do not enable inactive DSI1. */
 wr(0x14001008,0);wr(0x14001020,1);wr(0x14401008,0);wr(0x14401020,1);
 if(!idle()){logword("FRAME_BUSY_QUEUE ",rd(0x1400d00c));return 1;}
 wr(0x1400dd00,0x002c3909);wr(0x1400d060,0x8001);
 wr(0x1400d000,rd(0x1400d000)&~1u);wr(0x1400d000,rd(0x1400d000)|1);
 logword("FRAME_TRIGGERED ",rd(0x1400d00c));
 logword("FRAME_IDLE_AFTER ",idle());
 logword("FRAME_STATUS_AFTER ",rd(0x1400d00c));
 logword("FRAME_DONE ",1);return 0;
}
