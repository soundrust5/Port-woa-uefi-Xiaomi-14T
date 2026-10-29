"""Read back the one Windows-written block and neighbors from Android. No writes."""
import hashlib,json,datetime
from pathlib import Path
from stage_degas_ufs_winpe import ROOT,verify_gpt,shell

assert shell('getprop ro.product.device').strip()==b'degas'
assert shell('getprop sys.boot_completed').strip()==b'1'
verify_gpt()
base=ROOT/'evidence/raw/2026-10-27-degas-ufs-write-baseline'
original=(base/'original-three-blocks.bin').read_bytes()
expected=(base/'expected-block.bin').read_bytes()
data=shell('dd if=/dev/block/sdc bs=4096 skip=29796607 count=3 2>/dev/null')
assert len(data)==12288
out=ROOT/'evidence/raw'/('degas-write-readback-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(exist_ok=False);(out/'readback.bin').write_bytes(data)
report=dict(expected_match=data[4096:8192]==expected,
 neighbors_unchanged=data[:4096]==original[:4096] and data[8192:]==original[8192:],
 primary_backup_gpt_unchanged=True,
 sha256=hashlib.sha256(data[4096:8192]).hexdigest())
(out/'result.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
assert report['expected_match'] and report['neighbors_unchanged']
