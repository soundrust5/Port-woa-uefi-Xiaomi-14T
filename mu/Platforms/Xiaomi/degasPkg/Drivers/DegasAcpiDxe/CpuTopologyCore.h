// Pure bounded GICR discovery, shared with host tests. No register writes.
typedef UINT64 (*DEGAS_READ_GICR)(VOID *Context,UINT64 Address);
STATIC UINT32 DegasGicrDiscover(DEGAS_READ_GICR Read,VOID *Context,
 UINT64 Base,UINT64 Size,CONST UINT64 *Mpidrs,UINTN Count,UINT64 *Bases){
 UINT64 Address=Base,End,Typer,Stride;UINT32 Affinity,Seen=0;UINTN I,J;
 if(!Read || !Mpidrs || !Bases || !Count || Count>8 || Size<0x20000 ||
    Base>MAX_UINT64-Size || (Base&0xffff))return 1;
 End=Base+Size;
 for(I=0;I<Count;I++){
  Bases[I]=0;if(Mpidrs[I]&~0xff00ffffffULL)return 1;
  for(J=0;J<I;J++)if(Mpidrs[I]==Mpidrs[J])return 1;
 }
 while(Address<=End-0x20000){
  Typer=Read(Context,Address+8);Affinity=(UINT32)(Typer>>32);
  for(I=0;I<Count;I++)if(Affinity==(UINT32)((Mpidrs[I]&0xffffff)|((Mpidrs[I]>>8)&0xff000000)))break;
  if(I==Count || (Seen&(1U<<I)))return 2;
  Bases[I]=Address;Seen|=1U<<I;
  Stride=(Typer&2)?0x40000:0x20000; // VLPIS; GICv4 has two extra64KiB frames.
  if(Stride>End-Address)return 3;
  if(Typer&0x10)return Seen==((1U<<Count)-1)?0:4;
  Address+=Stride;
 }
 return 5; // Missing Last marker or incomplete region; never scan beyond DT.
}
