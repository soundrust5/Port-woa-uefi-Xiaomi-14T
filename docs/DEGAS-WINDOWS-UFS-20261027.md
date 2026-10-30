# Windows UFS driver

The MT6897 Storport miniport supports disk discovery, reads, writes and booting
installed Windows. Driver sources: `diagnostic/degas/ufs/windows/`.

## Controller and handoff

- ACPI ID `DGS0001`, MMIO `0x112b0000/0x3000`, GIC INTID 200, CCA 0.
- UEFI stops both request queues, disables interrupts and clears firmware DMA
  bases at EBS. It retains the initialized PHY/link only when no command is
  outstanding and the link is idle.
- Storport owns DMA mapping. The miniport uses its mapped request buffer and
  validates transfer lengths, LUN, response type and completion status.

## Enumeration fix

The earlier driver bypassed Storport's mapped `SrbDataBuffer` using the original
MDL. That selected the wrong CPU buffer: REPORT LUNS data reached DMA memory
but not the buffer Storport consumed. Changing descriptor width alone did not
resolve discovery.

V30 used the actual mapped buffer. Windows then enumerated all three LUNs and
passed inquiry, capacity and READ10. The capture recorded 69 completed data
commands and zero timeouts. Reference capture:
`evidence/raw/2026-10-27-degas-ufs-v30-read-pass`.

## Write validation

| Test | Result |
| --- | --- |
| V31: 4 KiB write, flush and readback | PASS; independently verified after reboot |
| V31 neighboring blocks and both GPT copies | Unchanged |
| V32: 675-second stress run | PASS, zero reported errors |
| V32: 65,536-block verification | Zero mismatches; guard blocks and GPT unchanged |

Write tests were initially restricted to a dedicated scratch extent. That
extent now belongs to the Windows NTFS filesystem. **Do not run the old raw
scratch/journal writers on the installed Windows partition.**

The later filesystem mode permits writes only within the configured Windows
and ESP ranges and implements flush, shutdown and FUA handling. It must not be
replaced with unrestricted whole-LUN writes.

## Performance

Timer-based completion limited early versions. Short completion polling was
an intermediate experiment; interrupt/DPC completion later raised observed
application-load throughput from roughly 10 to 50 MB/s. This is an observed
workload result, not a sequential UFS benchmark. Queue depth, request size and
buffer copying remain optimization targets.

## References

- [DMA and buffer audit](DEGAS-DMA-AUDIT-20261026.md)
- [Performance notes](DEGAS-UFS-PERFORMANCE-20261027.md)
- Local baselines: `read-pass-v30`, `write-pass-v31`, `stress-pass-v32` under
  `artifacts/degas/ufs-windows/`.

Historical fixed LBAs and slot B in the captures describe the test phone,
not installation defaults for another device. Sleep/resume requires separate
qualification.
