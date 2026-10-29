"""Bind the preserved watchdog PC/LR/SP to the exact first-test payload."""
import hashlib
import json
import re
import struct
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent/'vendor/python'))
from elftools.elf.elffile import ELFFile

root=Path(__file__).resolve().parents[1]
dump=(root/'evidence/raw/first-test-recovery/expdb.img').read_bytes()
assert hashlib.sha256(dump).hexdigest()=='9283837a9c7b05adcfa5405ff748426fe688c9323994b63f2da2308309b49c3f'
payload=(root/'diagnostic/build-marker/entry.bin').read_bytes()
validation=json.loads((root/'artifacts/first-test/validation.json').read_text())
assert hashlib.sha256(payload).hexdigest()==validation['kernel_image_sha256']
with (root/'diagnostic/build-marker/entry.elf').open('rb') as stream:
    elf=ELFFile(stream); table=elf.get_section_by_name('.symtab')
    symbols={name:table.get_symbol_by_name(name)[0]['st_value'] for name in ['diagnostic_park','diagnostic_marker','__stack_end','diagnostic_unsupported']}
pattern=rb'cpu\(0\) pc:<([0-9a-f]+)> lr:<([0-9a-f]+)> sp:<([0-9a-f]+)> pstate=([0-9a-f]+)'
matches=[]
for m in re.finditer(pattern,dump):
    pc,lr,sp,pstate=[int(x,16) for x in m.groups()]
    base=pc-symbols['diagnostic_park']-4
    if sp!=base+symbols['__stack_end'] or lr!=base+symbols['diagnostic_park']-4: continue
    park=symbols['diagnostic_park']
    assert payload[park:park+8]==bytes.fromhex('5f2003d5ffffff17')
    call=struct.unpack_from('<I',payload,park-8)[0]
    assert call>>26==0b100101
    imm=call&0x3ffffff
    if imm&(1<<25):imm-=1<<26
    assert park-8+4*imm==symbols['diagnostic_marker']
    assert payload[park-4:park]==bytes.fromhex('9f3f03d5')
    excerpt=dump[max(0,m.start()-800):m.start()+1100].decode('ascii',errors='replace')
    out=root/'evidence/extracted/first-test-expdb';out.mkdir(exist_ok=True)
    (out/'probe-watchdog.txt').write_text(excerpt)
    sctlr=re.search(r'sctlr_el1:([0-9a-f]+)',excerpt)
    hcr=re.search(r'hcr_el2:([0-9a-f]+)',excerpt)
    matches.append(dict(expdb_offset=hex(m.start()),pc=hex(pc),lr=hex(lr),sp=hex(sp),pstate=hex(pstate),
        inferred_load_base=hex(base),sctlr_el1=hex(int(sctlr[1],16)),hcr_el2=hex(int(hcr[1],16)),
        pc_matches_post_marker_park=True,lr_matches_marker_return=True,sp_matches_internal_stack_end=True,
        el1h=(pstate&15)==5,stage1_mmu_off=(int(sctlr[1],16)&1)==0,
        dcache_off=(int(sctlr[1],16)&4)==0,little_endian=(int(sctlr[1],16)&(1<<25))==0,
        stage2_enabled_in_hcr=(int(hcr[1],16)&1)!=0))
assert len(matches)==1,'Expected unique triple matching this probe'
report=dict(conclusion='Strong runtime evidence: probe reached post-marker wait loop; no visible marker reported',
    payload_sha256=hashlib.sha256(payload).hexdigest(),boot_image_sha256=validation['boot_image_sha256'],
    matches=matches,limits=['No embedded build ID recovered from dump; attribution uses exact PC/LR/SP and the recorded test.',
        'Marker function returned; actual scanout/DSI refresh and stage-2 framebuffer mapping remain unproven.'])
(root/'evidence/first-test-watchdog-analysis.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
