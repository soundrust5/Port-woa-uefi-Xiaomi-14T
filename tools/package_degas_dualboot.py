"""Compose a fresh-LK one-shot Android route; never writes a device."""
from pathlib import Path
import argparse,gzip,hashlib,json,struct,subprocess,tempfile

ROOT=Path(__file__).resolve().parents[1]
STOCK_SHA='dbb1b31dfc4cc1beee2bd4600591cf5420d39969b5ab142b9eeb3ef2e442668a'
MAGIC=int.from_bytes(b'DGANDR01','little')
def sha(b):return hashlib.sha256(b).hexdigest()
def build_route(directory, source=None, stem='android-route'):
    directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    # Existing project toolchain is WSL Ubuntu clang/lld, not a downloaded compiler.
    def wp(p):
        p=str(Path(p).resolve()).replace('\\','/')
        return '/mnt/'+p[0].lower()+p[2:]
    source=source or ROOT/'diagnostic/degas_android_route.S'
    obj=directory/(stem+'.o');elf=directory/(stem+'.elf');binary=directory/(stem+'.bin')
    for args in [
      ['clang','--target=aarch64-none-elf','-c',wp(source),'-o',wp(obj)],
      ['ld.lld','--image-base=0','-Ttext=0','--entry=DegasAndroidRoute',wp(obj),'-o',wp(elf)],
      ['llvm-objcopy','-O','binary','--only-section=.text',wp(elf),wp(binary)]]:
        subprocess.run(['wsl','--',*args],check=True,capture_output=True)
    symbols=subprocess.run(['wsl','--','llvm-nm','-n',wp(elf)],check=True,capture_output=True,text=True).stdout
    (directory/(stem+'.symbols.txt')).write_text(symbols)
    return binary.read_bytes(),symbols

def compose(kernel,stock,route,prime=None,normal_offset=None):
    image=bytearray(gzip.decompress(kernel))
    if len(image)!=0x4480000 or image[56:60]!=b'ARMd':raise ValueError('Unexpected existing UEFI Image layout')
    instruction=struct.unpack_from('<I',image)[0]
    if instruction!=0x14000010:raise ValueError('Expected original entry branch to +0x40')
    if sha(stock)!=STOCK_SHA:raise ValueError('Stock Android Image provenance mismatch')
    if struct.unpack_from('<QQQ',stock,8)!=(0,0x2270000,10):raise ValueError('Unexpected stock Image header')
    if len(route)>0x1000 or any(image[0x10000:0x10000+len(route)]):raise ValueError('Early route code overlaps wrapper data')
    image[0x10000:0x10000+len(route)]=route
    if prime is not None:
        if not prime or len(prime)>0x1000 or any(image[0x11000:0x11000+len(prime)]):raise ValueError('Prime entry overlaps wrapper')
        if normal_offset is None or normal_offset<0 or normal_offset+12>len(route) or normal_offset&3:raise ValueError('Invalid Android normal-route symbol')
        expected=struct.pack('<III',0xd2800804,0xf2a80004,0xd61f0080)
        if route[normal_offset:normal_offset+12]!=expected:raise ValueError('Normal-route instructions changed')
        delta=0x11000-(0x10000+normal_offset)
        struct.pack_into('<I',image,0x10000+normal_offset,0x14000000|((delta//4)&0x3ffffff))
        image[0x11000:0x11000+len(prime)]=prime
    struct.pack_into('<I',image,0,0x14004000) # B +0x10000
    image[0x400000:0x2670000]=stock+bytes(0x2270000-len(stock))
    assert len(image)==0x4480000 and 0x40000000+len(image)<0x47c80000
    return bytes(image)

def main():
    p=argparse.ArgumentParser();p.add_argument('kernel',type=Path);p.add_argument('output',type=Path)
    p.add_argument('--stock',type=Path,default=ROOT/'artifacts/degas/android-stock-Image')
    p.add_argument('--boot-core',type=int,choices=[0,7],default=0);a=p.parse_args()
    a.output.mkdir(parents=True,exist_ok=True)
    route,symbols=build_route(a.output);kernel=a.kernel.read_bytes();stock=a.stock.read_bytes()
    prime=None;normal=None
    if a.boot_core==7:
        prime,_=build_route(a.output,ROOT/'diagnostic/degas_prime_entry.S','prime-entry')
        normal=next(int(line.split()[0],16) for line in symbols.splitlines() if line.split()[-1]=='DegasAndroidNormal')
    image=compose(kernel,stock,route,prime,normal);packed=gzip.compress(image,compresslevel=9,mtime=0)
    (a.output/'kernel.bin.gz').write_bytes(packed)
    report=dict(input_uefi_sha256=sha(kernel),stock_sha256=sha(stock),route_source_sha256=sha((ROOT/'diagnostic/degas_android_route.S').read_bytes()),route_sha256=sha(route),image_sha256=sha(image),gzip_sha256=sha(packed),image_bytes=len(image),gzip_bytes=len(packed),flag_address='0x480aef00',flag_magic=hex(MAGIC),flag_inverse=hex(MAGIC^0xffffffffffffffff),source='0x40400000',destination='0x40000000',footprint='0x2270000',trampoline='0x44470000',normal_entry='0x40000040')
    report['boot_core']=a.boot_core
    if prime is not None:report.update(prime_sha256=sha(prime),prime_source_sha256=sha((ROOT/'diagnostic/degas_prime_entry.S').read_bytes()),prime_entry='0x40011000')
    (a.output/'dualboot-manifest.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
