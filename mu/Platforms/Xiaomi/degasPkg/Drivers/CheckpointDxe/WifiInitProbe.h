#include "WifiConnIo.h"
STATIC VOID WifiInitProbe(EFI_GRAPHICS_OUTPUT_PROTOCOL *Gop){
 WIFI_PROBE_CONTEXT C={0};DEGAS_CONN_RESULT R;CHAR8 Text[64];
 DEGAS_CONN_IO Io={&C,WifiProbeRead,WifiProbeWrite,WifiProbeDelay,WifiProbeLog};
 BootWatchdogArmTenSeconds();Label(Gop,1900,"CHECKING CONN POWER AND ID");
 DEBUG((DEBUG_ERROR,"DEGAS WIFI begin SCPSYS CONN ONLY NO PMIC SMC RF DMA FIRMWARE\n"));
 DegasConnPowerProbe(&Io,&R);
 DEBUG((DEBUG_ERROR,"DEGAS WIFI result=%u stage=%u owned=%u restored=%u writes=%u cfg=%08x before=%08x after=%08x\n",R.Code,R.Stage,R.Owned,R.Restored,R.Writes,R.Config,R.Before,R.After));
 DEBUG((DEBUG_ERROR,"DEGAS WIFI snapshot en=%08x/%08x protection=%08x/%08x mirror=%08x/%08x version=%08x ownership=%08x\n",R.Enable[0],R.Enable[1],R.Protect[0],R.Protect[1],R.Mirror[0],R.Mirror[1],R.Version,R.Ownership));
 DEBUG((DEBUG_ERROR,"DEGAS WIFI pins owned=%u restored=%u before=%08x/%08x/%08x/%08x\n",R.PinsOwned,R.PinsRestored,R.PinsBefore[0],R.PinsBefore[1],R.PinsBefore[2],R.PinsBefore[3]));
 Label(Gop,1900,!R.Restored?"CONN CLEANUP FAILED RESET":R.Code?"CONN INIT STOPPED SEE LOG":"CONN POWER AND ID PASS");
 AsciiSPrint(Text,sizeof(Text),"CODE %u STAGE %u ID %08x",R.Code,R.Stage,R.Version);Label(Gop,2000,Text);
 if(R.Owned || !R.Restored){
  DEBUG((DEBUG_ERROR,"DEGAS WIFI active test requires reset; cleanup means OFF AND PROTECTED, not exact LK EN restoration\n"));
  BootWatchdogArmTenSeconds();CpuDeadLoop();
 }
}
