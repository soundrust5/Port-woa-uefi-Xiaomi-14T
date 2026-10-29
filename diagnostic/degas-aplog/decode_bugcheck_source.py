"""Resolve BCG2 diagnostic file pointers only after same-attempt NT identity."""
from pathlib import Path
import argparse,hashlib,json,struct
from decode_runtime_identity import classify,KERNEL,EXPECTED_SHA

def resolve_file(pointer,base):
    image=KERNEL.read_bytes()
    assert hashlib.sha256(image).hexdigest()==EXPECTED_SHA
    rva=pointer-base
    if rva<0:return None
    pe=struct.unpack_from('<I',image,0x3c)[0]
    count=struct.unpack_from('<H',image,pe+6)[0]
    size=struct.unpack_from('<H',image,pe+20)[0]
    for i in range(count):
        name,vs,va,rs,raw=struct.unpack_from('<8sIIII',image,pe+24+size+i*40)
        if va<=rva<va+rs:
            data=image[raw+rva-va:min(raw+rs,raw+rva-va+512)]
            end=data.find(b'\0')
            if end<0:return None
            data=data[:end]
            if not data or any(c<32 or c>126 for c in data):return None
            return data.decode('ascii')
    return None

def interpret(record):
    ap1,ap2=record['records'][:2]
    if not ap1.get('valid') or not ap2.get('valid'):raise ValueError('Need CRC-valid AP1 and AP2 records')
    identity=classify(ap1['snapshot'])
    if not identity.get('runtime_build_match'):raise ValueError('Runtime NT build not identified')
    if ap2.get('record_kind')!='bugcheck_monitor_v2':raise ValueError('Need BCG2 details record')
    monitor=ap2['bugcheck_monitor'];context=ap2['context']
    base=int(context['nt_base'],16)
    if base!=int(identity['candidate_nt_base'],16):raise ValueError('AP1/AP2 NT bases differ')
    pointer=int(context['last_problem_file'],16)
    report=dict(attempt=record['attempt'],runtime_identity=identity,monitor=monitor,
                source_file=resolve_file(pointer,base),source_file_rva=hex(pointer-base) if pointer else None,
                details_complete=monitor['status']==0)
    # Report the recorded source and status. Do not synthesize a unique bugcheck
    # callsite merely from the generic 203/4/controller/D tuple.
    return report

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('decoded',type=Path);ap.add_argument('--output',type=Path)
    args=ap.parse_args();result=interpret(json.loads(args.decoded.read_text()))
    text=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
