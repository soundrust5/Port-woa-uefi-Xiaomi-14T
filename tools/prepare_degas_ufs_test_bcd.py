"""WSL: test signing on a COPY of the phone's default WinPE BCD entry only."""
from pathlib import Path
import copy,hashlib,json,hivex
from prepare_degas_ufs_hive import node,snapshot
root=Path(__file__).resolve().parents[1]
out=root/'artifacts/degas/ufs-windows/bcd'
source=out/'BCD.original';target=out/'BCD.test'
h=hivex.Hivex(str(source),write=True)
n=node(h,r'Objects\{9dea862c-5cdd-4e70-acc1-f32b344d4795}\Elements\23000003')
default=h.value_value(h.node_get_value(n,'Element'))[1].decode('utf-16le').rstrip('\0')
assert default=='{7619dcc9-fafe-11d9-b411-000476eba25f}'
path='Objects\\'+default+'\\Elements'
n=node(h,path+r'\26000022')
assert h.value_value(h.node_get_value(n,'Element'))==(3,b'\x01'),'not WinPE'
before=snapshot(h);n=node(h,path+r'\16000049',True)
h.node_set_value(n,dict(key='Element',t=3,value=b'\x01'));h.commit(str(target))
after=snapshot(hivex.Hivex(str(target)));expected=copy.deepcopy(before)
expected[path+r'\16000049']={'Element':(3,b'\x01')}
assert after==expected,'unexpected BCD changes'
report=dict(source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
 candidate_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),default_loader=default,
 only_change='testsigning=true on default WinPE entry',semantic_comparison='passed',host_bcd_modified=False)
(out/'manifest.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
