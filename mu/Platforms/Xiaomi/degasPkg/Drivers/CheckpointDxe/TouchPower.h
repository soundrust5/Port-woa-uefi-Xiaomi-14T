/* MT6368 VTP: SID5, enable1c87 bit0, selector1da5 low nibble4=1.8V.
 * Bounded power sequence; voltage selector/calibration are never written.
 * MiCode mt6368-regulator.h and live degas DT phandle2a8.
 */
STATIC VOID TouchPowerSnapshot(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 DEGAS_PMIF_IO Io={NULL,PmicRead,PmicWrite,PmicDelay};
 STATIC CONST UINT32 Addresses[]={0x1c87,0x1da4,0x1da5};
 UINT32 Values[3]={0},Error=0;UINTN I;
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH VTP BEGIN SID5 READ ONLY\n"));
 for(I=0;I<ARRAY_SIZE(Addresses);I++){
  Error=DegasPmicReadByte(&Io,5,Addresses[I],&Values[I]);
  DEBUG((DEBUG_ERROR,"DEGAS TOUCH VTP addr=%04x value=%02x error=%u\n",Addresses[I],Values[I],Error));
  if(Error)break;
 }
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH VTP END error=%u\n",Error));
 if(Error || (Values[2]&15)!=4 || (Values[1]&15)!=0 ||
    ((MmioRead32(0x10005370)>>16)&15)!=0 ||
    ((MmioRead32(0x10005390)>>4)&15)!=0 ||
    !(MmioRead32(0x10005010)&(1u<<28)) ||
    !(MmioRead32(0x10005020)&(1u<<9)) ||
    (MmioRead32(0x10005110)&(1u<<28)) ||
    (MmioRead32(0x10005120)&(1u<<9))){
  Label(Gop,1690,"TOUCH POWER PRECONDITION FAILED");return;
 }
 // Reset is already asserted. Match Android's >=2ms before power sequence.
 MicroSecondDelay(2100);
 Error=DegasPmicVtpEnable(&Io,1);
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH POWER iovdd_enable=%u\n",Error));
 if(!Error){
  MicroSecondDelay(1000); // DT enable-ramp-delay680us, rounded upward.
  MmioWrite32(0x10005124,1u<<9); // GPIO73 DOUT SET: AVDD enable.
  ArmDataSynchronizationBarrier();MicroSecondDelay(510);
  if(!(MmioRead32(0x10005120)&(1u<<9)))Error=7;
 }
 if(!Error){
  MmioWrite32(0x10005114,1u<<28); // GPIO60 DOUT SET: release reset.
  ArmDataSynchronizationBarrier();MicroSecondDelay(100000);
  if(!(MmioRead32(0x10005110)&(1u<<28)))Error=8;
 }
 DEBUG((DEBUG_ERROR,"DEGAS TOUCH POWER active error=%u reset=%08x avdd=%08x irq=%08x\n",
  Error,MmioRead32(0x10005210),MmioRead32(0x10005220),MmioRead32(0x10005200)));
 if(!Error)Error=TouchSpiIdentify(Gop);
 // Restore only the two GPIO bits and VTP enable bit we owned.
 MmioWrite32(0x10005118,1u<<28);ArmDataSynchronizationBarrier();MicroSecondDelay(2100);
 MmioWrite32(0x10005128,1u<<9);ArmDataSynchronizationBarrier();MicroSecondDelay(1000);
 {
  UINT32 Cleanup=DegasPmicVtpEnable(&Io,Values[0]&1);
  if((MmioRead32(0x10005110)&(1u<<28)) || (MmioRead32(0x10005120)&(1u<<9)))Cleanup=9;
  DEBUG((DEBUG_ERROR,"DEGAS TOUCH POWER cleanup=%u initial_en=%u\n",Cleanup,Values[0]&1));
  Label(Gop,1690,Cleanup?"TOUCH CLEANUP FAILED":Error?"TOUCH IDENTIFY FAILED":"TOUCH CHIP ID 3683 VERIFIED");
 }
}
