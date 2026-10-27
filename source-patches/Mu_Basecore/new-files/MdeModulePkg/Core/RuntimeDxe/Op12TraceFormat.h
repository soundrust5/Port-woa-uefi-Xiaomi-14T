// Experimental OnePlus12 trace ABI shared with the next-boot reader.
#ifndef OP12_TRACE_FORMAT_H_
#define OP12_TRACE_FORMAT_H_
#define OP12_TRACE_ADDRESS 0xA7FFFF000ULL
#define OP12_TRACE_MAGIC 0x315243543231504FULL
#define OP12_TRACE_VERSION 2
#define OP12_TRACE_BYTES 4096
#define OP12_TRACE_WORDS (OP12_TRACE_BYTES / 8)
#define OP12_TRACE_REPLICAS (OP12_TRACE_BYTES / 128)
#define OP12_TRACE_GUID {0x647596dc,0x8421,0x4a9f,{0xab,0x93,0x73,0x82,0x61,0xd3,0xd0,0x15}}
typedef unsigned long long OP12_TRACE_WORD;
// Each pair has two alternating records. Odd replicas invert every stored bit
// so reset-time bit decay is not biased towards a single data value.
// Record: magic/version/nonce/stage/detail/status/sequence/CRC32+complement.
static inline OP12_TRACE_WORD Op12TraceCheck (const OP12_TRACE_WORD *P)
{
  unsigned Crc = 0xffffffffU;
  for (unsigned I=0; I<7; I++) {
    OP12_TRACE_WORD W=P[I];
    for (unsigned B=0; B<8; B++, W>>=8) {
      Crc ^= (unsigned)(W & 0xff);
      for (unsigned Bit=0; Bit<8; Bit++)
        Crc=(Crc>>1) ^ (0xedb88320U & (0U-(Crc&1)));
    }
  }
  Crc=~Crc;
  return (OP12_TRACE_WORD)Crc | ((OP12_TRACE_WORD)(unsigned)~Crc << 32);
}
static inline int Op12TraceValid (const volatile OP12_TRACE_WORD *P)
{
  OP12_TRACE_WORD Words[7];
  unsigned I;
  if (P[0] != OP12_TRACE_MAGIC || P[1] != OP12_TRACE_VERSION || P[2] == 0 || P[6] == 0) return 0;
  for (I=0; I<7; I++) Words[I]=P[I];
  return Op12TraceCheck(Words) == P[7];
}
static inline int Op12TraceLatest (const volatile OP12_TRACE_WORD *P)
{
  int A=Op12TraceValid(P), B=Op12TraceValid(P+8);
  if (!A && !B) return -1;
  return B && (!A || P[14]>P[6]) ? 8 : 0;
}
static inline void Op12TraceStore (volatile OP12_TRACE_WORD *P,
  OP12_TRACE_WORD Nonce, OP12_TRACE_WORD Stage, OP12_TRACE_WORD Detail, OP12_TRACE_WORD Status)
{
  int Last=Op12TraceLatest(P);
  OP12_TRACE_WORD Seq=Last<0 ? 1 : P[Last+6]+1;
  OP12_TRACE_WORD W[8]={OP12_TRACE_MAGIC, OP12_TRACE_VERSION, Nonce, Stage, Detail, Status, Seq, 0};
  W[7]=Op12TraceCheck(W);
  for (unsigned R=0; R<OP12_TRACE_REPLICAS; R++) {
    OP12_TRACE_WORD Mask=(R&1) ? ~(OP12_TRACE_WORD)0 : 0;
    volatile OP12_TRACE_WORD *D=P+R*16+(Last==0 ? 8 : 0);
    D[0]=Mask; // invalid magic until the record is fully written
    for (unsigned I=1; I<8; I++) D[I]=W[I]^Mask;
    D[0]=W[0]^Mask;
  }
}
#endif
