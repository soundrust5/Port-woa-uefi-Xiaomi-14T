/* MT6363 SID4, only source-qualified RF rail fields. No calibration writes. */
static unsigned int DegasPmicRfUpdate(const DEGAS_PMIF_IO *Io,unsigned int Address,unsigned int Mask,unsigned int Value){
 unsigned int Allowed=Address==0x1f2c?15:(Address==0x1bcd||Address==0x1d07)?3:
  (Address==0x1bd4||Address==0x1d16)?128:0;
 unsigned int Before,After,I,Error,Target;
 if(!Allowed || !Mask || (Mask&~Allowed) || (Value&~Mask))return 5;
 Error=DegasPmicReadByte(Io,4,Address,&Before);if(Error)return Error;
 Target=(Before&~Mask)|Value;if(Target==Before)return 0;
 for(I=0;I<1000;I++){
  if(((Io->Read(Io->Context,0x8a8)>>1)&7)==0)break;
  Io->Delay(Io->Context,10);
 }
 if(I==1000)return 2;
 Io->Write(Io->Context,0x884,Target);
 Io->Write(Io->Context,0x880,0xe4000000u|Address);
 Error=DegasPmicReadByte(Io,4,Address,&After);if(Error)return Error;
 return After==Target?0:6;
}
