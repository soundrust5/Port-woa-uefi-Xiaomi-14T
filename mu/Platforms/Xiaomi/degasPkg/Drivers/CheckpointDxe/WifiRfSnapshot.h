/* MT6363 SID4 observation only. PMIF transaction setup writes are not PMIC
 * writes; no regulator enable, voltage, mode, RF or ATF operations occur. */
STATIC VOID WifiInitProbe(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 STATIC CONST UINT16 Addresses[]={0x1b3f,0x1bcd,0x1bd1,0x1bd2,0x1bd4,
  0x1d07,0x1d0b,0x1d14,0x1d16,0x1f2b,0x1f2c,0x1f88};
 DEGAS_PMIF_IO Io={NULL,PmicRead,PmicWrite,PmicDelay};UINT32 I,V,Error=0;
 BootWatchdogArmTenSeconds();
 DEBUG((DEBUG_ERROR,"DEGAS WIFI RF SNAPSHOT SID4 READ ONLY NO PMIC WRITES\n"));
 for(I=0;I<ARRAY_SIZE(Addresses);I++){
  MmioWrite32(0x1c00a008,0x1971);
  V=0;
  Error=DegasPmicReadByte(&Io,4,Addresses[I],&V);
  DEBUG((DEBUG_ERROR,"DEGAS WIFI RF address=%04x value=%02x error=%u\n",Addresses[I],V,Error));
  if(Error)break;
 }
 Label(Gop,1900,Error?"RF SNAPSHOT FAILED SEE LOG":"RF SNAPSHOT SAVED");
}
