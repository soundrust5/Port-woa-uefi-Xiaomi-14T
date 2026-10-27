/* MT6897 PMIF SWINF2: MT6363 SID4 keys and MT6368 SID5 VTP.
 * Register layout: MiCode MT6897 spmi-mtk-pmif-core.c mt6xxx_regs.
 * Device/base/SID: degas DT. Only VTP enable has a scoped write helper.
 */
typedef struct {
 void *Context;
 unsigned int (*Read)(void *,unsigned int);
 void (*Write)(void *,unsigned int,unsigned int);
 void (*Delay)(void *,unsigned int);
} DEGAS_PMIF_IO;
static unsigned int DegasPmicReadByte(const DEGAS_PMIF_IO *Io,unsigned int Sid,unsigned int Address,unsigned int *Value){
 unsigned int I,S;
 if(!Io || !Value || Sid>15 || Address>65535)return 5;
 if((Io->Read(Io->Context,0)&1)==0)return 1;
 for(I=0;I<1000;I++){
  S=Io->Read(Io->Context,0x8a8);
  if(((S>>1)&7)==0)break;
  Io->Delay(Io->Context,10);
 }
 if(I==1000)return 2; // Never clear or overwrite somebody else's transaction.
 Io->Write(Io->Context,0x880,0xc0000000u|(Sid<<24)|Address); // EXT_READ_LONG, one byte.
 for(I=0;I<1000;I++){
  S=Io->Read(Io->Context,0x8a8);
  if(((S>>1)&7)==6)break;
  Io->Delay(Io->Context,10);
 }
 if(I==1000)return 3;
 S=Io->Read(Io->Context,0x894)&0xff;
 Io->Write(Io->Context,0x8a4,1); // Host transaction completion, not a PMIC write.
 for(I=0;I<1000;I++){
  if(((Io->Read(Io->Context,0x8a8)>>1)&7)==0){*Value=S;return 0;}
  Io->Delay(Io->Context,10);
 }
 return 4;
}
static unsigned int DegasPmicKeysRead(const DEGAS_PMIF_IO *Io,unsigned int *Value){
 return DegasPmicReadByte(Io,4,0x1e,Value);
}
/* Scoped VTP enable only. Never changes voltage selection or other PMIC rails.
 * Readback after queued write also waits for PMIF idle before issuing read. */
static unsigned int DegasPmicVtpEnable(const DEGAS_PMIF_IO *Io,unsigned int Enable){
 unsigned int Before,After,I,Error;
 if(Enable>1)return 5;
 Error=DegasPmicReadByte(Io,5,0x1c87,&Before);if(Error)return Error;
 if((Before&1)==Enable)return 0;
 for(I=0;I<1000;I++){
  if(((Io->Read(Io->Context,0x8a8)>>1)&7)==0)break;
  Io->Delay(Io->Context,10);
 }
 if(I==1000)return 2;
 Io->Write(Io->Context,0x884,(Before&~1u)|Enable);
 Io->Write(Io->Context,0x880,0xe5001c87u);
 Error=DegasPmicReadByte(Io,5,0x1c87,&After);if(Error)return Error;
 return After==((Before&~1u)|Enable)?0:6;
}
