"""Read active GZ image header/payload and compare the saved exact firmware."""
from pathlib import Path
import hashlib,json,struct,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from degas_full_windows_stage_source import shell
assert shell('getprop ro.product.device').strip()==b'degas'
slot=shell('getprop ro.boot.slot_suffix').strip().decode()
assert slot in ('_a','_b')
source=ROOT/'evidence/raw/2026-10-24-degas-return/gz_b.img'
saved=source.read_bytes()
assert hashlib.sha256(saved).hexdigest()=='011c07ed744d69a7b2760a3fb2bdd727327bec08bc844dd4177086e4ce04ee0e'
partition='/dev/block/by-name/gz'+slot
header=shell('dd if='+partition+' bs=512 count=1 2>/dev/null')
assert len(header)==512
magic,size=struct.unpack_from('<II',header)
assert magic==0x58881688 and size==0x1b77c0
count=(size+511)//512
payload=shell('dd if='+partition+' bs=512 skip=1 count='+str(count)+' 2>/dev/null')
assert len(payload)==count*512
payload=payload[:size]
out=ROOT/'evidence/raw/2026-10-29-degas-active-gz-identity'
out.mkdir(exist_ok=False)
(out/'header.bin').write_bytes(header);(out/'gz-payload.bin').write_bytes(payload)
sha=lambda b:hashlib.sha256(b).hexdigest()
report=dict(partition=partition,slot=slot,payload_bytes=size,payload_sha256=sha(payload),
            cached_payload_sha256=sha(saved[512:512+size]),header_matches=header==saved[:512],
            payload_matches=payload==saved[512:512+size],source_image_sha256=sha(saved),readonly=True)
(out/'manifest.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
