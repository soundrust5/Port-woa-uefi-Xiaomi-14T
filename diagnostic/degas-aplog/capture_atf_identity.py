"""Read active TEE's pinned ATF segment; no flash or live MMIO access."""
from pathlib import Path
import hashlib,json,sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools'))
from degas_full_windows_stage_source import shell
assert shell('getprop ro.product.device').strip()==b'degas'
slot=shell('getprop ro.boot.slot_suffix').strip().decode()
assert slot in ('_a','_b')
out=ROOT/'evidence/raw/2026-10-29-degas-active-atf-identity'
out.mkdir(exist_ok=False)
partition='/dev/block/by-name/tee'+slot
header=shell('dd if='+partition+' bs=512 count=1 2>/dev/null')
assert len(header)==512
segment=shell('dd if='+partition+' bs=512 skip=1 count=1880 2>/dev/null')
assert len(segment)==962560
segment=segment[:962168]
old=(ROOT/'evidence/extracted/2026-09-14/atf-analysis/atf.bin').read_bytes()
sha=lambda b:hashlib.sha256(b).hexdigest()
(out/'header.bin').write_bytes(header)
(out/'atf.bin').write_bytes(segment)
report=dict(partition=partition,slot=slot,bytes=len(segment),sha256=sha(segment),
            cached_sha256=sha(old),matches_cached=segment==old,readonly=True)
(out/'manifest.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report,indent=2))
