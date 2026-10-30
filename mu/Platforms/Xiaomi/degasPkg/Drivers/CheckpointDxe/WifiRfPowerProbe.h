#include "RfPmicUpdate.h"
#include "../../../../../../diagnostic/degas/wifi/rf_power_core.h"
/* Reuse the already hardware-qualified CONN/pinmux transport callbacks. */
#include "WifiConnIo.h"
#include "WifiAdieAtf.h"
STATIC unsigned RfProbeRead(void *C,unsigned A,unsigned *V){
 DEGAS_PMIF_IO Io={NULL,PmicRead,PmicWrite,PmicDelay};(VOID)C;
 MmioWrite32(0x1c00a008,0x1971);return DegasPmicReadByte(&Io,4,A,V);
}
STATIC unsigned RfProbeUpdate(void *C,unsigned A,unsigned M,unsigned V){
 DEGAS_PMIF_IO Io={NULL,PmicRead,PmicWrite,PmicDelay};(VOID)C;
 DEBUG((DEBUG_ERROR,"DEGAS RF update address=%04x mask=%02x value=%02x\n",A,M,V));
 return DegasPmicRfUpdate(&Io,A,M,V);
}
STATIC void RfProbeLog(void *C,unsigned A,unsigned V,unsigned E){
 (VOID)C;DEBUG((DEBUG_ERROR,"DEGAS RF read address=%04x value=%02x error=%u\n",A,V,E));
}
STATIC VOID WifiInitProbe(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 WIFI_PROBE_CONTEXT C={0};DEGAS_CONN_RESULT Cr={0};DEGAS_RF_RESULT R={0};UINT32 State;CHAR8 Text[64];
 DEGAS_CONN_IO Ci={&C,WifiProbeRead,WifiProbeWrite,WifiProbeDelay,WifiProbeLog};
 DEGAS_RF_IO Ri={NULL,RfProbeRead,RfProbeUpdate,WifiProbeDelay,RfProbeLog};
 DEGAS_ADIE_RESULT Adie={0};DEGAS_ADIE_IO Ai={NULL,WifiAdieCall};
 BootWatchdogArmTenSeconds();Label(Gop,1900,"CHECKING RF POWER AND CONN");
 DEBUG((DEBUG_ERROR,"DEGAS RF ADIE BEGIN NATIVE ATF CONFIG AND SPI ID; NO AFE CAL FW DMA\n"));
 Cr.Restored=1;R.Restored=1;
 if(!WifiProbeRead(&C,CONN_CTL,&State) || (State&0xc000001fu)!=0x12)R.Code=2;
 else if(DegasAdieInitialize(&Ai,&Adie))DegasRfPrepare(&Ri,&R);
 else R.Code=4;
 if(R.Ready)DegasConnPowerProbeActive(&Ci,&Cr,WifiAdieActive,&Adie);
 if(R.Owned){
  if((!Cr.Owned || Cr.Restored) && WifiProbeRead(&C,CONN_CTL,&State) && (State&0xc000001fu)==0x12)DegasRfRestore(&Ri,&R);
  else{R.Code=3;R.Restored=0;}
 }
 DEBUG((DEBUG_ERROR,"DEGAS RF result=%u owned=%u ready=%u restored=%u conn_code=%u conn_owned=%u conn_restored=%u id=%08x ownership=%08x pins_restored=%u\n",R.Code,R.Owned,R.Ready,R.Restored,Cr.Code,Cr.Owned,Cr.Restored,Cr.Version,Cr.Ownership,Cr.PinsRestored));
 DEBUG((DEBUG_ERROR,"DEGAS ADIE result=%u op=%u id=%08x spi_id=%08x clock_off_failed=%u status=%016lx\n",Adie.Code,Adie.Stage,Adie.Id,Adie.ReadId,Adie.ClockOffFailed,Adie.LastStatus));
 Label(Gop,1900,(R.Code||!R.Restored||Cr.Code||!Cr.Restored||Adie.Code)?"ADIE STOPPED SEE LOG":"ADIE RF SPI ID PASS");
 AsciiSPrint(Text,sizeof(Text),"RF %u CONN %u ATF %u OP %u",R.Code,Cr.Code,Adie.Code,Adie.Stage);Label(Gop,2000,Text);
 AsciiSPrint(Text,sizeof(Text),"ADIE %08x SPI %08x",Adie.Id,Adie.ReadId);Label(Gop,2100,Text);
 if(R.Owned || Cr.Owned || Adie.Touched){
  BootWatchdogArmTenSeconds();
  DEBUG((DEBUG_ERROR,"DEGAS RF next reset=software delay_ms=3000\n"));
  MicroSecondDelay(3000000);
  DEBUG((DEBUG_ERROR,"DEGAS RF issuing software reset SWRST=1209\n"));
  DisableInterrupts();BootSoftwareReset();
 }
}
