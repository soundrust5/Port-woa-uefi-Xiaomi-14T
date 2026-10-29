"""Build a second probe without replacing the preserved first-test outputs."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

HERE=Path(__file__).resolve().parent
OUT=HERE/'build-readback'
def run(args): subprocess.run(args,cwd=HERE,check=True)
def main():
    OUT.mkdir(exist_ok=True)
    inputs=['entry.S','link.ld','build_readback.py','marker.c','readback.c','framebuffer.c',
            'include/marker.h','include/readback.h','include/framebuffer.h','include/degas_display.h']
    hashes={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in inputs}
    build_id=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).digest()
    (OUT/'build-id.bin').write_bytes(build_id)
    common=['clang','--target=aarch64-none-elf','-march=armv8-a','-mgeneral-regs-only']
    run(common+['-DDIAGNOSTIC_READBACK','-DBUILD_ID_FILE="build-readback/build-id.bin"','-c','entry.S','-o','build-readback/entry.o'])
    for name in ['marker','readback','framebuffer']:
        run(common+['-std=c11','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin',
            '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
            '-fno-pic','-O2','-I','include','-c',name+'.c','-o','build-readback/'+name+'.o'])
    run(['ld.lld','-T','link.ld','--build-id=none','-o','build-readback/entry.elf']+
        ['build-readback/'+name+'.o' for name in ['entry','marker','readback','framebuffer']])
    run(['llvm-objcopy','-O','binary','build-readback/entry.elf','build-readback/entry.bin'])
    data=(OUT/'entry.bin').read_bytes()
    assert data[56:60]==b'ARM\x64' and struct.unpack_from('<Q',data,16)[0]==len(data)
    sink=io.BytesIO()
    with gzip.GzipFile(fileobj=sink,filename='',mode='wb',mtime=0,compresslevel=9) as gz: gz.write(data)
    compressed=sink.getvalue();assert gzip.decompress(compressed)==data
    (OUT/'entry.bin.gz').write_bytes(compressed)
    symbols={}
    for line in subprocess.check_output(['llvm-nm','-n',str(OUT/'entry.elf')],text=True).splitlines():
        parts=line.split()
        if len(parts)==3 and parts[2] in ['diagnostic_readback_ok','diagnostic_readback_failed','diagnostic_unsupported','__stack_end']:
            symbols[parts[2]]=hex(int(parts[0],16))
    report=dict(build_id=build_id.hex(),inputs=hashes,image_bytes=len(data),image_sha256=hashlib.sha256(data).hexdigest(),
        gzip_bytes=len(compressed),gzip_sha256=hashlib.sha256(compressed).hexdigest(),symbols=symbols,
        phone_tested=False,status='EXPERIMENTAL readback probe; status loops are relative to actual load base',
        limitations=['Readback compares guest-visible addresses; does not prove stage-2 PA or panel refresh.',
                    'Watchdog report needs separate recovery; this build never flashes or reboots.'])
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
if __name__=='__main__': main()
