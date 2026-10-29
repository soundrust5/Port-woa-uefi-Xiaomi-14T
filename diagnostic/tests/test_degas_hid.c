#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../degas/touch/windows/degas_hid.h"
typedef struct {unsigned calls,failAt;uint8_t packets[64][14];} TEST;
static int sink(void *ctx,const uint8_t *p,size_t n){TEST *t=ctx;assert(n==14 && t->calls<64);memcpy(t->packets[t->calls++],p,n);return t->calls!=t->failAt;}
static void descriptor(void){
 unsigned size=0,count=0,id=0,input=0,feature=0,usage=0,unit=0,exp=0,physical=0,axes=0;size_t i=0;
 while(i<sizeof DegasHidDescriptor){unsigned key=DegasHidDescriptor[i++],n=key&3,v=0,j;if(n==3)n=4;
  assert(i+n<=sizeof DegasHidDescriptor);for(j=0;j<n;j++)v|=(unsigned)DegasHidDescriptor[i++]<<(8*j);
  switch(key&0xfc){case 0x74:size=v;break;case 0x94:count=v;break;case 0x84:id=v;break;
  case 0x08:usage=v;break;case 0x64:unit=v;break;case 0x54:exp=v;break;case 0x44:physical=v;break;
  case 0x80:assert(id==1);input+=size*count;if(usage==0x30 || usage==0x31){assert(unit==0x11 && exp==0x0d);assert(physical==(usage==0x30?6954u:15458u));axes++;}break;
  case 0xb0:assert(id==2);feature+=size*count;break;}
 }assert(input==104 && feature==8 && axes==4);
}
static DEGAS_FTS_REPORT pair(unsigned a,unsigned b){DEGAS_FTS_REPORT r={0};r.count=2;r.contacts[0].id=(uint8_t)a;r.contacts[1].id=(uint8_t)b;r.contacts[0].x=121999;r.contacts[0].y=271199;r.contacts[1].x=61000;r.contacts[1].y=135600;return r;}
int main(void){
 DEGAS_HID_STATE s={0};TEST t={0};DEGAS_FTS_REPORT r=pair(1,7);unsigned n,i;uint8_t *p;
 descriptor();assert(DegasHidUpdate(&s,&r,1,sink,&t)==DegasHidOk);assert(s.down==((1<<1)|(1<<7)));
 p=t.packets[0];assert(p[1]==2 && p[2]==1 && p[3]==1 && p[8]==1 && p[9]==7);assert(p[4]==255 && p[5]==127);
 /* Reordered packet retains IDs, neither release nor swap. */
 r=pair(7,1);assert(DegasHidUpdate(&s,&r,2,sink,&t)==DegasHidOk && t.calls==2);
 /* One explicit UP, other finger still held: no all-up discontinuity. */
 r.contacts[0].event=1;n=t.calls;assert(DegasHidUpdate(&s,&r,3,sink,&t)==DegasHidOk);
 p=t.packets[n];assert(p[1]==2 && p[2]==1 && p[3]==1 && p[8]==0 && p[9]==7);assert(s.down==(1<<1));
 /* Missing old contact is released before two new contacts: no union>2. */
 r=pair(3,9);n=t.calls;assert(DegasHidUpdate(&s,&r,4,sink,&t)==DegasHidOk && t.calls==n+2);
 assert(t.packets[n][1]==1 && t.packets[n][2]==0 && t.packets[n][3]==1);assert(s.down==((1<<3)|(1<<9)));
 /* Duplicate IDs invalidate whole frame and release both. */
 r=pair(3,3);assert(DegasHidUpdate(&s,&r,5,sink,&t)==DegasHidInvalid && !s.down && s.faulted);assert(DegasHidRearm(&s));
 r=pair(0,8);assert(DegasHidUpdate(&s,&r,6,sink,&t)==DegasHidOk);
 assert(DegasHidTick(&s,256,sink,&t)==DegasHidOk && !s.down && s.faulted);assert(DegasHidRearm(&s));
 /* Failed two-contact DOWN may have been delivered. Both UP required. */
 t.failAt=t.calls+1;assert(DegasHidUpdate(&s,&r,300,sink,&t)==DegasHidSubmitFailed);assert(s.down==257 && !DegasHidRearm(&s));
 t.failAt=t.calls+1;assert(DegasHidFault(&s,sink,&t)==DegasHidSubmitFailed && s.down==257);
 t.failAt=0;assert(DegasHidFault(&s,sink,&t)==DegasHidOk && !s.down);assert(DegasHidRearm(&s));
 /* Failed release must not introduce replacement IDs. */
 assert(DegasHidUpdate(&s,&r,301,sink,&t)==DegasHidOk);r=pair(2,4);n=t.calls;t.failAt=n+1;
 assert(DegasHidUpdate(&s,&r,302,sink,&t)==DegasHidSubmitFailed && t.calls==n+1 && s.down==257);
 t.failAt=0;assert(DegasHidFault(&s,sink,&t)==DegasHidOk);assert(DegasHidRearm(&s));
 /* Failure after successful release retains just the new uncertain pair. */
 r=pair(1,5);assert(DegasHidUpdate(&s,&r,303,sink,&t)==DegasHidOk);r=pair(2,4);t.failAt=t.calls+2;
 assert(DegasHidUpdate(&s,&r,304,sink,&t)==DegasHidSubmitFailed && s.down==20);
 t.failAt=0;assert(DegasHidFault(&s,sink,&t)==DegasHidOk);assert(DegasHidRearm(&s));
 r.contacts[1].x=122000;assert(DegasHidUpdate(&s,&r,305,sink,&t)==DegasHidInvalid);
 for(i=1;i<=271199;i++)assert(DegasHidScale(i,271199)>=DegasHidScale(i-1,271199));
 assert(DegasHidScale(0,121999)==0 && DegasHidScale(271199,271199)==32767);
 puts("degas two-contact HID PASS: independent UP, identity, missing contact, stale, failed delivery, descriptor");return 0;
}
