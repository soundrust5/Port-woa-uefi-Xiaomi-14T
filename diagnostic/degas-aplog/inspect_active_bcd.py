"""WSL: inspect a captured BCD hive without changing it."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from patch_bcd_system_marker import semantic_snapshot
folder=ROOT/'evidence/raw/2026-10-29-degas-active-boot-identity'
path=folder/'BCD'
before=hashlib.sha256(path.read_bytes()).hexdigest()
s=semantic_snapshot(path)
def elements(guid):
    prefix='System\\Objects\\'+guid+'\\Elements\\'
    return {p[len(prefix):]:values['Element'][1] for p,values in s.items() if p.startswith(prefix) and 'Element' in values}
def string(b):return b.decode('utf-16le').rstrip('\0')
manager=elements('{9dea862c-5cdd-4e70-acc1-f32b344d4795}')
default=string(manager['23000003']);loader=elements(default)
report=dict(sha256=before,readonly=True,default_loader=default,
            bootsequence_present='24000002' in manager,
            loader_description=string(loader['12000004']),loader_path=string(loader['12000002']),
            winpe=loader.get('26000022',b'\0')!=b'\0',
            kernel_override=string(loader['22000011']) if '22000011' in loader else None,
            hal_override=string(loader['22000012']) if '22000012' in loader else None,
            loader_element_bytes={k:v.hex() for k,v in loader.items()})
assert hashlib.sha256(path.read_bytes()).hexdigest()==before
(folder/'bcd-inspection.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
