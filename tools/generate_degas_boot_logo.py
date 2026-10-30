"""Compile the repository's monochrome Mi PNG to a bounded grayscale RLE.
SVG paths originate from the Xiaomi homepage; no font approximations are used.
"""
from pathlib import Path
import hashlib,itertools,json
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
def main():
 p=ROOT/'assets/degas-uefi/mi-monochrome.png'
 im=Image.open(p).convert('RGB');assert im.size==(360,360)
 values=[]
 for r,g,b in im.getdata():
  assert r==g==b
  values.append(r)
 encoded=bytearray()
 for value,items in itertools.groupby(values):
  count=sum(1 for _ in items)
  while count:
   n=min(count,65535);encoded+=bytes([n&255,n>>8,value]);count-=n
 # Independent round trip, including row-crossing runs.
 decoded=[]
 for i in range(0,len(encoded),3):decoded.extend([encoded[i+2]]*(encoded[i]|encoded[i+1]<<8))
 assert decoded==values
 header='// Generated from assets/degas-uefi/mi-monochrome.png; exact image, not text.\n#define DEGAS_LOGO_WIDTH 360U\n#define DEGAS_LOGO_HEIGHT 360U\nSTATIC CONST UINT8 mDegasMiLogoRle[]={\n'
 for i in range(0,len(encoded),24):header+=','.join('0x%02x'%x for x in encoded[i:i+24])+',\n'
 header+='};\n'
 (ROOT/'mu/Platforms/Xiaomi/degasPkg/Drivers/CheckpointDxe/LogoImage.h').write_text(header)
 (ROOT/'assets/degas-uefi/raster-manifest.json').write_text(json.dumps(dict(png_sha256=hashlib.sha256(p.read_bytes()).hexdigest(),rle_sha256=hashlib.sha256(encoded).hexdigest(),rle_bytes=len(encoded),width=360,height=360),indent=2))
 print('PASS:360x360 logo RLE round trip, bytes',len(encoded))
if __name__=='__main__':main()
