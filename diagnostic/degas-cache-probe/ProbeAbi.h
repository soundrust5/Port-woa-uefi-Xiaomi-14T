// Diagnostic ABI only. Including this file does not enable a firmware probe.
// Caller supplies the standard EDK2 UINT64/UINT8 types.
#ifndef DEGAS_CACHE_PROBE_ABI_H
#define DEGAS_CACHE_PROBE_ABI_H
#define DEGAS_CACHE_PROBE_BYTES 512U
#define DEGAS_CACHE_PROBE_MAGIC 0x3145484341434744ULL
#define DEGAS_CACHE_PROBE_COMMIT 0x48434143454e4f44ULL
enum {DegasCacheOk=0,DegasCacheInProgress=1,DegasCacheBadHeader=2,
 DegasCacheWrongCpu=3,DegasCacheWrongEl=4,DegasCacheBadApState=5,
 DegasCacheUnsupportedCcidx=6,DegasCacheReservedCtype=7,
 DegasCacheCpuOffReturned=9,DegasCacheUnsafeOffTarget=10};
typedef struct {UINT64 Selector,Ccsidr;} DEGAS_CACHE_RAW_ENTRY;
typedef struct {
 UINT64 Magic,Bytes,Epoch,ExpectedMpidr,Mode,ReservedHeader[3];
 UINT64 Phase,Status,Mpidr,Midr,El,Sctlr,Daif,Csselr,Clidr,Mmfr2,Count;
 UINT64 IncomingX0,CpuOffReturn,Commit,Detail,Reserved184,LastSelector,ReservedResult[7];
 DEGAS_CACHE_RAW_ENTRY Entries[14];
 UINT8 ReservedTail[32];
} DEGAS_CACHE_PROBE_RECORD;
// Mode0: BSP calls DegasCacheCollect(recordVA), which returns with DAIF and
// observed CSSELR restored. Mode1: independent-PC-literal AP wrapper, CPU_OFF.
// Reserve/retain separate code and result RAM; publish code/header to PoC.
// Record commit does not prove CPU_OFF. Caller MUST observe actual OFF state.
// Traps can prevent restoration/off; timeout means reset, never proceed to OS.
#endif
