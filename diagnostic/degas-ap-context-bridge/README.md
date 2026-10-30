# per-MPIDR AP context bridge (offline)

`ApContext.S` is a 448-byte AArch64 diagnostic prototype. It independently
finds a published descriptor through MPIDR, restores its complete 64-bit
context to X0, and branches to its real physical AP entry. The original
incoming X0 is never used as a pointer or input. Nothing in this directory is
linked into firmware, a boot image, or Windows.

From the repository root, using Windows Python plus the existing WSL compiler:

```powershell
python diagnostic/degas-ap-context-bridge/build.py
python diagnostic/degas-ap-context-bridge/test_bridge.py
```

Linux/WSL can run those same commands with `python3`. Tests use the repository's
existing vendored Unicorn and Capstone. `generated/ap-context.json` pins the
source and assembled binary; tests refuse mismatched fixtures.

`bridge.py` is a **host publication model**, including required cache-operation
events and native CPU_ON arguments/status. Those events do not perform real
cache maintenance. Its intended RX code/XN data mapping constants are tested
but no page tables are installed. AP tests execute assembled instructions;
system registers and backing-RAM values are fixture inputs.

Current result: 11 test methods, 109 AP instruction execution cases; six also
execute the pinned original GZ CPU_ON handler and final register-restore
instructions before feeding the resulting X0=0/entry PC into the AP fixture.
No full GZ world-switch, power-state, cache, MMU, or interconnect is emulated.

The assembly requires EL1, little endian, and MMU/data/instruction caches off.
The publisher must reserve and retain physical code/data pages, preserve the
real caller's register/stack contract, serialize a CPU's outstanding request,
publish code/config/descriptor to PoC before native CPU_ON, and keep each
published slot immutable for its boot epoch. There is deliberately no runtime
Windows interception mechanism here. This prototype alone does not fix
Windows startup or establish eight-core Windows operation.

Detailed contract and limits: [DEGAS-AP-CONTEXT-TRAMPOLINE-20261029.md](../../docs/DEGAS-AP-CONTEXT-TRAMPOLINE-20261029.md).
