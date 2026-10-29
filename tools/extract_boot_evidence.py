"""Unpack local boot evidence with pinned AOSP unpack_bootimg; extract DT tables.

Requires the already-downloaded tools/vendor/unpack_bootimg.py and WSL dtc.
No ADB access, no modification of input images. Output must be a new directory.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
from inspect_fdt import parse


def sha(path):
    with path.open('rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--stock', type=Path, required=True)
    ap.add_argument('--live', type=Path, required=True)
    ap.add_argument('--output', type=Path, required=True)
    args = ap.parse_args()
    if args.output.exists():
        raise SystemExit('Output already exists')
    args.output.mkdir(parents=True)
    tool = Path(__file__).parent/'vendor/unpack_bootimg.py'
    provenance = json.loads((tool.parent/'provenance.json').read_text())
    if sha(tool) != provenance['sha256']:
        raise SystemExit('AOSP tool hash mismatch')
    records = []

    def unpack(label, src):
        dest = args.output/label
        dest.mkdir()
        # AOSP creates relative convenience symlinks for ramdisk names. Windows
        # may deny symlink creation; materialize identical alias files instead.
        wrapper = (
            'import os,runpy,shutil,sys; '
            'os.symlink=lambda source,dest: shutil.copyfile(os.path.join(os.path.dirname(dest),source),dest); '
            'script=sys.argv.pop(1); runpy.run_path(script,run_name="__main__")'
        )
        result = subprocess.run([sys.executable, '-c', wrapper, str(tool), '--boot_img', str(src), '--out', str(dest)], capture_output=True)
        (dest/'header.txt').write_bytes(result.stdout)
        (dest/'unpack.stderr.txt').write_bytes(result.stderr)
        if result.returncode:
            raise RuntimeError(f'AOSP unpack failed: {src}')
        # Independent section-bound checks for the v4 inputs used in this survey.
        with src.open('rb') as f:
            head = f.read(4096)
        align = lambda n,p: (n+p-1)//p*p
        if head[:8] == b'ANDROID!':
            if struct.unpack_from('<I',head,40)[0] != 4:
                raise ValueError('Expected boot v4')
            kernel, ramdisk = struct.unpack_from('<II',head,8)
            expected = {'kernel': (4096,kernel), 'ramdisk': (4096+align(kernel,4096),ramdisk)}
        elif head[:8] == b'VNDRBOOT':
            version,page = struct.unpack_from('<II',head,8)
            if version != 4 or page == 0 or page & (page-1):
                raise ValueError('Unexpected vendor header')
            ramdisk = struct.unpack_from('<I',head,24)[0]
            header_size, dtb_size = struct.unpack_from('<II',head,2096)
            expected = {'dtb':(align(header_size,page)+align(ramdisk,page),dtb_size)}
        else:
            raise ValueError('Unexpected magic')
        with src.open('rb') as f:
            for name,(offset,length) in expected.items():
                if offset+length > src.stat().st_size:
                    raise ValueError('Section out of image bounds')
                target=dest/name
                if length == 0 and not target.exists():
                    continue
                f.seek(offset)
                if target.read_bytes() != f.read(length):
                    raise ValueError('AOSP extraction does not match declared section')
        records.append({'label':label,'source':str(src.resolve()),'sha256':sha(src),'sections_checked':list(expected)})
        print(label+': unpacked and section bounds checked',flush=True)

    for label, directory, suffix in [('stock',args.stock,''),('live-a',args.live,'_a'),('live-b',args.live,'_b')]:
        for base in ('boot','init_boot','vendor_boot'):
            unpack(label+'-'+base, directory/(base+suffix+'.img'))
        src=directory/('dtbo'+suffix+'.img')
        data=src.read_bytes()
        magic,total,header_size,entry_size,count,entries_offset,page,version=struct.unpack_from('>8I',data)
        if magic != 0xd7b7ab1e or not 32 <= header_size <= total <= len(data) or entry_size < 32 or entries_offset < header_size or entries_offset+entry_size*count > total:
            raise ValueError('Invalid DTBO table')
        dest=args.output/(label+'-dtbo'); dest.mkdir()
        entries=[]
        for i in range(count):
            size,offset,ident,rev,*custom=struct.unpack_from('>8I',data,entries_offset+i*entry_size)
            if offset < entries_offset+entry_size*count or offset+size > total:
                raise ValueError('DTBO entry outside payload')
            blob=data[offset:offset+size]
            # Do not silently interpret compressed v1 payloads as an FDT.
            parse(blob)
            (dest/f'{i:03d}.dtb').write_bytes(blob)
            entries.append({'index':i,'size':size,'offset':offset,'id':ident,'rev':rev,'custom':custom,'sha256':hashlib.sha256(blob).hexdigest()})
        (dest/'table.json').write_text(json.dumps({'version':version,'page_size':page,'entries':entries},indent=2))
        records.append({'label':label+'-dtbo','source':str(src.resolve()),'sha256':sha(src),'entries':count})
        print(f'{label}-dtbo: {count} verified FDT entries',flush=True)
    for dtb in list(args.output.glob('*vendor_boot/dtb')):
        data=dtb.read_bytes(); pos=0; index=0
        if data[:4] == bytes.fromhex('d7b7ab1e'):
            magic,total,hs,es,count,eo,page,version=struct.unpack_from('>8I',data)
            if not 32 <= hs <= total <= len(data) or es < 32 or eo < hs or eo+es*count > total:
                raise ValueError('Invalid vendor DT table')
            table=[]
            for i in range(count):
                size,offset,ident,rev,*custom=struct.unpack_from('>8I',data,eo+i*es)
                if offset < eo+es*count or offset+size > total:
                    raise ValueError('Vendor DT entry outside payload')
                blob=data[offset:offset+size]; parse(blob)
                (dtb.parent/f'tree-{i:03d}.dtb').write_bytes(blob)
                table.append({'index':i,'offset':offset,'size':size,'id':ident,'rev':rev,'custom':custom})
            (dtb.parent/'dt-table.json').write_text(json.dumps({'version':version,'entries':table},indent=2))
            continue
        while pos < len(data):
            if data[pos:pos+4] != b'\xd0\x0d\xfe\xed':
                if not any(data[pos:]):
                    break
                raise ValueError(f'Unexpected bytes in DTB section at {pos}')
            size=struct.unpack_from('>I',data,pos+4)[0]
            blob=data[pos:pos+size]; parse(blob)
            (dtb.parent/f'tree-{index:03d}.dtb').write_bytes(blob)
            pos+=size; index+=1
    for dtb in args.output.rglob('*.dtb'):
        absolute=dtb.resolve()
        unix='/mnt/'+absolute.drive[0].lower()+absolute.as_posix()[2:]
        result=subprocess.run(['wsl','-d','Ubuntu','--exec','dtc','-I','dtb','-O','dts','-o',unix[:-4]+'.dts',unix],capture_output=True)
        dtb.with_suffix('.dtc.log').write_bytes(result.stdout+result.stderr)
        if result.returncode:
            raise RuntimeError(f'dtc failed: {dtb}')
    files=[{'file':str(p.relative_to(args.output)),'bytes':p.stat().st_size,'sha256':sha(p)} for p in args.output.rglob('*') if p.is_file()]
    (args.output/'manifest.json').write_text(json.dumps({'aosp_tool':provenance,'windows_compatibility':'Relative ramdisk symlinks materialized as file copies; extraction code unchanged','inputs':records,'outputs':files},indent=2),encoding='utf-8')
    print(f'Complete: {len(records)} source images, {len(files)} output files')


if __name__ == '__main__':
    main()
