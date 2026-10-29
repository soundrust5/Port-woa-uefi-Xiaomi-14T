"""Execute the actual Mu MainEntryPoint tail with test PCDs and a SecEntry stub."""
from pathlib import Path
import json
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_SP
from elftools.elf.elffile import ELFFile


def main():
    source = ROOT / 'mu/Silicon/Silicium/SiliciumPkg/Sec/AArch64/ModuleEntryPoint.S'
    body = source.read_text().split('ASM_PFX (MainEntryPoint):', 1)[1]
    body = body.replace('MOV64 (x3, FixedPcdGet64 (PcdCPUCoresStackBase))', 'ldr x3, =0x200040')
    body = body.replace('MOV32 (x4, FixedPcdGet32 (PcdCPUCorePrimaryStackSize))', 'mov x4, #0x400')
    body = body.replace('ASM_PFX (SecEntry)', 'SecEntry')
    assert not re.search(r'ASM_PFX|MOV64|MOV32|Pcd', body)
    output = ROOT / 'diagnostic/build'
    output.mkdir(exist_ok=True)
    assembly = '.text\n.global _start, SecEntry\n_start:\n' + body + '\nSecEntry:\n wfe\n'
    (output / 'mu-stack.S').write_text(assembly)
    linux_root = '/mnt/' + ROOT.drive[0].lower() + ROOT.as_posix()[2:]
    import shlex
    command = ('cd ' + shlex.quote(linux_root) +
               ' && clang --target=aarch64-none-elf -c diagnostic/build/mu-stack.S -o diagnostic/build/mu-stack.o'
               ' && ld.lld --image-base=0 -Ttext=0x100000 -e _start diagnostic/build/mu-stack.o -o diagnostic/build/mu-stack.elf'
               ' && llvm-objcopy -O binary diagnostic/build/mu-stack.elf diagnostic/build/mu-stack.bin')
    subprocess.run(['wsl', '--', 'bash', '-c', command], check=True)
    with (output / 'mu-stack.elf').open('rb') as f:
        elf = ELFFile(f)
        stop = elf.get_section_by_name('.symtab').get_symbol_by_name('SecEntry')[0]['st_value']
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    uc.mem_map(0x100000, 0x10000)
    uc.mem_write(0x100000, (output / 'mu-stack.bin').read_bytes())
    uc.mem_map(0x200000, 0x1000)
    uc.mem_write(0x200000, b'\xa5' * 0x1000)
    reached = []

    def hook(engine, address, size, data):
        if address == stop:
            reached.append(address)
            engine.emu_stop()

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.emu_start(0x100000, 0, count=5000)
    assert reached, 'SecEntry not reached'
    assert uc.reg_read(UC_ARM64_REG_X0) == 0x200040, 'StackBase must not become StackEnd'
    assert uc.reg_read(UC_ARM64_REG_X1) == 0x400
    assert uc.reg_read(UC_ARM64_REG_SP) == 0x200440
    assert bytes(uc.mem_read(0x200000, 0x1000)) == b'\xa5' * 64 + bytes(0x400) + b'\xa5' * (0x1000 - 64 - 0x400)
    report = {'passed': True, 'scope': 'Actual MainEntryPoint tail; test PCDs; SecEntry stub; no whole-Mu or hardware execution'}
    (output / 'mu-stack-test.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Mu stack-base regression test passed')


if __name__ == '__main__':
    main()
