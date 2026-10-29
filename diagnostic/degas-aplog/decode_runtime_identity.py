"""Interpret a decoded AP journal from the isolated runtime-identity image."""
from pathlib import Path
import argparse, hashlib, json, struct, uuid

ROOT=Path(__file__).resolve().parents[2]
KERNEL=ROOT/'artifacts/degas/cpu-audit/ntoskrnl.exe'
EXPECTED_SHA='5bb49f8383e38b26ef0c18f3087db3be541e99143b80b92d2f83d824b7e5301c'

def classify(snapshot):
    image=KERNEL.read_bytes()
    assert hashlib.sha256(image).hexdigest()==EXPECTED_SHA
    pe=struct.unpack_from('<I',image,0x3c)[0]
    assert pe==0x110
    opt=pe+24
    n=struct.unpack_from('<H',image,pe+6)[0]
    size=struct.unpack_from('<H',image,pe+20)[0]
    sections=[struct.unpack_from('<8sIIII',image,opt+size+i*40) for i in range(n)]
    def at(rva,span):
        for name,vs,va,rs,raw in sections:
            if va<=rva and rva+span<=va+rs:return image[raw+rva-va:raw+rva-va+span]
        raise ValueError('RVA is not backed by a section')
    expected_rsds=at(0x4dc78,24)
    assert expected_rsds[:4]==b'RSDS'
    status=int(snapshot['mailbox_e00'],16)
    if status:
        return dict(valid_capture=False,reader_error=status&0xffffffff,failed_read=status>>32,
                    runtime_build_match=None)
    pc=int(snapshot['mailbox_e08'],16)
    got_rsds=struct.pack('<3Q',*[int(snapshot[f'mailbox_{x:03x}'],16) for x in (0xe28,0xe30,0xe38)])
    checks=dict(entry_word=struct.pack('<Q',int(snapshot['mailbox_e10'],16))==at(0xa91ba0,8),
                dos_header=struct.pack('<Q',int(snapshot['mailbox_e18'],16))==image[:8],
                pe_header=struct.pack('<Q',int(snapshot['mailbox_e20'],16))==image[pe:pe+8],
                rsds_guid_and_age=got_rsds==expected_rsds)
    return dict(valid_capture=True,runtime_build_match=all(checks.values()),checks=checks,
                saved_entry_pc=hex(pc),candidate_nt_base=hex(pc-0xa91ba0),
                codeview_signature=got_rsds[:4].hex(),
                codeview_guid=str(uuid.UUID(bytes_le=got_rsds[4:20])),
                codeview_age=struct.unpack_from('<I',got_rsds,20)[0],
                reference_file_sha256=EXPECTED_SHA,
                scope='Entry instruction + DOS/PE signatures + complete CodeView identity; not a runtime full-image hash')

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('decoded',type=Path)
    ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    record=json.loads(args.decoded.read_text())
    cpu=record['records'][0]
    if cpu.get('cpu')!=1 or not cpu.get('valid'):raise SystemExit('AP1 has no validated committed journal')
    result=dict(attempt=record['attempt'],source_manifest_sha256=record['source_manifest_sha256'],**classify(cpu['snapshot']))
    text=json.dumps(result,indent=2)+'\n'
    if args.output:args.output.write_text(text)
    print(text,end='')

if __name__=='__main__':main()
