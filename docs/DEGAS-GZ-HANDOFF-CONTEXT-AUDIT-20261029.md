# Exact GZ handoff and CPU_ON context audit — 2026-10-29

Read-only binary audit; no phone operation, MMIO, firmware patch, or Windows
change. This report is the only new artifact. Root independently verified the
active ATF against the pinned ATF hash. GZ findings below are pinned to saved
`evidence/raw/2026-10-24-degas-return/gz_b.img`, full-partition SHA256
`011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e`.
Its MTK header declares payload length0x1B77C0, beginning at raw offset0x200.
All GZ addresses below are payload-relative; add0x200 for raw file offsets
(existing linked-image references use base0x38000000). Root's separate read-only
capture now verifies the active gz_b header and exact loaded payload against
this saved image: `evidence/raw/2026-10-29-degas-active-gz-identity/manifest.json`,
payload SHA256 `85802b8800ce948f36f463945dd9bf3dfd348073c771133b5f7e44d1baa57fd9`.

## Kernel handoff: GZ explicitly overrides the potential ATF EL2 exit

LK's established SMC has X0=C2000115, X1=kernel, X2=DTB, X3=0, X4=1.
The ATF handler at4880C724 is not the last word on the guest's exception level.

GZ3C5D8..3C600 compares `(SMC_ID & ~0x40000000)` with82000115, so both
SMC32/64 forms match. For context kind2 it branches to3C794, saves the entire
original caller register context, forwards through3C560, and resumes its
own Linux-launch construction. Relevant exact instructions:

| GZ relative | Instruction / effect |
|---|---|
| 3C800 | LDR X1,[X22,#20]: original caller X4 |
| 3C808 | CBNZ X1,3C890: any nonzero value selects AArch64 branch |
| 3C890..898 | MOV W0,#3C5; stores to current context+108 and Linux vCPU+1108 |
| 3C8A4..8B0 | original X1 becomes Linux PC+1100; original X2 becomes Linux X0+1000 |
| 3C8B4..8B8 | original X3 becomes Linux X1+1008 |
| 3C80C..814 | zero-X4 alternative writes1D3 (AArch32 SVC), not EL2 |

The adjacent exact strings are `trap lk2linux smc back!`,
`boot linux-64 via EL2` and `boot linux-32 via EL2`. Here “via EL2” describes
the hypervisor executing the launch, not a guest EL2 entry: guest SPSR3C5 is
EL1h. Changing kernel payload, X4 from1 to another nonzero value, or switching
the SMC32/64 ID cannot request EL2 through this branch.

ATF4880BF44's early gate is now bounded:48821EC0 returns
`(~word[4886A07C]) & 1`. The on-disk word is1. The inspected store at488216B4
copies the first U32 of boot-tag88610023, via4880C5A8; its diagnostic name is
`init_gz_info`. This is an earlier GZ boot-info input, not an EL-select bit in
the LK kernel-boot call. Even when ATF's normal path constructs SPSR3C9 from
hardware EL2 presence, GZ's subsequent guest construction above selects3C5.
ATF's kernel-boot handler also has a one-shot guard at4891216E; replaying the
call from an already running payload is not an established elevation route.

## CPU_ON: the saved GZ image discards the guest context argument

The ordinary CPU_ON64 trap reaches3D604..610, which recognizesC4000003 and
branches to3D6D8. CPU_ON32 joins the same branch through3D500..50C.

| GZ relative | Exact data flow |
|---|---|
| 3D6F4 | LDR X25,[X20,#8]: target MPIDR from trapped caller X1 |
| 3D708 | LDR X26,[X20,#10]: guest entry from trapped caller X2 |
| 3D73C..760 | memset(target Linux vCPU+1000,0,34C) |
| 3D764 | STR X26,[X23,#1100]: set target guest PC |
| 3D744/780..784 | choose3C5/1D3 and store target guest SPSR+1108 |
| 3D7A4..7C4 | native CPU_ON64 to GZ physical entry, with X3 explicitly0 |

The CPU_ON branch does not load `[X20,#18]` (trapped caller X3) or restore it
to the target guest register block at+1000. Consequently the zeroed guest X0
survives even though entry PC and affinity are retained. Native X3=0 alone
would not prove a bug—the hypervisor may legitimately use its own bootstrap
context—but the missing guest-X3-to-guest-X0 transfer in the vCPU construction
is the concrete loss. Root's independent ATF trace preserves native X3 through
its entry record and ERET, which places this discrepancy in GZ mediation.

The non-cold branch at3D87C also initializes a distinct hypervisor context;
its presence does not repair the already zeroed Linux vCPU register block.
This behavior fits all observed APs reaching their requested entry with X0=0
and explains why an Android path not requiring a nonzero context can succeed.

The guest-X0 interpretation is closed through the actual restore code:
Linux VM registration1D3FC/1D428 passes callback1BE78 as constructor argX4;
294D0 saves it at29500 and stores it in VM-object+40 at295CC. That callback
1BE78..88 returns the per-CPU Linux context base (GOT[1B7560]+TPIDR_EL2).
Restore297EC obtains the VM object,29808..810 invokes its+40 callback, and
2981C forms X22=that base+1000. At299B4 it sets SP=X22 and branches to1BF4.
The common return reaches1C7C, then1C84 executes `LDP X0,X1,[SP],#10`;
1C98/1C9C restore ELR_EL2/SPSR_EL2 and1CD4 executes ERET. Thus base+1000
really supplies architectural X0; it is not only a guessed structure field.

## HVC, signed container, and recovery boundary

The GZ exception table at payload79D10 routes EC12/16 (HVC32/64) to3CF84,
and EC13/17 (SMC32/64) to3CFD8. HVC initially passes X0..X7 to31238,
where module callbacks may claim calls; the unhandled route goes to3D30C.
The initial core owner4 table entry reaches the unsupported handler, not the
CPU_ON implementation described above. The bundled modules resolve the runtime
registration ambiguity: core API table849C0[+8] points to300FC, the module
`gz_eh_hvc_register(client_id, owner, handler)` API. It saves original W1, passes
it as W0 at30144, and calls owner-table registration2F7E0 at3014C. The latter
indexes owner0..63 and installs the supplied function in both call tables.

The saved container's `unmap2` ELF begins at raw39DCC0, length15400, SHA256
`46effa7284cd82e47a4d879b5c8cf9e5eaa6e9aa4c013260a2122d51fa899097`.
Its initializer37C012B4 registers **owner4**, handler37C01834, through this API
at37C01318..32C. That handler recognizes full IDs84000001/2/3, but interprets
them as custom mapping operations, not PSCI CPU suspend/off/on. For example,
the84000003 branch37C01960 walks mapping descriptor arrays. It does not match
C4000003,84000000, or8400000A.

For these unmatched IDs,37C01894..8A4 only writes its output buffer
`[2, original_function_id, 0, 0]`, restores registers, and returns. This handler
path has no module-global or guest-state writes and no argument-pointer
dereference. Core3D354..370 copies that output to caller X0..X3. Thus a VERSION
query is predicted to return X0=2 and X1=84000000; interpreting this as PSCI0.2
would be a namespace collision. A gate accepting only major version1 rejects
it. Direct FEATURES is likewise predicted to return2. The module handler itself
does not make these query IDs destructive, but the earlier generic callback
route remains part of the actual execution path and was audited separately.

`mkp_service` similarly registers owner2; its ELF starts raw1B8A70, length1E41A0,
SHA256 `6fa149e648ca460a8296369424db26bc87b48014bba45a34e6eb7af2384f97c1`.
No standard PSCI-over-HVC provider is established. These module bytes are pinned
to the saved full container; the active identity capture above verifies the GZ
core, not yet the module payloads. The retained log proves authenticated module
loading on that saved boot. FADT ARM_BOOT_ARCH=3 is therefore not an established
workaround, and vendor HVC support does not justify invoking CPU_ON over HVC.

The retained `evidence/raw/2026-09-14-memory-display/pl_lk.txt` records the
security and boot order, despite its filename:

- line6896: `starting app bl2_ext_boot`;
- lines6920..6929: image authentication required, SBC enabled, GZ certificate
  chain verification, header authentication and image authentication pass;
- line6930: gz_b is loaded; line6966 says GZ will boot;
- lines6980..6990: LK BL33 image is authenticated and loaded afterward;
- line7534: `starting app fastboot`.

Thus GZ loads before the LK fastboot application. A rejected or broken GZ
image may prevent reaching fastboot; the existing boot.img recovery technique
does not establish recovery from that failure. Bootloader unlock has not been
shown to disable this earlier authentication policy.

Raw offset1B79C0, immediately after the GZ executable payload, is another MTK
header named `cert1`, declaring0x6C5 payload bytes. Further signed components
include mkp_service and unmap2, also authenticated in the retained log. This
container tail is not free executable padding. No region has been proven both
unreferenced executable padding and safe to replace; consequently no binary
trampoline, code cave, or modified GZ image is produced. The minimal intended
source semantics are loading trapped guest X3 and storing it to the target
Linux register block's X0 after its initialization. Turning those two memory
operations into a safe binary insertion still needs an independently proved
placement and a valid authenticated-container construction path.

## Feasibility conclusion

The existing LK kernel handoff is a GZ guest launch at EL1, with no EL2 request
variant in the inspected path. Both initial guest launch and AP guest launch
choose EL1. An EL2 PSCI relay cannot be installed by changing only the current
EL1 UEFI payload or FADT HVC flag. A different earlier boot chain that supplies
EL2 before GZ owns it, or a correction inside the resident GZ PSCI emulation,
would be required. No supported public/runtime GZ API granting guest EL2 was
established by this bounded audit.

The source-level repair responsibility is now precise: preserve guest CPU_ON
X3 into the selected guest vCPU's X0 while retaining proper hypervisor bootstrap
and power-state semantics. This report does not authorize or propose a raw
firmware byte patch. Complete cold/warm-path validation and boot-chain
acceptance/recovery would precede any separate implementation.
It provides no basis for patching a Windows NULL callback or scheduler field.
