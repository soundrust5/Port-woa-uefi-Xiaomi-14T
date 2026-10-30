"""Offline checks for the opt-in stock Android EFI-stub image; no phone I/O."""
from pathlib import Path
import argparse, ast, gzip, hashlib, json, re, struct, zlib

ROOT = Path(__file__).resolve().parents[1]

def number(expression, macros):
    expression = re.sub(r'(?<=\w)(?:ULL|UL|U)\b', '', expression.strip())
    node = ast.parse(expression, mode='eval').body
    def value(n):
        if isinstance(n, ast.Constant) and isinstance(n.value, int): return n.value
        if isinstance(n, ast.Name): return macros[n.id]
        if isinstance(n, ast.BinOp):
            a, b = value(n.left), value(n.right)
            if isinstance(n.op, ast.Add): return a + b
            if isinstance(n.op, ast.Sub): return a - b
        raise ValueError('Unsupported memory-map expression: ' + expression)
    return value(node)

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('profile', type=Path)
    args = ap.parse_args(); out = args.profile
    manifest = json.loads((out/'source-manifest.json').read_text())
    assert manifest['android_efi_boot']
    for path, digest in manifest['sources'].items():
        assert hashlib.sha256((ROOT/path).read_bytes()).hexdigest() == digest, path
    stock = (ROOT/'artifacts/degas/android-stock-Image').read_bytes()
    assert hashlib.sha256(stock).hexdigest() == 'dbb1b31dfc4cc1beee2bd4600591cf5420d39969b5ab142b9eeb3ef2e442668a'
    header = (ROOT/'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/AndroidEfiBoot.h').read_text()
    macros = {k:int(v,16) for k,v in re.findall(r'^#define\s+(ANDROID_\w+)\s+(0x[0-9a-fA-F]+)', header, re.M)}
    assert len(stock) == macros['ANDROID_IMAGE_SIZE']
    assert zlib.crc32(stock) == macros['ANDROID_IMAGE_CRC']
    image = gzip.decompress((out/'kernel.bin.gz').read_bytes())
    assert len(image) == 0x4480000 and struct.unpack_from('<Q',image,16)[0] == len(image)
    start = macros['ANDROID_IMAGE_BASE']; offset = start-0x40000000
    assert image[offset:offset+len(stock)] == stock
    assert start+len(stock) < 0x44440000
    assert image[0x4440000:] == bytes(0x40000), 'GCE/UFS initial arenas changed'
    pe = struct.unpack_from('<I',stock,0x3c)[0]
    assert stock[:2] == b'MZ' and stock[pe:pe+4] == b'PE\0\0'
    assert struct.unpack_from('<H',stock,pe+4)[0] == 0xaa64

    # Evaluate the actual descriptor source with this profile's build flags.
    base = ROOT/'mu/Platforms/Xiaomi/degasPkg'
    for k,v in re.findall(r'^#define\s+(DEGAS_GCE_ARENA_\w+)\s+(0x[0-9a-fA-F]+)', (base/'Include/GceArena.h').read_text(), re.M):
        macros[k] = int(v,16)
    text = (base/'Library/MemoryMapLib/MemoryMapLib.c').read_text()
    text = text.replace('#include "../../Include/ExpandedRam.inc"', (base/'Include/ExpandedRam.inc').read_text())
    flags = {'DEGAS_ANDROID_EFI_BOOT': True, 'DEGAS_WIFI_INIT_TEST': manifest.get('wifi_init_test',False)}
    enabled = True; stack = []; regions = []
    for line in text.splitlines():
        s = line.strip()
        if s.startswith('#if '):
            condition = flags[s[4:].strip()]; stack.append((enabled,condition)); enabled = enabled and condition
        elif s == '#else': enabled = stack[-1][0] and not stack[-1][1]
        elif s == '#endif': enabled = stack.pop()[0]
        elif enabled and s.startswith('{"'):
            fields = s.strip('{},').split(',')
            address,length = number(fields[1],macros), number(fields[2],macros)
            regions.append(dict(name=fields[0].strip('"'),base=address,end=address+length,type=fields[6]))
    assert not stack
    for i,a in enumerate(regions):
        assert a['base'] < a['end']
        for b in regions[i+1:]:
            assert a['end'] <= b['base'] or b['end'] <= a['base'], (a,b)
    kernel = next(r for r in regions if r['name']=='Android stock Image')
    assert kernel['type'] == 'BsData' and kernel['base']==start and kernel['end']==0x44440000
    fdt = json.loads((ROOT/'evidence/raw/2026-10-30-degas-ram-max/live-fdt.json').read_text())['nodes']['/chosen']
    rdstart = int(fdt['linux,initrd-start']['hex'],16); rdend = int(fdt['linux,initrd-end']['hex'],16)
    rd = next(r for r in regions if r['name']=='LK Android initrd')
    assert rd['base'] <= rdstart < rdend <= rd['end'] and rd['type']=='Reserv'
    android_view = None
    if manifest.get('android_framebuffer_memory_view'):
        efi = (ROOT/'mu/Build/degasPkg/DEBUG_CLANGPDB/AARCH64/DegasCheckpointDxe.efi').read_bytes()
        assert b'DEGAS ANDROID MEMORY VIEW full DT framebuffer' in efi, 'corrected path absent from linked EFI'
        view = (base/'Drivers/CheckpointDxe/AndroidMemoryView.h').read_text()
        fb = {k:int(v,16) for k,v in re.findall(r'^#define\s+(ANDROID_FB_\w+)\s+(0x[0-9a-fA-F]+)',view,re.M)}
        chosen = fdt
        fbbase = int(chosen['atag,videolfb-fb_base_l']['hex'],16)
        fbend = fbbase+int(chosen['atag,videolfb-vramSize']['hex'],16)
        assert (fb['ANDROID_FB_BASE'],fb['ANDROID_FB_END']) == (fbbase,fbend)
        display = next(r for r in regions if r['name']=='Display Reserved')
        assert display['base']==fbbase and display['end']==fb['ANDROID_FB_EFI_END'] and display['type']=='Reserv'
        assert all(r['end']<=display['end'] or r['base']>=fbend for r in regions)
        android_view = dict(base=hex(fbbase),end=hex(fbend),type='LoaderData',cache='WB',
                            native_dt_reservation_preserved=True,gop_handoff='BLT only',
                            firmware_allocator_and_windows_map_unchanged=True)
    result = dict(offline_pass=True,phone_accessed=False,hardware_boot_tested=False,
                  stock_sha256=hashlib.sha256(stock).hexdigest(),stock_crc32=hex(zlib.crc32(stock)),
                  protected_source=kernel,initrd_reference=[hex(rdstart),hex(rdend)],
                  descriptor_count=len(regions),all_descriptors_disjoint=True,android_framebuffer_view=android_view,
                  boot_image_sha256=hashlib.sha256((out/'boot.img').read_bytes()).hexdigest())
    (out/'android-efi-qualification.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))

if __name__=='__main__': main()
