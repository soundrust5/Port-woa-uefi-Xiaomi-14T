"""Record actual module outputs; never identifies donor artifacts as degas firmware."""
import hashlib
import json
import re
import shutil
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT / 'mu/Build/stonePkg/DEBUG_CLANGPDB/AARCH64/SiliciumPkg'
DEST = ROOT / 'artifacts/mu-module-check'
DEST.mkdir(parents=True, exist_ok=True)
modules = []
for name, relative in [('Sec', 'Sec/Sec'), ('SimpleFbDxe', 'Drivers/SimpleFbDxe/SimpleFbDxe')]:
    source = BUILD / relative / 'OUTPUT' / (name + '.efi')
    data = source.read_bytes()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    assert data[:2] == b'MZ' and data[pe:pe+4] == b'PE\0\0'
    machine = struct.unpack_from('<H', data, pe+4)[0]
    assert machine == 0xaa64, hex(machine)
    assert struct.unpack_from('<H', data, pe+24)[0] == 0x20b
    log = ROOT / 'evidence/build' / ('mu-' + name + '.log')
    assert '- Done -' in log.read_text()
    shutil.copy2(source, DEST / source.name)
    modules.append(dict(name=name, file=str((DEST/source.name).relative_to(ROOT)),
                        size=len(data), sha256=hashlib.sha256(data).hexdigest(), machine='AARCH64', format='PE32+', log=str(log.relative_to(ROOT))))
pcds = {}
for name, relative in [('serial', 'Library/FrameBufferSerialPortLib/FrameBufferSerialPortLib'), ('gop', 'Drivers/SimpleFbDxe/SimpleFbDxe')]:
    header = (BUILD / relative / 'DEBUG/AutoGen.h').read_text()
    values = dict(re.findall(r'^#define _PCD_VALUE_(PcdFrameBuffer\w+)\s+(\d+)U?$', header, re.M))
    for key, expected in [('Width',1220),('Height',2712),('ColorDepth',32),('PixelsPerScanLine',1248)]:
        assert int(values['PcdFrameBuffer'+key]) == expected
    pcds[name] = {key:int(value) for key,value in values.items()}
report = dict(date='2026-09-14', build='DEBUG CLANGPDB AARCH64, stone dependency graph, module-only', modules=modules, actual_autogen_pcds=pcds,
              phone_tested=False, degas_firmware=False,
              limitations=['Donor memory map and platform hooks remain.', 'Compilation does not validate LK handoff, display refresh, cache policy or ownership.', 'No Android boot image produced.'],
              warning='Donor ArmPsciResetSystemLib does not declare HwResetSystemLib class; build succeeded.')
fd_source = ROOT/'mu/Build/stonePkg/RELEASE_CLANGPDB/FV/SILICIUM_UEFI.fd'
fd_log = ROOT/'evidence/build/mu-control-fd.log'
if fd_source.exists() and fd_log.exists() and '- Done -' in fd_log.read_text():
    data = fd_source.read_bytes()
    assert len(data) == 0x200000
    assert data[40:44] == b'_FVH'
    assert struct.unpack_from('<Q', data, 32)[0] == len(data)
    header_length = struct.unpack_from('<H', data, 48)[0]
    assert header_length >= 56 and header_length % 2 == 0
    assert sum(struct.unpack('<'+'H'*(header_length//2), data[:header_length])) & 0xffff == 0
    fd_dest = ROOT/'artifacts/control-stone/STONE_CONTROL_ONLY.fd'
    fd_dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(fd_source, fd_dest)
    report['control_fd'] = dict(file=str(fd_dest.relative_to(ROOT)), size=len(data), sha256=hashlib.sha256(data).hexdigest(),
        build='RELEASE CLANGPDB AARCH64, donor stone PCDs, four bundled Basecore patches applied',
        fv_header_checksum_valid=True, degas_firmware=False, phone_tested=False)
(ROOT/'evidence/mu-build-validation-2026-09-14.json').write_text(json.dumps(report, indent=2)+'\n')
print(json.dumps(report, indent=2))
