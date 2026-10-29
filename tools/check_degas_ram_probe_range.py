"""Fail closed if the diagnostic range intersects captured owners or DT allocator ranges."""
import hashlib
import json
from pathlib import Path
import re
from prepare_degas_windows_facts import ROOT, pairs, ranges


def main():
    header=ROOT/'mu/Platforms/Xiaomi/degasPkg/Include/RamProbeRange.h'
    text=header.read_text()
    values={k:int(v,16) for k,v in re.findall(r'#define DEGAS_RAM_PROBE_(BASE|SIZE)\s+(0x[0-9a-fA-F]+)',text)}
    start,end=values['BASE'],values['BASE']+values['SIZE']
    capture=ROOT/'evidence/raw/2026-10-25-degas-windows-preflight/live-fdt.json'
    comparison=ROOT/'artifacts/degas-windows-preflight/current-capture-comparison.json'
    report=json.loads(comparison.read_text())
    if not any(a<=start<end<=b for a,b in pairs(report['additional_candidates_after_current_exclusions'])):
        raise ValueError('probe is not contained in a captured candidate interval')
    dt=json.loads(capture.read_text())
    dynamic=[]
    for path,node in dt['nodes'].items():
        if not path.startswith('/reserved-memory/') or 'size' not in node or 'reg' in node:continue
        if 'alloc-ranges' not in node:raise ValueError('unbounded dynamic reservation '+path)
        for a,b in ranges({'reg':node['alloc-ranges']},2,2):
            if a<end and start<b:raise ValueError('probe intersects allocator range '+path)
            dynamic.append(dict(path=path,start=hex(a),end_exclusive=hex(b)))
    result=dict(status='bounded diagnostic candidate, NOT conventional RAM',start=hex(start),end_exclusive=hex(end),
                outside_dynamic_allocator_ranges=dynamic,
                sources=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in [header,capture,comparison]])
    out=ROOT/'artifacts/degas-ram-access-v1';out.mkdir(exist_ok=True)
    (out/'range-validation.json').write_text(json.dumps(result,indent=2)+'\n')
    print('PASS: 0x%x..0x%x excluded from all captured reservations AND all dynamic allocator ranges'%(start,end))


if __name__=='__main__':main()
