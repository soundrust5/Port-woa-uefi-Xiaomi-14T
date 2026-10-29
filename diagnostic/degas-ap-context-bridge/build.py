"""Assemble the diagnostic source; Windows delegates compilation to WSL."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys

HERE = Path(__file__).resolve().parent
if os.name == 'nt':
    script = '/mnt/' + HERE.drive[0].lower() + HERE.as_posix()[2:] + '/build.py'
    raise SystemExit(subprocess.call(['wsl.exe', '--exec', 'python3', script]))

out = HERE / 'generated'
out.mkdir(exist_ok=True)
src = HERE / 'ApContext.S'
obj, elf, binary = [out / ('ap-context.' + ext) for ext in ('o', 'elf', 'bin')]
subprocess.run(['clang', '--target=aarch64-none-elf', '-c', str(src), '-o', str(obj)], check=True)
subprocess.run(['ld.lld', '--image-base=0', '-Ttext=0', '--entry=DegasApContextStart',
                str(obj), '-o', str(elf)], check=True)
subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '--only-section=.text',
                str(elf), str(binary)], check=True)
symbols = subprocess.check_output(['aarch64-linux-gnu-nm', '-n', str(elf)], text=True)
(out / 'ap-context.symbols').write_text(symbols)
disassembly = subprocess.check_output(['aarch64-linux-gnu-objdump', '-d', str(elf)], text=True)
(out / 'ap-context.disasm').write_text(disassembly)
syms = {words[2]: int(words[0], 16) for line in symbols.splitlines()
        if len(words := line.split()) == 3}
code = binary.read_bytes()
assert syms['DegasApContextStart'] == 0 and len(code) <= 4096
meta = dict(source_sha256=hashlib.sha256(src.read_bytes()).hexdigest(),
            binary_sha256=hashlib.sha256(code).hexdigest(), binary_bytes=len(code),
            symbols=syms, linked_at_zero=True, deployable=False)
(out / 'ap-context.json').write_text(json.dumps(meta, indent=2) + '\n')
print('Host-only AP bridge:', len(code), 'assembled bytes;', meta['binary_sha256'])
