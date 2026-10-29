"""Test-sign the MT6897 read-only miniport. Never changes host trust."""
from pathlib import Path
import hashlib,json,shutil,struct,subprocess,sys
from package_op12_touch import ROOT,KEY_DIR,SDK,VERIFIER_DIR,VERIFIER_SHA,sha

assert sys.argv[1:] in ([],['--performance'])
performance=sys.argv[1:]==['--performance']
BUILD=ROOT/('artifacts/degas/ufs-windows/performance-candidate' if performance else 'artifacts/degas/ufs-windows/install-candidate')
OUT=BUILD/'test-package'
OUT.mkdir(exist_ok=True)
inf=ROOT/'diagnostic/degas/ufs/windows/degasufs.inf'
if performance:
 candidate_inf=BUILD/'degasufs.inf'
 candidate_inf.write_text(inf.read_text().replace('0.1.0.0','0.2.0.0'))
 inf=candidate_inf
verifier=VERIFIER_DIR/'infverif.exe'
assert sha(verifier)==VERIFIER_SHA
r=subprocess.run([str(verifier),'/w','/v',str(inf)],capture_output=True)
(BUILD/'infverif.log').write_bytes(r.stdout+r.stderr)
assert r.returncode==0,'INF failed verification; inspect infverif.log'
binary=BUILD/'degasufs.sys'
b=binary.read_bytes();pe=struct.unpack_from('<I',b,60)[0]
assert b[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',b,pe+4)[0]==0xaa64
assert struct.unpack_from('<H',b,pe+24+68)[0]==1
shutil.copyfile(binary,OUT/binary.name)
(OUT/inf.name).write_bytes(inf.read_text().replace('\r\n','\n').replace('\n','\r\n').encode('ascii'))
shutil.copyfile(KEY_DIR/'op12ufs.cer',OUT/'development.cer')
logs=[]
def run(args):
    r=subprocess.run(args,cwd=OUT,capture_output=True)
    logs.append((r.stdout+r.stderr).decode(errors='replace'))
    (BUILD/'signing.log').write_text('\n'.join(logs))
    assert r.returncode==0,'signing command failed'
run([str(SDK/'signtool.exe'),'sign','/fd','SHA256','/f',str(KEY_DIR/'local-test-only.pfx'),'degasufs.sys'])
cdf=BUILD/'degasufs.cdf'
cdf.write_text('[CatalogHeader]\nName=degasufs.cat\nPublicVersion=0x0000001\nEncodingType=0x00010001\nCATATTR1=0x10010001:OSAttr:2:10.0\n[CatalogFiles]\n<HASH>degasufs.inf=degasufs.inf\n<HASH>degasufs.sys=degasufs.sys\n')
run([str(SDK/'makecat.exe'),'/v',str(cdf)])
run([str(SDK/'signtool.exe'),'sign','/fd','SHA256','/f',str(KEY_DIR/'local-test-only.pfx'),'degasufs.cat'])
assert not list(OUT.glob('*.pfx'))
manifest=dict(target='ACPI\\DGS0001',service='degasufs',microsoft_signed=False,
    performance_candidate=performance,maximum_transfer_bytes=0x40000 if performance else 0xe000,
    test_signing_required=True,host_trust_modified=False,hardware_tested=False,
    read_only=False,install_mode=True,write_lun=2,write_ranges=[[29267968,29792256],[29796352,64573432]],automatic_watchdog=False,requires_live_uefi_link=True,cold_start_and_resume=False,
    files={p.name:sha(p) for p in sorted(OUT.iterdir()) if p.is_file()},
    sources={str(p.relative_to(ROOT)):sha(p) for p in [inf,ROOT/'diagnostic/degas/ufs/windows/degasufs.c',ROOT/'diagnostic/degas/ufs/windows/retained_log.h',ROOT/'tools/build_degas_ufs.sh',ROOT/'diagnostic/degas/ufs/install_policy.h']})
(BUILD/'package.json').write_text(json.dumps(manifest,indent=2)+'\n')
print(json.dumps(manifest,indent=2))
