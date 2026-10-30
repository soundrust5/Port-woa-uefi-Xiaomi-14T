#ifndef DEGAS_WIFI_CONN_IO_H
#define DEGAS_WIFI_CONN_IO_H
#include "../../../../../../diagnostic/degas/wifi/conn_power_core.h"
extern UINT64 DegasNativeTranslate(UINT64 Address,UINT32 Write);
typedef struct {UINT64 LastRead;} WIFI_PROBE_CONTEXT;
STATIC BOOLEAN WifiReadAllowed(UINT64 A){
 return A==CONN_CFG || A==CONN_CTL || A==CONN_BUS || A==CONN_BUS+0xc ||
  A==CONN_BUS+0x1c0 || A==CONN_BUS+0x1cc || A==0x1000100c || A==0x100011cc || A==0x18011000 || A==0x18060010 ||
  A==ConnPinAddress[0] || A==ConnPinAddress[1] || A==ConnPinAddress[2] || A==ConnPinAddress[3];
}
STATIC BOOLEAN WifiWriteAllowed(UINT64 A,UINT32 V){
 if(A==CONN_CTL)return TRUE;
 if(A==CONN_BUS+4 || A==CONN_BUS+8)return V==BIT25 || V==BIT26;
 if(A==CONN_BUS+0x1c4 || A==CONN_BUS+0x1c8)return V==1 || V==2;
 if(A==ConnPinAddress[0] || A==ConnPinAddress[1] || A==ConnPinAddress[2] || A==ConnPinAddress[3])return TRUE;
 return FALSE;
}
STATIC int WifiProbeRead(void *Context,uint64_t A,uint32_t *V){
 WIFI_PROBE_CONTEXT *C=Context;UINT64 Par;BOOLEAN New;
 if(!WifiReadAllowed(A) || !V)return 0;
 Par=DegasNativeTranslate(A,0);
 if((Par&1) || (Par&0x000ffffffffff000ULL)!=(A&~4095ULL))return 0;
 New=C->LastRead!=A;
 if(New)DEBUG((DEBUG_ERROR,"DEGAS WIFI read_begin=%08x\n",(UINT32)A));
 MmioWrite32(0x1c00a008,0x1971);*V=MmioRead32((UINTN)A);
 if(New)DEBUG((DEBUG_ERROR,"DEGAS WIFI read_value=%08x value=%08x\n",(UINT32)A,*V));
 C->LastRead=A;return 1;
}
STATIC int WifiProbeWrite(void *Context,uint64_t A,uint32_t V){
 UINT64 Par;UINTN I;(VOID)Context;
 if(!WifiWriteAllowed(A,V))return 0;
 Par=DegasNativeTranslate(A,1);
 if((Par&1) || (Par&0x000ffffffffff000ULL)!=(A&~4095ULL))return 0;
 for(I=0;I<4;I++)if(A==ConnPinAddress[I] && ((MmioRead32((UINTN)A)^V)&~ConnPinMask[I]))return 0;
 DEBUG((DEBUG_ERROR,"DEGAS WIFI write=%08x value=%08x\n",(UINT32)A,V));
 MmioWrite32((UINTN)A,V);MemoryFence();return 1;
}
STATIC void WifiProbeDelay(void *Context,unsigned Us){(VOID)Context;MicroSecondDelay(Us);}
STATIC void WifiProbeLog(void *Context,unsigned S,uint64_t A,uint32_t V){
 (VOID)Context;DEBUG((DEBUG_ERROR,"DEGAS WIFI stage=%u address=%08x value=%08x\n",S,(UINT32)A,V));
}

#endif
