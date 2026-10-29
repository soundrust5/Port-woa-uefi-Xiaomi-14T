# Host-only MPIDR context bridge feasibility fixture

Build with `wsl bash diagnostic/degas-linux-context-design/build.sh` from the
workspace root, then run `python diagnostic/degas-linux-context-design/test_bridge.py`.
Outputs remain in this directory's `out/` folder.

The fixture ignores incoming X0, masks the architectural MPIDR affinity fields,
finds exactly one matching bounded shared-memory record, restores its full
64-bit context into X0, then branches to the record's original physical entry.
The zero context is valid. Unknown/duplicate identities, invalid row count,
and zero/unaligned entry addresses take a local failure loop. It performs no
memory writes, uses no stack and preserves X18-X30 and SP.

Tests execute the assembled AArch64 lookup and restore instructions in
Unicorn, supplying MPIDR at the MRS instruction. They do **not** implement or
prove the missing Windows integration point, coherency, cache publication,
power management, page-table transitions, or execution on MT6897. This is
not a Windows patch, UEFI image, PSCI provider, or deployable firmware shim.
No original Windows, GZ or UEFI bytes are changed by these tests.

`wsl python3 diagnostic/degas-linux-context-design/test_native_prepare.py`
also compiles the actual current `DegasAcpiDxe/NativeContext.h` body with UBSan
and firmware stubs. Its 268 preparation cases run independently for two-CPU and
eight-CPU configurations (134 each) and cover complete MPIDRs, every
GICC's UID/interface/type/length/enable fields, FADT/MADT gates, every CPU's
affinity state, allocation failure/invalid addresses, exactly eight requested
NVS pages, initialization and cache-clean ordering, and exactly the selected
number of published mailbox addresses. An unsupported three-CPU build limit is
also rejected before queries or allocation. Every recorded SMC is
AFFINITY_INFO64; CPU_ON is never requested.
It preserves the real firmware sources and writes only its local result JSON.
The low-address host allocation and query/cache stubs do not establish live
UEFI ownership, device coherency, or actual PSCI behavior.
