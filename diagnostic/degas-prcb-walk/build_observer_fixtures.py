"""WSL: build both observer variants from current source for emulation."""
from pathlib import Path
import argparse, hashlib, json, subprocess

root = Path(__file__).resolve().parents[2]
src = root / 'mu/Platforms/Xiaomi/degasPkg/Drivers/DegasAcpiDxe'
out = root / 'artifacts/degas/cpu-audit'
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--monitor',action='store_true')
args=parser.parse_args()
targets=[('windows-observer',0,0,'WindowsObserver.S','DegasWindowsObserverStart'),
         ('windows-identity',1,0,'WindowsObserver.S','DegasWindowsObserverStart'),
         ('parking-observer',0,0,'Parking.S','DegasParkingStart')]
if args.monitor:
    targets += [('windows-bugcheck',1,1,'WindowsObserver.S','DegasWindowsObserverStart'),
                ('parking-bugcheck',1,1,'Parking.S','DegasParkingStart')]
for stem, mode, monitor, source, entry in targets:
    obj = out / (stem + '.o')
    elf = out / (stem + '.elf')
    binary = out / (stem + '.bin')
    subprocess.run(['clang', '--target=aarch64-none-elf', '-I', str(src),
                    f'-DDEGAS_WINDOWS_IDENTITY_OBSERVER={mode}', '-c',
                    f'-DDEGAS_WINDOWS_BUGCHECK_MONITOR={monitor}',
                    str(src / source), '-o', str(obj)], check=True)
    subprocess.run(['ld.lld', '--image-base=0', '-Ttext=0', '--entry='+entry,
                    str(obj), '-o', str(elf)], check=True)
    subprocess.run(['aarch64-linux-gnu-objcopy', '-O', 'binary', str(elf), str(binary)], check=True)
    symbols = subprocess.check_output(['aarch64-linux-gnu-nm', '-n', str(elf)], text=True)
    (out / (stem + '-symbols.txt')).write_text(symbols)
    code = binary.read_bytes()
    dependencies=[src/source,src/('PrcbWalk.inc' if source=='WindowsObserver.S' else 'ApJournalCommit.inc')]
    if monitor and source=='WindowsObserver.S':dependencies.append(src/'BugcheckMonitor.inc')
    metadata = dict(sources={str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest()
                            for p in dependencies},
                    code_sha256=hashlib.sha256(code).hexdigest(), code_bytes=len(code),
                    linked_at_zero=True, identity_mode=mode, bugcheck_monitor=monitor)
    (out / (stem + '.json')).write_text(json.dumps(metadata, indent=2))
    print(stem, len(code), metadata['code_sha256'])
