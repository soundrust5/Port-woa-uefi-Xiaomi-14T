"""Generate fresh build identity for the bounded native cold-start experiment."""
from pathlib import Path
import argparse, hashlib, json, uuid

ROOT = Path(__file__).resolve().parents[2]
FILES = [
 'mu/Platforms/Xiaomi/degasPkg/Include/NativeContextCore.h',
 'mu/Platforms/Xiaomi/degasPkg/Include/NativeContextFixture.h',
 'mu/Platforms/Xiaomi/degasPkg/Include/NativeContextRecord.h',
 'mu/Platforms/Xiaomi/degasPkg/Include/Protocol/DegasNativeContext.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/NativeContext.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/DegasAcpiDxe.c',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/CpuPpttGenerated.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasSmbiosDxe/DegasSmbios.c',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/Checkpoint.c',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CpuDvfsSnapshot.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/DvfsPmuRead.S',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CpuDvfsStep.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UefiCsramSnapshot.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/UefiCsramJournal.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/NativeContextBoot.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/NativeTranslation.S',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/CheckpointDxe.inf',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/WindowsBoot.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/WindowsViewport.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/ApJournalRetention.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/OsHandoff.h',
 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/AcpiTablesGenerated.h',
 'mu/Platforms/Xiaomi/degasPkg/Include/ExpandedRam.inc',
 'mu/Platforms/Xiaomi/degasPkg/Library/MemoryMapLib/MemoryMapLib.c',
 'mu/Platforms/Xiaomi/degasPkg/Library/RamSerialPortLib/RamSerialPortLib.c',
 'mu/Platforms/Xiaomi/degasPkg/degas.dsc',
 'tools/build_degas_first_dxe.sh',
 'diagnostic/degas-windows-context-bridge/bridge_fixture.py',
 'diagnostic/degas-windows-context-bridge/generate_identity.py',
]
def sha(data):
    return hashlib.sha256(data).hexdigest()
def array(name, values):
    return 'STATIC CONST UINT8 ' + name + '[' + str(len(values)) + ']={' + ','.join('0x%02x' % n for n in values) + '};\n'
if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT / 'artifacts/degas-native-context-v1')
    parser.add_argument('--cpus', type=int, choices=[2,8], default=2)
    parser.add_argument('--topology', action='store_true')
    args = parser.parse_args()
    sources = {p: sha((ROOT / p).read_bytes()) for p in FILES}
    digest = sha(json.dumps(sources, sort_keys=True, separators=(',', ':')).encode())
    seed = uuid.uuid4()
    text = '// Generated fresh before build; identity does not certify a live Windows boot.\n'
    text += array('mDegasNativeBuildId', seed.bytes)
    text += array('mDegasNativeSourceHash', bytes.fromhex(digest))
    header = ROOT / 'mu/Platforms/Xiaomi/degasPkg/Include/NativeContextIdentity.h'
    header.write_text(text)
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'source-manifest.json').write_text(json.dumps(dict(
        build_id=str(seed), source_manifest_sha256=digest, sources=sources,
        identity_header_sha256=sha(header.read_bytes()), cpu_count=args.cpus, boot_core=0,
        cpu_topology=args.topology, dvfs_read_only_snapshot=True,
        native_psci=True, early_firmware_unchanged=True), indent=2) + '\n')
    print(seed, digest)
