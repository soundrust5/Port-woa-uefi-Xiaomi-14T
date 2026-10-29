#include <stdint.h>
#include <stddef.h>
#include <assert.h>
#define STATIC static
#define VOID void
#define TRUE 1
#define FALSE 0
#define DEBUG(x) ((void)0)
typedef uint32_t UINT32;typedef size_t UINTN;typedef int BOOLEAN;
static unsigned scenario,hce=1,attempts,stops,enables,delay;
static int UfsReadAttributes(void){return scenario!=4;}
static int UfsStartLink(void){attempts++;return scenario==0&&attempts==2;}
static unsigned MmioRead32(unsigned a){if(a==0x112b0034)return hce;if(a==0x112b0030)return scenario==3?0:8;assert(0);return 0;}
static void MmioWrite32(unsigned a,unsigned v){assert(a==0x112b0034||a==0x112b0020);if(a==0x112b0034){if(!v){stops++;if(scenario==2)return;}else enables++;hce=v;}else assert(v==0x404);}
static void MicroSecondDelay(unsigned n){delay+=n;}
STATIC BOOLEAN UfsLinkAttempts(VOID){
 UINTN Attempt,N;BOOLEAN Done;
 for(Attempt=0;Attempt<3;Attempt++){
  DEBUG((DEBUG_ERROR,"DEGAS UFS LINK attempt=%u\n",(UINT32)Attempt+1));
  if(!UfsReadAttributes())return FALSE;
  if(UfsStartLink())return TRUE;
  if(Attempt==2)break;
  // Match the generic Android retry's HBA disable/enable transition.
  // No SoC reset or device-reset line operation is implied here.
  MmioWrite32(0x112b0034,0);Done=FALSE;
  for(N=0;N<1000;N++){
   if((MmioRead32(0x112b0034)&1)==0){Done=TRUE;break;}MicroSecondDelay(100);
  }
  if(!Done){DEBUG((DEBUG_ERROR,"DEGAS UFS RETRY STOP FAILED\n"));return FALSE;}
  MmioWrite32(0x112b0020,0x404); // Both sources observed/read before acknowledge.
  MmioWrite32(0x112b0034,1);MicroSecondDelay(600);Done=FALSE;
  for(N=0;N<1000;N++){
   if((MmioRead32(0x112b0034)&1) && (MmioRead32(0x112b0030)&8)){Done=TRUE;break;}
   MicroSecondDelay(100);
  }
  if(!Done){DEBUG((DEBUG_ERROR,"DEGAS UFS RETRY ENABLE FAILED\n"));return FALSE;}
 }
 return FALSE;
}

static void init(unsigned s){scenario=s;hce=1;attempts=stops=enables=delay=0;}
int main(void){init(0);assert(UfsLinkAttempts());assert(attempts==2&&stops==1&&enables==1);init(1);assert(!UfsLinkAttempts());assert(attempts==3&&stops==2);init(2);assert(!UfsLinkAttempts());assert(attempts==1&&enables==0&&delay==100000);init(3);assert(!UfsLinkAttempts());assert(attempts==1&&delay==100600);init(4);assert(!UfsLinkAttempts());assert(attempts==0&&stops==0);return 0;}
