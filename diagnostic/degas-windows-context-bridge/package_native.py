"""Verify the final linked native experiment, then package recoverable boot_b.

No phone commands, filesystem mounts or on-disk Windows patches occur here.
"""
from pathlib import Path
import argparse, hashlib, json, subprocess, sys, uuid
from bridge_fixture import source_mapped, TEMPLATE_START, TEMPLATE_END, PREFIX_ORIGINAL

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'artifacts/degas-native-context-v1'
def sha(data):
    return hashlib.sha256(data).hexdigest()
def run(*args):
    return subprocess.run([sys.executable, *args], cwd=ROOT, check=True, capture_output=True, text=True)

if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=OUT)
    parser.add_argument('--clock-step',action='store_true')
    parser.add_argument('--no-watchdog',action='store_true')
    args=parser.parse_args();OUT=args.output.resolve()
    manifest = json.loads((OUT / 'source-manifest.json').read_text())
    assert bool(manifest.get('dvfs_control_test',False))==args.clock_step
    cpus=manifest['cpu_count'];assert cpus in (2,8)
    assert all(sha((ROOT / path).read_bytes()) == value for path, value in manifest['sources'].items()), 'source changed after build identity'
    assert sha((ROOT / 'mu/Platforms/Xiaomi/degasPkg/Include/NativeContextIdentity.h').read_bytes()) == manifest['identity_header_sha256']
    log = (ROOT / 'evidence/build/degas-first-dxe.log').read_text(errors='replace')
    assert '\n- Done -\n' in log and '\n- Failed -\n' not in log
    for flag in ['DEGAS_NATIVE_CONTEXT_BRIDGE=2', 'DEGAS_CPU_PARKING_EXPERIMENT=0',
                 'DEGAS_CPU_TEST_LIMIT='+str(cpus), 'DEGAS_BOOT_CORE=0', 'DEGAS_DIAGNOSTIC_EBS_WATCHDOG='+str(int(not args.no_watchdog))]:
        assert '-D' + flag in log, flag
    topology=manifest.get('cpu_topology',False)
    assert '-DDEGAS_CPU_TOPOLOGY='+str(int(topology)) in log
    efi = (ROOT / 'mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64/DegasCheckpointDxe.efi').read_bytes()
    assert (b'CLK1 end status=' in efi)==args.clock_step, 'linked DVFS actuation and profile metadata disagree'
    acpi = (ROOT / 'mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64/DegasAcpiDxe.efi').read_bytes()
    smbios = (ROOT / 'mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64/DegasSmbiosDxe.efi').read_bytes()
    if topology:
        import re
        header=(ROOT/'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/CpuPpttGenerated.h').read_text()
        pptt=bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})(?![0-9a-fA-F])',header))
        assert len(pptt)==216 and sha(pptt)=='fef240c88e1496665c837016289b5814d03baca8328b3951868ca1c76f63091f'
        assert acpi.count(pptt)==1, 'exact topology table missing from final linked ACPI driver'
        assert b'SoC L3' in smbios, 'L3 inventory missing from final linked SMBIOS driver'
    assert b'CPU DVFS SNAPSHOT' in efi and b'DVF1 B src=UEFI' in efi
    image = source_mapped()
    assert efi.count(image[TEMPLATE_START:TEMPLATE_END]) == 1, 'exact original NT template absent from linked guard'
    assert efi.count(PREFIX_ORIGINAL) >= 1
    assert efi.count(uuid.UUID(manifest['build_id']).bytes) == 1, 'linked retained build ID mismatch'
    assert efi.count(bytes.fromhex(manifest['source_manifest_sha256'])) == 1, 'linked source identity mismatch'
    assert b'DEGAS NATIVE CONTEXT prepared' in acpi and b'DEGAS NATIVE CONTEXT complete' in efi
    # Assemble independently and require exact actual permission-probe bytes in
    # the linked image; this catches a stale or missing assembly source list.
    def wp(p):
        p = str(Path(p).resolve()).replace('\\', '/')
        return '/mnt/' + p[0].lower() + p[2:]
    asm = ROOT / 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/NativeTranslation.S'
    obj, binary = OUT / 'native-translation.o', OUT / 'native-translation.bin'
    for command in [
        ['clang', '--target=aarch64-none-elf', '-c', wp(asm), '-o', wp(obj)],
        ['llvm-objcopy', '-O', 'binary', '--only-section=.text', wp(obj), wp(binary)],
    ]:
        subprocess.run(['wsl', '-d', 'Ubuntu', '--', *command], check=True, capture_output=True)
    translation = binary.read_bytes()
    assert efi.count(translation) == 1, 'linked access qualifier differs from reviewed assembly'
    restore = ROOT / 'evidence/raw/2026-10-24-degas-return/boot_b.img'
    assert sha(restore.read_bytes()) == 'fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113'
    # Existing qualified wrapper and Android overlay package pipeline.
    intermediate=OUT.name+'-intermediate'
    run('tools/package_degas_first_dxe.py', intermediate)
    run('tools/package_degas_dualboot.py', 'artifacts/'+intermediate+'/kernel.bin.gz', str(OUT), '--boot-core', '0')
    run('tools/package_degas_boot.py', '--kernel', str(OUT / 'kernel.bin.gz'), '--stock', str(restore),
        '--output', str(OUT / 'boot-uefi.img'), '--manifest', str(OUT / 'boot-manifest.json'),
        '--restore-reference', str(OUT / 'restore.json'))
    run('tools/package_degas_stock_overlay.py', str(OUT / 'boot-uefi.img'), str(OUT / 'boot.img'))
    config = dict(build_id=manifest['build_id'], source_manifest_sha256=manifest['source_manifest_sha256'],
                  mode=2, cpu_count=cpus, boot_core=0, psci=True, parking=False,
                  cpu_topology=topology, dvfs_read_only_snapshot=not args.clock_step, dvfs_control_writes=args.clock_step,
                  dvfs_test_target_khz=2000000 if args.clock_step else None,
                  watchdog_after_ebs_seconds=0 if args.no_watchdog else 30, watchdog_disabled_by_windows_ufs=True, diagnostic_only=True,
                  checkpoint_efi_sha256=sha(efi), acpi_efi_sha256=sha(acpi),
                  smbios_efi_sha256=sha(smbios),
                  translation_sha256=sha(translation), exact_linked_guard_and_identity=True,
                  boot_image_sha256=sha((OUT / 'boot.img').read_bytes()),
                  stock_restore_image=str(restore), stock_restore_sha256=sha(restore.read_bytes()),
                  gz_tee_lk_preloader_untouched=True, windows_files_untouched=True,
                  limitations=['first-EBS-attempt only', 'currentEL1 stage1 access qualification only',
                               'Windows SMP and late integrity acceptance require hardware observation',
                               'cold '+str(cpus)+'-CPU proof only; sleep/hotplug/resume unqualified',
                               'Windows UFS driver disables watchdog; no guaranteed automatic reset after storage starts'])
    (OUT / 'build-config.json').write_text(json.dumps(config, indent=2) + '\n')
    print(json.dumps(config, indent=2))
