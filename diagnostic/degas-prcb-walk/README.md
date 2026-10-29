# Read-only Windows PRCB diagnostic reader

This is a standalone test artifact. It is **not integrated into firmware**.
No phone operations are performed by the build or tests.

## Private register ABI

`DegasPrcbRead64` is a position-independent AArch64 reader with no stack usage.
Its internal shared RAM guard uses BL/RET; the original caller LR is saved
in X17 and restored on every success/failure return. No external calls occur.
It is **not an ordinary C function**; a three-register return would need a
wrapper to comply with AAPCS C structure-return rules.

Inputs: X0 = aligned upper-region virtual address, X1 = captured TTBR1_EL1,
X2 = captured TCR_EL1. Outputs: X0 = status, X1 = 64-bit value, X2 = physical
address. Failure always zeros X1/X2. Clobbers X3–X17 and NZCV; preserves
X18–X30 and SP. It reads no system registers and writes no memory/register
configuration. Caller must preserve its own return address around BL normally.

Statuses: 0 success; 1 invalid VA/alignment; 2 reserved TTBR low bits;
3 invalid descriptor; 4 reserved/unsupported descriptor; 5 outside captured
ordinary RAM; 6 unsupported TCR.

Exact TCR gate is `0x00000695B5513511`: T1SZ17, TG1=4KiB, IPS48, no
TBI1/LPA2. Root is 2KiB/256 entries; TTBR bit11 is preserved. Later tables
are 4KiB/512 entries. Maximum is four descriptor reads plus one leaf read.
L1 1GiB blocks, L2 2MiB blocks, and L3 4KiB pages are supported. Invalid
types, L0 blocks, malformed block addresses, and PA bits outside this regime
are rejected. Permissions and AF are not enforced: this is an independent
physical diagnostic read, not emulation of an architectural load.

Every physical table span and every complete eight-byte read must fall in
one captured range, with overflow checked:

- `[0x40480000, 0x44440000)`
- `[0x140000000, 0x1BFFFE000)`
- `[0x1C0000000, 0x32D400000)`

These are platform-specific whitelists, not general RAM discovery. Physical
readability of those ranges and correct CPU execution/mapping state remain
hardware preconditions. No exception recovery is provided for a hardware
external abort in otherwise whitelisted DRAM. Snapshot TCR/TTBR once before
calling; the routine does not provide atomicity against Windows changing
page tables concurrently.

## Build and verify

From repository root, run in WSL:

```
bash diagnostic/degas-prcb-walk/build.sh
```

Then run host Python:

```
python diagnostic/degas-prcb-walk/test_walk.py
```

Tests emulate the actual linked bytes with Unicorn and monitor every data
read/write. They include root bit11, ASID/CnP, canonical VA with bit46 zero,
root indices0/255, all RAM-range final-eight-byte boundaries, descriptor
failures, MMIO/secure gaps, and random out-of-range leaf addresses. They
assert return within a bounded instruction budget and no memory writes.

The shared-guard build is 456 bytes (previous inlined guards:692 bytes),
SHA256 `47db1938a15eaf957b92e89fc0ccb0132f71eac3182e0c58bde7cdf938754f37`.
All159 cases pass, including preservation of every X18–X30 register and SP.

## Intended probes

Independently read loader block+0x88 to find PRCB. Validate every subsequent
address through this same routine; do not treat a virtual pointer as physical.
Then read PCR number from PRCB-0x980+0x9A4 and PRCB+0x8300/+0x1908.
PCR number is a DWORD: read an aligned eight-byte pair and extract the
appropriate half, because this API rejects non-eight-byte-aligned inputs.
Check pointer arithmetic/canonicality before each call and stop on failure.
Never alter any pointer or Windows data to suppress the candidate fault.
