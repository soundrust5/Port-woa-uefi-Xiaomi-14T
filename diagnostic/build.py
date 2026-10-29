"""Build a RAM-only diagnostic image using Clang/LLD (run under WSL/Linux)."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

HERE = Path(__file__).resolve().parent
OUT = HERE / 'build'


def run(args):
    subprocess.run(args, cwd=HERE, check=True)


def main():
    OUT.mkdir(exist_ok=True)
    inputs = ['entry.S', 'link.ld', 'build.py']
    source_hashes = {name: hashlib.sha256((HERE / name).read_bytes()).hexdigest() for name in inputs}
    build_id = hashlib.sha256(json.dumps(source_hashes, sort_keys=True).encode()).digest()
    (OUT / 'build-id.bin').write_bytes(build_id)
    run(['clang', '--target=aarch64-none-elf', '-march=armv8-a', '-c', 'entry.S', '-o', 'build/entry.o'])
    run(['ld.lld', '-T', 'link.ld', '--build-id=none', '-o', 'build/entry.elf', 'build/entry.o'])
    run(['llvm-objcopy', '-O', 'binary', 'build/entry.elf', 'build/entry.bin'])
    run(['clang', '--target=aarch64-none-elf', '-std=c11', '-Wall', '-Wextra', '-Werror',
         '-ffreestanding', '-fno-builtin', '-fno-stack-protector', '-O2', '-I', 'include',
         '-include', 'include/entry_record.h', '-c', 'framebuffer.c', '-o', 'build/framebuffer.o'])
    data = (OUT / 'entry.bin').read_bytes()
    assert data[56:60] == b'ARM\x64'
    assert struct.unpack_from('<Q', data, 16)[0] == len(data)
    report = {
        'build_id': build_id.hex(), 'inputs': source_hashes,
        'image_bytes': len(data), 'image_sha256': hashlib.sha256(data).hexdigest(),
        'separate_framebuffer_object_sha256': hashlib.sha256((OUT / 'framebuffer.o').read_bytes()).hexdigest(),
        'framebuffer_linked_into_entry': False,
        'compiler': subprocess.check_output(['clang', '--version'], text=True).splitlines()[0],
        'linker': subprocess.check_output(['ld.lld', '--version'], text=True).strip(),
        'status': 'HOST_TEST_ONLY: RAM capture; no framebuffer/MMIO, boot.img packaging or device approval',
        'entry_requirements': 'EL1/EL2; little endian; MMU and D-cache off; whole image loaded into owned writable RAM; system-register access permitted',
        'observation_limit': 'Record is inside the image. No device retrieval channel or display update is implemented.',
    }
    (OUT / 'manifest.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
