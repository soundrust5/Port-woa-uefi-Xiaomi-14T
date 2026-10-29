"""Independent Android readback of the entire Windows stress extent; no writes."""
import datetime,hashlib,json,struct
from stage_degas_ufs_winpe import ROOT,verify_gpt,shell

assert shell('getprop ro.product.device').strip()==b'degas'
assert shell('getprop sys.boot_completed').strip()==b'1'
verify_gpt()
base=ROOT/'evidence/raw/2026-10-27-degas-ufs-stress-baseline'
original=(base/'original-with-guards.bin').read_bytes()
data=shell('dd if=/dev/block/sdc bs=4096 skip=29800447 count=65538 2>/dev/null')
assert len(data)==len(original)==65538*4096
out=ROOT/'evidence/raw'/('degas-stress-readback-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
out.mkdir(exist_ok=False);(out/'readback-with-guards.bin').write_bytes(data)
pattern=bytearray(((i*73+41)^(i>>4))&255 for i in range(4096))
expected=hashlib.sha256();mismatches=[]
for block in range(65536):
 lba=29800448+block
 struct.pack_into('<II',pattern,0,lba,(~lba)&0xffffffff)
 expected.update(pattern)
 if data[(block+1)*4096:(block+2)*4096]!=pattern:mismatches.append(lba)
result=dict(blocks_verified=65536,mismatched_blocks=len(mismatches),
 first_mismatched_lbas=mismatches[:16],expected_sha256=expected.hexdigest(),
 actual_sha256=hashlib.sha256(memoryview(data)[4096:-4096]).hexdigest(),
 guards_unchanged=data[:4096]==original[:4096] and data[-4096:]==original[-4096:],
 primary_backup_gpt_unchanged=True)
(out/'result.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
assert not mismatches and result['guards_unchanged']
