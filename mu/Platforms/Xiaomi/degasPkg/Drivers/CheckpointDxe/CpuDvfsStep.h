// One menu attempt, domain0 only. Stock CSRAM protocol; no PLL/PMIC writes.
typedef struct {UINT64 Raw[9];UINT32 Rate,Error;} DVFS_CLOCK_RAW;
STATIC DVFS_CLOCK_RAW mClockTrace[5][128];
STATIC UINT32 mClockTraceCount[5],mClockPhase,mClockError,mClockReq[5],mClockReady[5];
STATIC UINT32 mClockPhaseError[5];
STATIC UINT64 mClockPhaseStart,mClockBudget;
STATIC BOOLEAN DvfsClockSamples(UINT32 ExpectedKhz,UINT32 *Measured){
 UINT64 P[9],First[5]={0},Cycles,Ticks,Khz;UINT32 I,J;UINT64 Sum=0,Start=GetPerformanceCounter();
 DVFS_CLOCK_RAW *Trace;
#define CLOCK_REJECT(Code) do{mClockError=Code;Trace->Error=Code;return FALSE;}while(0)
 for(I=0;I<4;I++){
  Trace=&mClockTrace[mClockPhase][mClockTraceCount[mClockPhase]++%128];ZeroMem(Trace,sizeof(*Trace));
  if(ArmReadCurrentEL()!=4 || (ArmReadMpidr()&0xff00ffffffULL))CLOCK_REJECT(1);
  ZeroMem(P,sizeof(P));
  J=DegasDvfsPmuRead(P);CopyMem(Trace->Raw,P,sizeof(P));
  if(J || ArmReadCurrentEL()!=4 || (ArmReadMpidr()&0xff00ffffffULL))CLOCK_REJECT(2);
  if(GetTimeInNanoSecond(GetPerformanceCounter()-Start)>150000000ULL)CLOCK_REJECT(3);
  if(GetTimeInNanoSecond(GetPerformanceCounter()-mClockPhaseStart)>mClockBudget)CLOCK_REJECT(8);
  if(P[0]!=0x000011f010305619ULL || P[1]!=0x3041 || P[2]!=0x80000000 || P[3] || P[4]!=13000000)CLOCK_REJECT(4);
  if(!I)CopyMem(First,P,sizeof(First));
  else for(J=0;J<5;J++)if(P[J]!=First[J])CLOCK_REJECT(5);
  if(I==3)break; // Metadata reread after the three measured windows.
  Ticks=P[7]-P[5];Cycles=P[8]-P[6];
  if(Ticks<104000 || Ticks>260000 || Cycles>100000000)CLOCK_REJECT(6);
  Khz=Cycles*13000/Ticks;
  Trace->Rate=(UINT32)Khz;*Measured=(UINT32)Khz;
  if(Khz<ExpectedKhz*975ULL/1000 || Khz>ExpectedKhz*1025ULL/1000)CLOCK_REJECT(7);
  Sum+=Khz;
 }
 *Measured=(UINT32)(Sum/3);mClockError=0;return TRUE;
#undef CLOCK_REJECT
}
STATIC BOOLEAN DvfsWaitClock(UINT32 Target,UINT32 *Rate,UINT32 Phase){
 UINT32 Attempt;BOOLEAN Ok=FALSE;
 mClockPhase=Phase;
 for(Attempt=0;Attempt<32;Attempt++){
  mClockReq[Phase]=MmioRead32(0x11bc98);mClockReady[Phase]=MmioRead32(0x11bc9c);
  if(GetTimeInNanoSecond(GetPerformanceCounter()-mClockPhaseStart)>mClockBudget){mClockError=8;break;}
  if(mClockReq[Phase]!=Target || (mClockReady[Phase]&3)!=3){mClockError=9;MicroSecondDelay(1000);continue;}
  if(DvfsClockSamples(Target,Rate)){Ok=TRUE;break;}
  if(mClockError!=7)break; // Only target mismatch is eligible for settling.
 }
 mClockReq[Phase]=MmioRead32(0x11bc98);mClockReady[Phase]=MmioRead32(0x11bc9c);
 mClockPhaseError[Phase]=mClockError;
 return Ok;
}
STATIC BOOLEAN DvfsSubmitClock(UINT32 Target,UINT32 *Rate,UINT32 Phase){
 mClockPhaseStart=GetPerformanceCounter();mClockBudget=500000000ULL;
 MmioWrite32(0x11bc98,Target);MemoryFence();
 return DvfsWaitClock(Target,Rate,Phase);
}
STATIC BOOLEAN DvfsWriteQualified(UINT64 Address){
 UINT64 Par=DegasNativeTranslate(Address,1);
 return !(Par&1) && (Par&0x000ffffffffff000ULL)==(Address&~4095ULL);
}
STATIC UINT32 CpuDvfsClockStep(CONST DEGAS_DVFS_SNAPSHOT *S){
 UINT32 Base=0,Low=0,Small=0,High=0,Restored=0,Status=1,N;
 BOOLEAN Ready=FALSE,RestoreOk;CHAR8 Line[192];UINT32 Phase,I,Count;DVFS_CLOCK_RAW *T;
 STATIC CONST UINT32 Lut[]={2200,2100,2000,1900,1800,1700,1600,1500,1400,1300,1200,1100,1000,900,800,700,600,480};
 ZeroMem(mClockTrace,sizeof(mClockTrace));ZeroMem(mClockTraceCount,sizeof(mClockTraceCount));
 ZeroMem(mClockReq,sizeof(mClockReq));ZeroMem(mClockReady,sizeof(mClockReady));
 ZeroMem(mClockPhaseError,sizeof(mClockPhaseError));mClockError=0;
 mClockPhase=0;mClockPhaseStart=GetPerformanceCounter();mClockBudget=150000000ULL;
 for(N=0;N<ARRAY_SIZE(Lut);N++)if((S->Raw[1+N]&4095)!=Lut[N])goto Done;
 if((S->Raw[19]&4095)!=480)goto Done;
 // Exact boot state plus the stock LUT entries for1300/1400/2000MHz.
 if(S->Status || S->Raw[0]!=1 || S->Raw[33]!=0x101 || S->Raw[34]!=9 || S->Raw[35]!=3 ||
    (S->Raw[3]&4095)!=2000 || (S->Raw[9]&4095)!=1400 || (S->Raw[10]&4095)!=1300 ||
    !DvfsWriteQualified(0x11bc94) || !DvfsWriteQualified(0x11bc98) ||
    !DvfsClockSamples(1300000,&Base))goto Done;
 DvfsEmit(NULL,"CLK1 begin restore_khz=1300000 target_khz=2000000\n");
 if(!DvfsPublish())goto Done;
 // Same whole-word enable1 and readiness3 handshake as the installed driver.
 MmioWrite32(0x11bc94,1);MemoryFence();
 for(N=0;N<300;N++){
  if((MmioRead32(0x11bc9c)&3)==3){Ready=TRUE;break;}
  MicroSecondDelay(1000);
 }
 Status=2;if(!Ready)goto Restore;
 if(!DvfsSubmitClock(1300000,&Low,1))goto Restore;
 // Validate the small stock step before attempting the user-requested2GHz.
 Status=3;
 if(!DvfsSubmitClock(1400000,&Small,2))goto Restore;
 Status=4;
 if(!DvfsSubmitClock(2000000,&High,3))goto Restore;
 Status=0;
Restore:
 // Every ordinary failure after enable enters this path; never restore index9.
 RestoreOk=DvfsSubmitClock(1300000,&Restored,4);
 if(!RestoreOk)Status=5;
Done:
 mClockPhaseError[0]=mClockTraceCount[0]?mClockTrace[0][mClockTraceCount[0]-1].Error:0;
 // Full raw trace goes to the independent console only after restoration.
 // Replace the already-committed LUT journal with a bounded clock summary.
 ZeroMem(mDvfsJournal,sizeof(mDvfsJournal));CopyMem((UINT8 *)mDvfsJournal+12,"====0.000000-D\n",15);
 mDvfsJournalPosition=15;mDvfsJournalComplete=TRUE;
 for(Phase=0;Phase<5;Phase++){
  Count=mClockTraceCount[Phase];
  for(I=Count>128?Count-128:0;I<Count;I++){
   T=&mClockTrace[Phase][I%128];
   DEBUG((DEBUG_ERROR,"CLK2 RAW phase=%u n=%u error=%u rate=%u dfr=%Lx cr=%Lx en=%Lx filter=%Lx ref=%Lu t0=%Lx c0=%Lx t1=%Lx c1=%Lx\n",Phase,I,T->Error,T->Rate,T->Raw[0],T->Raw[1],T->Raw[2],T->Raw[3],T->Raw[4],T->Raw[5],T->Raw[6],T->Raw[7],T->Raw[8]));
  }
  {
   T=&mClockTrace[Phase][Count?(Count-1)%128:0];
   if(Count>1 && !T->Rate && !T->Error)T=&mClockTrace[Phase][(Count-2)%128];
   AsciiSPrint(Line,sizeof(Line),"CLK2 phase=%u n=%u error=%u rate=%u req=%u ready=%x t0=%Lx c0=%Lx t1=%Lx c1=%Lx\n",Phase,Count,mClockPhaseError[Phase],T->Rate,mClockReq[Phase],mClockReady[Phase],T->Raw[5],T->Raw[6],T->Raw[7],T->Raw[8]);DvfsEmit(NULL,Line);
  }
 }
 AsciiSPrint(Line,sizeof(Line),"CLK1 end status=%u boot=%u low=%u small=%u high=%u restored=%u khz\n",Status,Base,Low,Small,High,Restored);
 DvfsEmit(NULL,Line);if(!DvfsPublish() && Status!=5)return 6;return Status;
}
