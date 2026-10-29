"""Build host-only AArch64 cache probe; Windows uses the existing WSL tools."""
from pathlib import Path
import hashlib
import json
import os
import subprocess

HERE = Path(__file__).resolve().parent
if os.name == 'nt':
    path = '/mnt/' + HERE.drive[0].lower() + HERE.as_posix()[2:] + '/build.py'
    raise SystemExit(subprocess.call(['wsl.exe', '--exec', 'python3', path]))
out = HERE / 'generated'
out.mkdir(exist_ok=True)
source = HERE / 'CacheProbe.S'
obj, elf, binary = [out / ('cache-probe.' + ext) for ext in ('o', 'elf', 'bin')]
subprocess.run(['clang', '--target=aarch64-none-elf', '-c', str(source), '-o', str(obj)], check=True)
subprocess.run(['ld.lld', '--image-base=0', '-Ttext=0', '--entry=DegasCacheApStart',
                str(obj), '-o', str(elf)], check=True)
subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', '--only-section=.text',
                str(elf), str(binary)], check=True)
symbols = subprocess.check_output(['aarch64-linux-gnu-nm', '-n', str(elf)], text=True)
(out / 'cache-probe.symbols').write_text(symbols)
(out / 'cache-probe.disasm').write_text(subprocess.check_output(
    ['aarch64-linux-gnu-objdump', '-d', str(elf)], text=True))
syms = {parts[2]: int(parts[0], 16) for line in symbols.splitlines()
        if len(parts := line.split()) == 3}
code = binary.read_bytes()
assert syms['DegasCacheApStart'] == 0 and len(code) <= 4096
metadata = dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                code_sha256=hashlib.sha256(code).hexdigest(), code_bytes=len(code),
                symbols=syms, scope='Host instruction fixture, not deployed firmware')
(out / 'cache-probe.json').write_text(json.dumps(metadata, indent=2) + '\n')
print(len(code), 'assembled bytes;', metadata['code_sha256'])
