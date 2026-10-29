/* Read-only handoff survey. Each potentially faulting read is preceded by
 * a persistent breadcrumb. No display MMIO writes and no clock changes.
 * Physical blocks corroborated by live MT6897 iomem and exact LK analysis.
 */
#define diagnostic_ramoops diagnostic_ramoops_base
#include "ramoops_probe.c"
#undef diagnostic_ramoops

static void line(const char *tag, uint64_t address, uint64_t value)
{
    char out[96];
    size_t n=text(out,0,tag);
    n=hex64(out,n,address);out[n++]=' ';
    n=hex64(out,n,value);out[n++]='\n';
    ramoops_console_append((volatile void *)(uintptr_t)CONSOLE_BASE,CONSOLE_LENGTH,out,n);
    __asm__ volatile("dsb sy" ::: "memory");
}

unsigned diagnostic_ramoops(const uint64_t *record)
{
    if(diagnostic_ramoops_base(record))return 1;
    /* Only control/state words used by the exact LK update chain. Do not
     * read command FIFO/data windows or infer that LK VA equals PA. */
    static const uint32_t addresses[]={
        0x1400d000,0x1400d010,0x1400d014,0x1400d060,
        0x1400d100,0x1400d104,0x1400d160,0x1400d164,
        0x14001000,0x14001008,0x14001020,0x14401020,
        /* Exact BL2 layer0 configuration, not a guessed scanout address. */
        0x14402038,0x14402044,0x14402f40,
        /* Mutex0 configuration retained from LK; inspect before trigger. */
        0x14001028,0x1400102c,0x14001030,0x14001034,
        0x14401028,0x1440102c,0x14401030,0x14401034,
        /* Dual-path counterparts from the exact LK branch. */
        0x1420d000,0x1420d010,0x1420d014,0x1420d060,
        0x1420d100,0x1420d104,0x1420d160,0x1420d164,
        0x14602038,0x14602044,0x14602f40,
        0x14201020,0x14201030,0x14201034,
        0x14601020,0x14601030,0x14601034,
        /* Candidate retained LK RAM: signature first, mode is usable only
         * if this signature matches the exact binary. Never call LK code. */
        0x50746e5c,0x50746e60,0x5082e570,0x5082e548,0x5082eaa8,
        0x1400d00c,0x1400d190
    };
    /* Capture framebuffer content first, even if later MMIO faults. */
    uint32_t hash=2166136261U,nonzero=0;
    line("FB_BEGIN ",0xfcc2f000,640*192);
    for(uint32_t y=0;y<192;y++)for(uint32_t x=0;x<640;x++){
        uint32_t v=*(volatile uint32_t *)(uintptr_t)(0xfcc2f000ULL+y*4992+x*4);
        hash=(hash^v)*16777619U;
        if(v)nonzero++;
    }
    line("FB_HASH_NONZERO ",hash,nonzero);
    for(unsigned i=0;i<sizeof(addresses)/sizeof(addresses[0]);i++){
        line("READ_BEGIN ",addresses[i],0);
        uint32_t value=*(volatile uint32_t *)(uintptr_t)addresses[i];
        line("READ_VALUE ",addresses[i],value);
    }
    line("DISPLAY_SNAPSHOT_DONE ",0,0);
    return 0;
}
