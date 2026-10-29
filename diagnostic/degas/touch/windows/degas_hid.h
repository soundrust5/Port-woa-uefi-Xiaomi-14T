/* Two-contact FT3683 snapshot -> parallel HID. Serialized callers; sink copies.
 * Hardware capture is bounded FIFO (20-byte payload,29-byte wire frame).
 * Missing contacts are released before introducing replacements. */
#ifndef DEGAS_HID_H
#define DEGAS_HID_H
#include "../fts_contact.h"
#define DEGAS_HID_INPUT_ID 1u
#define DEGAS_HID_FEATURE_ID 2u
#define DEGAS_HID_MAX_CONTACTS 2u
#define DEGAS_HID_STALE_MS 250u
#define DEGAS_HID_REPORT_BYTES 14u
typedef int (*DEGAS_HID_SINK)(void *,const uint8_t *,size_t);
typedef enum {DegasHidOk,DegasHidIdle,DegasHidInvalid,DegasHidSubmitFailed,DegasHidFaulted} DEGAS_HID_RESULT;
typedef struct {
 uint16_t x[10],y[10],down; /* possible host contact ID bitmask */
 uint8_t faulted,hasTime;
 uint64_t lastTime;
} DEGAS_HID_STATE;
/* Phone DisplayDeviceInfo:1220x2712 @445.614dpi =>69.54x154.58mm.
 * Each finger has tip,id,X,Y =6bytes. Count includes valid UP records too. */
#define DEGAS_HID_FINGER \
 0x05,0x0d,0x09,0x22,0xa1,0x02,0x09,0x42,0x15,0x00,0x25,0x01, \
 0x75,0x01,0x95,0x01,0x81,0x02,0x75,0x07,0x81,0x03, \
 0x09,0x51,0x25,0x09,0x75,0x08,0x81,0x02, \
 0x05,0x01,0x09,0x30,0x26,0xff,0x7f,0x75,0x10, \
 0x35,0x00,0x55,0x0d,0x65,0x11,0x46,0x2a,0x1b,0x81,0x02, \
 0x09,0x31,0x46,0x62,0x3c,0x81,0x02, \
 0x45,0x00,0x55,0x00,0x65,0x00,0xc0
static const uint8_t DegasHidDescriptor[]={
 0x05,0x0d,0x09,0x04,0xa1,0x01,0x85,0x01,
 0x09,0x54,0x15,0x00,0x25,0x02,0x75,0x08,0x95,0x01,0x81,0x02,
 DEGAS_HID_FINGER,DEGAS_HID_FINGER,
 0x05,0x0d,0x85,0x02,0x09,0x55,0x15,0x00,0x25,0x02,
 0x75,0x08,0x95,0x01,0xb1,0x02,0xc0
};
#undef DEGAS_HID_FINGER
static inline uint16_t DegasHidScale(uint32_t v,uint32_t max){return (uint16_t)(((uint64_t)v*32767u+max/2u)/max);}
/* records enumerates valid slots, downs selects tip states. All old downs
 * must be present (possibly UP) or already released by an earlier frame. */
static inline DEGAS_HID_RESULT DegasHidFrame(DEGAS_HID_STATE *s,uint16_t records,
 uint16_t downs,const uint16_t *x,const uint16_t *y,DEGAS_HID_SINK sink,void *ctx){
 uint8_t p[DEGAS_HID_REPORT_BYTES]={1,0};unsigned i,n=0;
 if((downs&~records) || (s->down&~records))return DegasHidInvalid;
 for(i=0;i<10;i++)if(records&(1u<<i)){
  unsigned at=2+6*n;if(n>=DEGAS_HID_MAX_CONTACTS)return DegasHidInvalid;
  p[at]=(downs&(1u<<i))!=0;p[at+1]=(uint8_t)i;
  p[at+2]=(uint8_t)x[i];p[at+3]=(uint8_t)(x[i]>>8);
  p[at+4]=(uint8_t)y[i];p[at+5]=(uint8_t)(y[i]>>8);n++;
 }
 p[1]=(uint8_t)n;
 if(!sink || !sink(ctx,p,sizeof p)){
  /* Attempted DOWN may have reached host. Retain union for an accepted UP.
   * Replacements only occur after old releases succeeded. */
  for(i=0;i<10;i++)if(downs&(1u<<i)){s->x[i]=x[i];s->y[i]=y[i];}
  s->down|=downs;s->faulted=1;return DegasHidSubmitFailed;
 }
 for(i=0;i<10;i++)if(downs&(1u<<i)){s->x[i]=x[i];s->y[i]=y[i];}
 s->down=downs;return DegasHidOk;
}
static inline DEGAS_HID_RESULT DegasHidFault(DEGAS_HID_STATE *s,DEGAS_HID_SINK sink,void *ctx){
 if(!s)return DegasHidInvalid;s->faulted=1;
 return s->down?DegasHidFrame(s,s->down,0,s->x,s->y,sink,ctx):DegasHidIdle;
}
static inline int DegasHidRearm(DEGAS_HID_STATE *s){
 if(!s || s->down)return 0;s->faulted=0;s->hasTime=0;return 1;
}
static inline DEGAS_HID_RESULT DegasHidTick(DEGAS_HID_STATE *s,uint64_t now,DEGAS_HID_SINK sink,void *ctx){
 if(!s)return DegasHidInvalid;
 if(s->faulted || (s->down && s->hasTime && (now<s->lastTime || now-s->lastTime>=DEGAS_HID_STALE_MS)))return DegasHidFault(s,sink,ctx);
 return DegasHidIdle;
}
static inline DEGAS_HID_RESULT DegasHidUpdate(DEGAS_HID_STATE *s,const DEGAS_FTS_REPORT *r,uint64_t now,DEGAS_HID_SINK sink,void *ctx){
 uint16_t ids=0,downs=0,x[10]={0},y[10]={0};unsigned i;DEGAS_HID_RESULT result;
 if(!s)return DegasHidInvalid;if(s->faulted)return DegasHidFaulted;
 if(!r || !r->count || r->count>DEGAS_HID_MAX_CONTACTS)goto Invalid;
 for(i=0;i<r->count;i++){
  const DEGAS_FTS_CONTACT *c=&r->contacts[i];uint16_t bit;
  if(c->id>=10 || c->event>2 || c->x>121999 || c->y>271199)goto Invalid;
  bit=(uint16_t)(1u<<c->id);if(ids&bit)goto Invalid;ids|=bit;
  if(c->event!=1){downs|=bit;x[c->id]=DegasHidScale(c->x,121999);y[c->id]=DegasHidScale(c->y,271199);}
 }
 if(s->hasTime && (now<s->lastTime || (s->down && now-s->lastTime>=DEGAS_HID_STALE_MS))){
  (void)DegasHidFault(s,sink,ctx);return DegasHidFaulted;
 }
 /* Full sensor snapshot. Release missing/UP old IDs while continuing held
  * IDs. No new IDs in this phase: failed delivery retains at most two IDs,
  * so all-UP always fits one report. */
 if(s->down&~downs){
  result=DegasHidFrame(s,s->down,(uint16_t)(s->down&downs),s->x,s->y,sink,ctx);
  if(result!=DegasHidOk)return result;
  if(!downs){s->hasTime=1;s->lastTime=now;return DegasHidOk;}
 }
 if(!downs)return DegasHidIdle;
 result=DegasHidFrame(s,downs,downs,x,y,sink,ctx);
 if(result==DegasHidOk){s->hasTime=1;s->lastTime=now;}return result;
Invalid:
 (void)DegasHidFault(s,sink,ctx);return DegasHidInvalid;
}
#endif
