"""Build a ramoops retention probe, preserving both earlier test images."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

HERE=Path(__file__).resolve().parent
OUT=HERE/'build-mu-handoff'
def run(args): subprocess.run(args,cwd=HERE,check=True)
def main():
    OUT.mkdir(exist_ok=True)
    inputs=['entry_ramoops.S','link.ld','build_mu_handoff.py','ramoops.c','mu_handoff.c','ramoops_probe.c',
            'include/ramoops.h']
    hashes={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in inputs}
    build_id=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).digest()
    (OUT/'build-id.bin').write_bytes(build_id)
    common=['clang','--target=aarch64-none-elf','-march=armv8-a','-mgeneral-regs-only','-mstrict-align']
    run(common+['-DBUILD_ID_FILE="build-mu-handoff/build-id.bin"','-c','entry_ramoops.S','-o','build-mu-handoff/entry.o'])
    for name in ['ramoops','mu_handoff']:
        run(common+['-std=c11','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin',
            '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
            '-fno-pic','-O2','-I','include','-c',name+'.c','-o','build-mu-handoff/'+name+'.o'])
    run(['ld.lld','-T','link.ld','--build-id=none','-o','build-mu-handoff/entry.elf']+
        ['build-mu-handoff/'+name+'.o' for name in ['entry','ramoops','mu_handoff']])
    run(['llvm-objcopy','-O','binary','build-mu-handoff/entry.elf','build-mu-handoff/entry.bin'])
    data=(OUT/'entry.bin').read_bytes()
    assert data[56:60]==b'ARM\x64' and struct.unpack_from('<Q',data,16)[0]==len(data)
    sink=io.BytesIO()
    with gzip.GzipFile(fileobj=sink,filename='',mode='wb',mtime=0,compresslevel=9) as gz: gz.write(data)
    compressed=sink.getvalue();assert gzip.decompress(compressed)==data
    (OUT/'entry.bin.gz').write_bytes(compressed)
    symbols={}
    for line in subprocess.check_output(['llvm-nm','-n',str(OUT/'entry.elf')],text=True).splitlines():
        parts=line.split()
        if len(parts)==3 and parts[2] in ['diagnostic_ramoops_ok','diagnostic_ramoops_failed','diagnostic_unsupported','__stack_end']:
            symbols[parts[2]]=hex(int(parts[0],16))
    report=dict(build_id=build_id.hex(),inputs=hashes,image_bytes=len(data),image_sha256=hashlib.sha256(data).hexdigest(),
        gzip_bytes=len(compressed),gzip_sha256=hashlib.sha256(compressed).hexdigest(),symbols=symbols,
        phone_tested=False,status='EXPERIMENTAL single-frame command-mode display probe; status loops are relative to actual load base',
        limitations=['Writes framebuffer marker and guarded DSI/mutex trigger plus retained console. Hardware outcome unverified.',
                    'Watchdog report needs separate recovery; this build never flashes or reboots.'])
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__': main()
