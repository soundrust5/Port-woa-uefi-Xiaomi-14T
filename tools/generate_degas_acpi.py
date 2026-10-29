"""Single-CPU diagnostic ACPI from captured degas DT and UEFI boot evidence."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from prepare_degas_windows_facts import ROOT, DT, HISTORY, extract, gtdt_draft
from generate_op12_acpi import run_iasl, wsl_path, header_array, DEFAULT_IASL

LOG = ROOT / 'evidence/raw/2026-10-25-degas-platform-facts-v1-result/console-ramoops-0'
HEADER = ROOT / 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe/AcpiTablesGenerated.h'


def table(sig, revision, body):
    data = bytearray(struct.pack('<4sIBB6s8sI4sI', sig, 36 + len(body), revision,
                                0, b'XIAOMI', b'DEGAS   ', 1, b'DGAS', 1) + body)
    data[9] = -sum(data) & 255
    return bytes(data)


def boot_evidence(text, facts):
    cpu = re.findall(r'DEGAS BOOT CPU mpidr=([0-9a-fA-F]+) midr=([0-9a-fA-F]+) cntfrq=(\d+)', text)
    gic = re.findall(r'DEGAS BOOT GICR first=([0-9a-fA-F]+) typer=([0-9a-fA-F]+) affinity=([0-9a-fA-F]+) boot_match=(\d+)', text)
    if len(cpu) != 1 or len(gic) != 1:
        raise ValueError('need exactly one complete boot CPU/GICR observation')
    mpidr, midr = [int(x, 16) for x in cpu[0][:2]]
    frequency = int(cpu[0][2])
    base, typer, recorded_affinity = [int(x, 16) for x in gic[0][:3]]
    affinity = (mpidr & 0xffffff) | ((mpidr >> 8) & 0xff000000)
    if (int(gic[0][3]) != 1 or typer >> 32 != affinity or recorded_affinity != affinity
            or frequency != facts['timer']['dt_frequency_hz']
            or base != int(facts['gic']['redistributor_region']['start'], 0)):
        raise ValueError('runtime CPU/GICR/timer evidence does not agree')
    # First stage deliberately supports only the observed boot affinity 0.
    if affinity != 0 or not any(int(c['mpidr'], 0) == 0 for c in facts['cpus']):
        raise ValueError('single-CPU degas boot profile requires affinity 0')
    return dict(mpidr=mpidr, affinity=affinity, midr=midr, frequency=frequency, gicr=base)


def fadt():
    data = bytearray(276 - 36)
    struct.pack_into('<I', data, 112 - 36, 1 << 20)  # hardware reduced
    struct.pack_into('<H', data, 129 - 36, 1)        # PSCI compliant, SMC
    data[131 - 36] = 3                             # ACPI 6.3 minor revision
    # DSDT/XDSDT are patched by AcpiTableDxe. No invented sleep/reset GAS.
    return table(b'FACP', 6, data)


def madt(facts, boot, all_cpus=False):
    cpus=facts['cpus'] if all_cpus else [facts['cpus'][0]]
    gicc=bytes()
    for uid,cpu in enumerate(cpus):
        mpidr=int(cpu['mpidr'],0)
        # Runtime firmware MUST fill secondary redistributor addresses after
        # matching GICR_TYPER affinity. Never infer them from CPU index.
        base=boot['gicr'] if uid==0 else 0
        efficiency=0 if 'arm,cortex-a510' in cpu['compatible'] else 1
        gicc += struct.pack('<BBHIIIIIQQQQIQQBBH',
                           11,80,0,0,uid,1,0,0,0,0,0,0,
                           facts['gic']['maintenance']['intid'],base,
                           mpidr,efficiency,0,0)
    gicd = struct.pack('<BBHIQIB3s', 12, 24, 0, 0,
                       int(facts['gic']['distributor']['start'], 0), 0, 3, bytes(3))
    return table(b'APIC', 5, struct.pack('<II', 0, 0) + gicc + gicd)


DSDT = '''// Diagnostic boot CPU only. No fabricated MediaTek peripheral resources.
DefinitionBlock ("", "DSDT", 2, "XIAOMI", "DEGAS", 1)
{
    Scope (\\_SB)
    {
        Device (C000)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, Zero)
        }
    }
}
'''

UFS_ENUM = '''
        // Enumeration only: no generic class CID, no automatic driver binding.
        // Captured MT6897 DT: HCI 112b0000/3000, legacy SPI168 => INTID200.
        Device (UFS0)
        {
            Name (_HID, "DGS0001")
            Name (_UID, Zero)
            Name (_DDN, "MediaTek MT6897 UFS diagnostic")
            Name (_STA, 0x0F)
            Name (_CCA, Zero)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x112B0000, 0x3000)
                // Temporary 30s recovery timer for read-only Windows probe.
                Memory32Fixed (ReadWrite, 0x1C00A000, 0x1000)
                Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 200 }
            })
        }
'''

TOUCH_ENUM = '''
        // Qualified FT3683 FIFO transport. No DMA or interrupt resource.
        // Resources are ordered SPI2, GPIO, TOPCKGEN, PERICFG, PMIF.
        // The dedicated driver owns these pages; no generic SPI/GPIO CID.
        Device (TCH0)
        {
            Name (_HID, "DGS0002")
            Name (_UID, Zero)
            Name (_DDN, "Xiaomi 14T FT3683 touch")
            Name (_STA, 0x0F)
            Name (_CCA, Zero)
            Name (_CRS, ResourceTemplate ()
            {
                Memory32Fixed (ReadWrite, 0x11012000, 0x1000)
                Memory32Fixed (ReadWrite, 0x10005000, 0x1000)
                Memory32Fixed (ReadWrite, 0x10000000, 0x1000)
                Memory32Fixed (ReadWrite, 0x11036000, 0x1000)
                Memory32Fixed (ReadWrite, 0x1CC04000, 0x1000)
            })
        }
'''


def dsdt_source(ufs_enumeration, dt, touch_enumeration=False, all_cpus=False):
    source = DSDT
    if all_cpus:
        first='''        Device (C000)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, Zero)
        }'''
        cpus='\n'.join('''        Device (C%03X)
        {
            Name (_HID, "ACPI0007")
            Name (_UID, %d)
        }'''%(uid,uid) for uid in range(8))
        assert source.count(first)==1
        source=source.replace(first,cpus).replace('Diagnostic boot CPU only.','Eight DT CPUs; GICR and PSCI validated at runtime.')
    if touch_enumeration:
        expected = {
            '/soc/spi2@11012000': (0x11012000, 0x100),
            '/soc/gpio@10005000': (0x10005000, 0x1000),
            '/soc/topckgen@10000000': (0x10000000, 0x1000),
            '/syscon@11036000': (0x11036000, 0x1000),
            '/soc/spmi@1cc04000': (0x1cc04000, 0x8ff),
        }
        for path, resource in expected.items():
            if bytes.fromhex(dt['nodes'][path]['reg']['hex'])[:16] != struct.pack('>QQ', *resource):
                raise ValueError('Touch MMIO evidence changed: ' + path)
        source = source.replace('    }\n}', TOUCH_ENUM + '    }\n}')
    if not ufs_enumeration:
        return source
    node = dt['nodes']['/soc/ufshci@112b0000']
    if (bytes.fromhex(node['reg']['hex']) != struct.pack('>QQ', 0x112b0000, 0x3000)
            or bytes.fromhex(node['interrupts']['hex'])[:16] != struct.pack('>IIII', 0,168,4,0)):
        raise ValueError('UFS MMIO/legacy IRQ evidence changed')
    # Conservative noncoherent declaration; no DMA occurs without a driver.
    if 'dma-coherent' in node or 'iommus' in node:
        raise ValueError('Reaudit UFS DMA/coherency contract before publishing')
    return source.replace('    }\n}', UFS_ENUM + '    }\n}')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--output', type=Path, default=ROOT / 'artifacts/degas-acpi-v1/tables')
    ap.add_argument('--header', type=Path, default=HEADER)
    ap.add_argument('--boot-log', type=Path, default=LOG)
    ap.add_argument('--ufs-enumeration', action='store_true', help='Publish DGS0001 only; no driver CID or AML hardware access')
    ap.add_argument('--touch-enumeration', action='store_true', help='Publish experimental DGS0002 FT3683 polling driver resources')
    ap.add_argument('--all-cpus',action='store_true',help='Eight CPU template; runtime GICR/PSCI gates required')
    args = ap.parse_args()
    facts = extract(json.loads(DT.read_text()), json.loads(HISTORY.read_text()))
    boot = boot_evidence(args.boot_log.read_text(), facts)
    args.output.mkdir(parents=True, exist_ok=True)
    source = args.output / 'DSDT.asl'
    source.write_text(dsdt_source(args.ufs_enumeration, json.loads(DT.read_text()), args.touch_enumeration,args.all_cpus))
    compile_log = run_iasl(DEFAULT_IASL, ['-ve', '-p', wsl_path(args.output / 'DSDT'), wsl_path(source)], ROOT)
    (args.output / 'iasl.log').write_text(compile_log)
    tables = {'Dsdt': (args.output / 'DSDT.aml').read_bytes(), 'Fadt': fadt(),
              'Madt': madt(facts, boot,args.all_cpus), 'Gtdt': gtdt_draft(facts)}
    report = {'status': 'diagnostic single boot CPU; not a complete Windows platform',
              'ufs_enumeration_only': args.ufs_enumeration,
              'touch_enumeration': args.touch_enumeration,
              'boot': boot, 'sources': [{'path': str(p), 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
                                        for p in [DT, HISTORY, args.boot_log]], 'tables': {}}
    report['cpu_template_count']=8 if args.all_cpus else 1
    report['status']='runtime-gated CPU template; hardware test pending'
    for name, data in tables.items():
        if len(data) < 36 or struct.unpack_from('<I', data, 4)[0] != len(data) or sum(data) & 255:
            raise ValueError('invalid ACPI table: ' + name)
        (args.output / (name + '.bin')).write_bytes(data)
        report['tables'][name] = {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}
    args.header.parent.mkdir(parents=True, exist_ok=True)
    args.header.write_text('// Generated by tools/generate_degas_acpi.py; do not edit.\n'
                          + '#define DEGAS_ACPI_CPU_COUNT '+str(report['cpu_template_count'])+'\n'
                          + '\n\n'.join(header_array('mDegas' + name, data) for name, data in tables.items()) + '\n')
    (args.output / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
