"""Overlay a packaged diagnostic boot payload, preserving stock AVB/padding."""
from pathlib import Path
import hashlib,json,struct,sys

src,out=map(Path,sys.argv[1:3])
stock=Path('evidence/raw/2026-10-24-degas-return/boot_b.img').read_bytes()
assert hashlib.sha256(stock).hexdigest()=='fa01819342dd24164dea568f88f9715bc3132f738a68c49453907982946c1113'
payload=src.read_bytes()
assert payload[:8]==b'ANDROID!' and struct.unpack_from('<I',payload,40)[0]==4
magic,major,minor,original,offset,size,_=struct.unpack('>4sIIQQQ28s',stock[-64:])
assert magic==b'AVBf' and 0<len(payload)<=original<=offset and stock[offset:offset+4]==b'AVB0'
result=payload+stock[len(payload):]
assert len(result)==67108864 and result[offset:offset+size]==stock[offset:offset+size]
assert result[-64:]==stock[-64:] and not out.exists()
out.write_bytes(result)
assert out.read_bytes()==result
record=dict(sha256=hashlib.sha256(result).hexdigest(),bytes=len(result),target='boot_b',
 payload_bytes=len(payload),preserved_stock_tail=True,stock_signature_valid_for_modified_payload=False)
out.with_suffix('.overlay.json').write_text(json.dumps(record,indent=2))
print(json.dumps(record,indent=2))
