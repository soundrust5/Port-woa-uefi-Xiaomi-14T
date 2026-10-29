# Frozen host-only cache probe

Cache investigation was stopped at the user's request to prioritize CPU
frequency work. These artifacts are retained for possible later use. They are
not linked into firmware and contain no hardware measurements.

```powershell
python diagnostic/degas-cache-probe/build.py
python diagnostic/degas-cache-probe/test_probe.py
```

Windows builds use the existing WSL clang/binutils; Linux can use `python3`.
Tests use the repository's existing Unicorn/Capstone packages.

- `CacheProbe.S`: 912-byte stackless AArch64 probe. CPU0 uses the returning
  `DegasCacheCollect` leaf; AP1..7 can use the independent PC-relative mailbox
  entry followed by genuine CPU_OFF. CPU0 is explicitly excluded from CPU_OFF.
- `ProbeAbi.h`: 512-byte private record layout for an eventual integration.
- `decode.py`: legacy/CCIDX decoding, selector validation and a host deadline
  monitor. It reports maintenance geometry and does not infer physical cache
  capacity or sharing. An outer retained journal/identity/CRC would still be
  required for qualified hardware evidence.
- `generated/`: source/binary hashes, disassembly and passing host results.

Current validation: 11 methods, 70 assembled instruction execution cases.
Registers, traps and CPU_OFF outcomes are synthetic fixture inputs. No clock
MMIO, AT/DC/IC/TLBI, GZ changes, phone actions or shared firmware edits occur.

See [DEGAS-CACHE-PROBE-20261029.md](../../docs/DEGAS-CACHE-PROBE-20261029.md).
