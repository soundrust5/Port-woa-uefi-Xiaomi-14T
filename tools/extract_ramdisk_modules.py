"""Bounded legacy-LZ4/newc reader for offline ramdisk evidence.

Lists every archive entry and extracts only selected regular kernel modules.
Does not create symlinks or execute archive content.
"""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import lz4.block


def decompress(data):
    if data[:4] != bytes.fromhex('02214c18'):
        raise ValueError('Expected legacy LZ4 stream')
    pos=4; chunks=[]; total=0
    while pos<len(data):
        if pos+4>len(data):raise ValueError('Truncated LZ4 block header')
        size=struct.unpack_from('<I',data,pos)[0];pos+=4
        if not 0<size<=9*1024*1024 or pos+size>len(data):raise ValueError('Invalid LZ4 block bounds')
        block=lz4.block.decompress(data[pos:pos+size],uncompressed_size=8*1024*1024)
        pos+=size;total+=len(block)
        if total>512*1024*1024:raise ValueError('Ramdisk exceeds inspection limit')
        chunks.append(block)
    return b''.join(chunks)


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path)
    ap.add_argument('--output',type=Path,required=True)
    ap.add_argument('--select',default=r'mediatek[-_]drm|panel[-_]n12a|mtk_dramc|aee|bootprof|log_store|mblock|memblock|mtkfb',
                    help='Regex matching regular .ko basenames to extract; archive content is never executed')
    args=ap.parse_args()
    args.output.mkdir(parents=True,exist_ok=False)
    packed=args.input.read_bytes();data=decompress(packed);pos=0;entries=[]
    while pos<len(data):
        if not any(data[pos:pos+6]):
            if not any(data[pos:]):break
            pos+=4;continue
        if data[pos:pos+6] not in (b'070701',b'070702') or pos+110>len(data):raise ValueError(f'Expected newc at {pos}')
        fields=[int(data[pos+6+i*8:pos+14+i*8],16) for i in range(13)]
        mode,size,namesize=fields[1],fields[6],fields[11]
        name_start=pos+110;name_end=name_start+namesize
        if namesize<1 or name_end>len(data) or data[name_end-1]!=0:raise ValueError('Invalid cpio name')
        name=data[name_start:name_end-1].decode('utf-8');start=(name_end+3)&~3;end=start+size
        if end>len(data):raise ValueError('Truncated cpio content')
        pos=(end+3)&~3
        if name=='TRAILER!!!':continue
        path=PurePosixPath(name)
        if path.is_absolute() or '..' in path.parts:raise ValueError('Unsafe cpio path')
        record={'name':name,'mode':oct(mode),'size':size,'offset':start}
        if mode & 0o170000 == 0o100000:
            body=data[start:end];record['sha256']=hashlib.sha256(body).hexdigest()
            if path.suffix=='.ko' and re.search(args.select,path.name):
                target=args.output/path.name
                if target.exists():raise ValueError('Module basename collision')
                target.write_bytes(body);record['extracted']=target.name
                print(target.name,size)
        entries.append(record)
    (args.output/'inventory.json').write_text(json.dumps({'source':str(args.input.resolve()),'source_sha256':hashlib.sha256(packed).hexdigest(),'uncompressed_bytes':len(data),'entries':entries},indent=2),encoding='utf-8')
    print(f'{len(entries)} entries, decompressed {len(data)} bytes')


if __name__=='__main__':main()
