// Bound this experimental boot attempt, including disk-read watchdog reloads.
STATIC BOOLEAN mBootBudgetActive;
STATIC UINT64 mBootBudgetStart;
STATIC VOID BootWatchdogFeed(VOID){
 if(mBootBudgetActive &&
    GetTimeInNanoSecond(GetPerformanceCounter()-mBootBudgetStart)>=60000000000ULL)return;
 MmioWrite32(0x1c00a008,0x1971);
}

STATIC VOID BootWatchdogArmTenSeconds(VOID){
 UINT32 Mode=MmioRead32(0x1c00a000);
 // mtk_wdt_set_timeout: seconds * 64 ticks << 5, key8. Reset-only mode.
 MmioWrite32(0x1c00a004,(10u<<11)|8u);
 MmioWrite32(0x1c00a000,0x22000000u|((Mode&0x00ffffffu)&~0x48u)|1u);
 MmioWrite32(0x1c00a008,0x1971);
 DEBUG((DEBUG_ERROR,"DEGAS RESET TIMEOUT seconds=10 mode=%08x length=%08x\n",MmioRead32(0x1c00a000),MmioRead32(0x1c00a004)));
}

// Exact MT6897 DT watchdog base and mtk_wdt_restart(): SWRST0x14/key0x1209.
// No logging here: BootAndroid's console request must remain the final bytes.
// The already armed reset-only watchdog remains a fallback if SWRST is ignored.
STATIC VOID BootSoftwareReset(VOID){
 for(;;){MmioWrite32(0x1c00a014,0x1209);MemoryFence();MicroSecondDelay(5000);}
}
