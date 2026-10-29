/* Same reserved ramoops console as degas RamSerialPortLib. No storage I/O.
 * Single record writer uses a nonblocking interlocked lock: higher IRQL never
 * spins on a preempted writer. Drop a record rather than deadlock.
 */
static volatile ULONG *Retained;
static volatile LONG RetainedBusy;
static PDRIVER_UNLOAD PortUnload;
static void Retain(const char *event,ULONG x,ULONG y,ULONG z){
 const char hex[]="0123456789ABCDEF";char line[112];ULONG n=0,i,j,pos,size,values[3];
 volatile UCHAR *data;const ULONG cap=0x40000-12;
 if(!Retained || InterlockedCompareExchange(&RetainedBusy,1,0))return;
 if(Retained[0]!=0x43474244 || Retained[1]>=cap || Retained[2]>cap)goto Done;
 line[n++]='\n';line[n++]='W';line[n++]='I';line[n++]='N';line[n++]='U';line[n++]='F';line[n++]='S';line[n++]=' ';
 for(i=0;event[i] && i<48;i++)line[n++]=event[i];
 values[0]=x;values[1]=y;values[2]=z;
 for(i=0;i<3;i++){line[n++]=' ';for(j=0;j<8;j++)line[n++]=hex[(values[i]>>(28-j*4))&15];}
 line[n++]='\n';data=(volatile UCHAR*)Retained+12;pos=Retained[1];size=Retained[2];
 for(i=0;i<n;i++){data[pos++]=(UCHAR)line[i];if(pos==cap)pos=0;if(size<cap)size++;}
 KeMemoryBarrier();Retained[1]=pos;Retained[2]=size;KeMemoryBarrier();
Done:
 InterlockedExchange(&RetainedBusy,0);
}
static void RetainedInit(void){
 PHYSICAL_ADDRESS p;p.QuadPart=0x480af000;
 Retained=MmMapIoSpace(p,0x40000,MmNonCached);
 Retain("DriverEntry ACPIBus",17,0,0);
}
static void RetainedClose(void){
 if(Retained){MmUnmapIoSpace((PVOID)Retained,0x40000);Retained=NULL;}
}
static void DiagnosticUnload(PDRIVER_OBJECT driver){
 if(PortUnload)PortUnload(driver);
 RetainedClose();
}
