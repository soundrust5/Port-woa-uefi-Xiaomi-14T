"""Build the experimental framebuffer probe; no phone operations."""
import gzip
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess

HERE=Path(__file__).resolve().parent
OUT=HERE/'build-marker'
def run(args): subprocess.run(args,cwd=HERE,check=True)
def main():
    OUT.mkdir(exist_ok=True)
    inputs=['entry.S','link.ld','build_marker.py','marker.c','framebuffer.c',
            'include/marker.h','include/framebuffer.h','include/degas_display.h']
    hashes={name:hashlib.sha256((HERE/name).read_bytes()).hexdigest() for name in inputs}
    build_id=hashlib.sha256(json.dumps(hashes,sort_keys=True).encode()).digest()
    (OUT/'build-id.bin').write_bytes(build_id)
    common=['clang','--target=aarch64-none-elf','-march=armv8-a','-mgeneral-regs-only']
    run(common+['-DDIAGNOSTIC_MARKER','-DBUILD_ID_FILE="build-marker/build-id.bin"','-c','entry.S','-o','build-marker/entry.o'])
    for name in ['marker','framebuffer']:
        run(common+['-std=c11','-Wall','-Wextra','-Werror','-ffreestanding','-fno-builtin',
            '-fno-stack-protector','-fno-unwind-tables','-fno-asynchronous-unwind-tables',
            '-fno-pic','-O2','-I','include','-c',name+'.c','-o','build-marker/'+name+'.o'])
    run(['ld.lld','-T','link.ld','--build-id=none','-o','build-marker/entry.elf',
        'build-marker/entry.o','build-marker/marker.o','build-marker/framebuffer.o'])
    run(['llvm-objcopy','-O','binary','build-marker/entry.elf','build-marker/entry.bin'])
    data=(OUT/'entry.bin').read_bytes()
    assert data[56:60]==b'ARM\x64' and struct.unpack_from('<Q',data,16)[0]==len(data)
    sink=io.BytesIO()
    with gzip.GzipFile(fileobj=sink,filename='',mode='wb',mtime=0,compresslevel=9) as gz: gz.write(data)
    compressed=sink.getvalue()
    assert gzip.decompress(compressed)==data
    (OUT/'entry.bin.gz').write_bytes(compressed)
    report=dict(build_id=build_id.hex(),inputs=hashes,image_bytes=len(data),image_sha256=hashlib.sha256(data).hexdigest(),
        gzip_bytes=len(compressed),gzip_sha256=hashlib.sha256(compressed).hexdigest(),
        framebuffer_linked_into_entry=True,phone_tested=False,
        status='EXPERIMENTAL: bounded framebuffer stores; panel refresh and entry ownership not runtime-verified',
        entry_requirements='EL1/EL2, little endian, MMU and D-cache off; unsupported state parks before writes',
        compiler=subprocess.check_output(['clang','--version'],text=True).splitlines()[0])
    (OUT/'manifest.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
if __name__=='__main__': main()
