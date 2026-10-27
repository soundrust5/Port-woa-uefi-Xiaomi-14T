// Bounded runner for trusted, fixed packets ending in SPR sentinel/EOC/jump.
// Identity IOVA avoids assuming the command-fetch translation mode.
// Reset-only session; mapping and packet remain reserved even after failure.
STATIC BOOLEAN GceSmokePoll(UINTN Address,UINT32 Mask,UINT32 Want,UINT32 Limit){
 UINT32 I;
 for(I=0;I<Limit;I++){
  if((MmioRead32(Address)&Mask)==Want)return TRUE;
  MicroSecondDelay(1);
 }
 return FALSE;
}
STATIC UINT32 GceStopProgram(VOID){
 CONST UINTN Tb=0x1e980100;UINT32 Irq;BOOLEAN Suspended,Reset;
 MmioWrite32(Tb+8,1);
 Suspended=((MmioRead32(Tb+4)&1)==0) || GceSmokePoll(Tb+0xc,2,2,1000);
 MmioWrite32(Tb,1);Reset=GceSmokePoll(Tb,1,0,100);
 if(Reset)MmioWrite32(0x1e980030,0x3200);
 MmioWrite32(Tb+4,0);ArmDataSynchronizationBarrier();
 Irq=MmioRead32(Tb+0x10);MmioWrite32(Tb+0x10,~Irq); // W0C, not W1C
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH cleanup suspended=%u reset=%u enabled=%08x irq=%08x KEEP BUFFER UNTIL RESET\n",
  Suspended,Reset,MmioRead32(Tb+4),MmioRead32(Tb+0x10)));
 if(!Suspended || !Reset || (MmioRead32(Tb+4)&1) ||
    MmioRead32(Tb+0x10)!=0 || MmioRead32(Tb+0x14)!=0)return 48;
 return 0;
}
STATIC UINT32 GceRunProgram(CONST UINT64 *Words,UINTN WordCount,UINT32 Sentinel,volatile UINT64 *Packet,BOOLEAN (*BeforeEnable)(VOID),BOOLEAN Loop,VOID (*Progress)(UINT32,UINT32),BOOLEAN KeepRunning){
 CONST UINTN Tb=0x1e980100;
 CONST UINT32 Pc=(UINT32)((DEGAS_GCE_IOVA+0x40000000ULL)>>3);
 UINT32 End;
 UINT32 I,Count0,Count1=0,LastCount,Spr=0,Irq=0,At=0,Status=0,Result=46;
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH begin IDENTITY IOVA words=%Lu sentinel=%08x\n",(UINT64)WordCount,Sentinel));
 if(KeepRunning && (!Loop || Progress!=NULL))return 40;
 if(Packet==NULL || Words==NULL || WordCount<3 || WordCount>96 || DEGAS_GCE_IOVA!=DEGAS_GCE_PACKET_PA)return 40;
 End=(UINT32)((DEGAS_GCE_IOVA+(WordCount-(Loop?0:1))*8+0x40000000ULL)>>3);
 // Immutable packet contract: only the final jump differs for loop mode.
 if(Words[WordCount-3]!=(0xa080000000000000ULL|Sentinel) ||
    Words[WordCount-2]!=0x4000000000000000ULL ||
    Words[WordCount-1]!=(Loop?(0x1000000100000000ULL|Pc):0x1000000000000001ULL))return 40;
 // Mapping has been verified by caller. Recheck exclusive inactivity and IRQs
 // immediately before testing ownership of normal candidate thread0's CSRs.
 if(GceSnapshot()!=0)return 41;
 if(MmioRead32(Tb+0x10)!=0 || MmioRead32(Tb+0x14)!=0)return 42;
 for(I=0;I<WordCount;I++)Packet[I]=Words[I];
 ArmDataSynchronizationBarrier();
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH reset_begin thread=0 pc=%08x end=%08x mode=%08x\n",Pc,End,MmioRead32(0x1e980054)));
 MmioWrite32(Tb,1);
 if(!GceSmokePoll(Tb,1,0,100)){DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH reset_timeout\n"));return 43;}
 MmioWrite32(0x1e980030,0x3200);
 Count0=MmioRead32(Tb+0x28);Spr=MmioRead32(Tb+0x60);
 LastCount=Count0;
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH baseline count=%08x spr=%08x\n",Count0,Spr));
 if(Spr==Sentinel){Result=44;goto Cleanup;}
 MmioWrite32(Tb+0x50,0);MmioWrite32(Tb+0x40,0);
 MmioWrite32(Tb+0x24,End);MmioWrite32(Tb+0x20,Pc);MmioWrite32(Tb+0x14,0);
 ArmDataSynchronizationBarrier();
 if(MmioRead32(Tb+0x24)!=End || MmioRead32(Tb+0x20)!=Pc ||
    MmioRead32(Tb+0x14)!=0 || MmioRead32(Tb+0x40)!=0 ||
    MmioRead32(Tb+0x50)!=0 || MmioRead32(Tb+0x10)!=0 ||
    (MmioRead32(Tb+4)&1)!=0){Result=45;goto Cleanup;}
 if(BeforeEnable!=NULL && !BeforeEnable()){Result=49;goto Cleanup;}
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH enable_begin\n"));
 MemoryFence();MmioWrite32(Tb+4,1);ArmDataSynchronizationBarrier();
 for(I=0;I<(KeepRunning?2000U:(Loop?20000U:200000U));I++){
  Irq=MmioRead32(Tb+0x10);At=MmioRead32(Tb+0x20);
  Spr=MmioRead32(Tb+0x60);Count1=MmioRead32(Tb+0x28);
  if(Irq&0x12){Result=47;break;}
  if(!Loop && At==End && Spr==Sentinel && Count1!=Count0){Result=0;break;}
  if(Loop){
   // Loop END is one-past; no IRQ is requested. SPR0 is scratch again as soon
   // as the next iteration starts, so only EOC progress proves completion.
   if(At<Pc || At>=End || !(MmioRead32(Tb+4)&1)){Result=53;break;}
   if(KeepRunning && (UINT32)(Count1-Count0)>=3U){Result=0;break;}
   if(I && I%5000U==0){
    DEBUG((DEBUG_ERROR,"DEGAS GCELOOP tick=%u count=%08x pc=%08x\n",I,Count1,At));
    if(Count1==LastCount){Result=54;break;}
    LastCount=Count1;
    if(Progress!=NULL)Progress(I,Count1);
   }
  }
  MicroSecondDelay(Loop?100:1);
 }
 if(Loop && !KeepRunning && I==20000U)Result=(Count1!=LastCount)?0:54;
 // Capture execution evidence BEFORE reset or IRQ acknowledgment.
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH poll_detail dsi=%08x r0=%08x r1=%08x r14=%08x r15=%08x\n",
  MmioRead32(0x1400d00c),MmioRead32(0x1e980080),MmioRead32(0x1e980084),MmioRead32(0x1e9800b8),MmioRead32(0x1e9800bc)));
 Status=MmioRead32(Tb+0xc);
 DEBUG((DEBUG_ERROR,"DEGAS GCEFETCH observed result=%u polls=%u loop=%u pc=%08x end=%08x spr=%08x count0=%08x count1=%08x irq=%08x state=%08x enabled=%08x\n",
  Result,I,Loop,At,MmioRead32(Tb+0x24),Spr,Count0,Count1,Irq,Status,MmioRead32(Tb+4)));
 if(KeepRunning && !Result){
  DEBUG((DEBUG_ERROR,"DEGAS GCE OS LOOP ACTIVE count=%08x NO CPU CALLBACK\n",Count1));
  return 0;
 }
Cleanup:
 if(GceStopProgram()!=0)return 48;
 return Result;
}
