"""Decode a CRC-qualified DGBUG001 record from raw RAM/pstore/UEFI raw dump."""
from pathlib import Path
import argparse,json,re,struct,zlib

def decode(data):
 records=[]
 for hit in re.finditer(b'DGBUG001',data):
  b=data[hit.start():hit.start()+256]
  if len(b)!=256 or struct.unpack_from('<II',b,8)!=(1,256):continue
  crc,inv=struct.unpack_from('<II',b,240)
  if crc^inv!=0xffffffff or zlib.crc32(b[8:240])!=crc:continue
  q=lambda n:struct.unpack_from('<Q',b,n)[0]
  d=lambda n:struct.unpack_from('<I',b,n)[0]
  records.append(dict(offset=hit.start(),sequence=q(16),stage=d(24),event=d(28),context=hex(q(32)),code=hex(q(40)),parameters=[hex(q(n)) for n in (48,56,64,72)],data_valid=bool(d(80)),registered=bool(d(84)),init_status=hex(d(104)),crash=bool(d(108)),watchdog_mapped=bool(d(128)),last_stage=d(132),boot_epoch=q(136)))
 return records
if __name__=='__main__':
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('input',type=Path);p.add_argument('--output',type=Path);a=p.parse_args();raw=a.input.read_bytes();rows=[]
 # UEFl text has an exact full4KiB dump; scan records reconstructed from each block.
 for block in re.findall(rb'DEGAS APJ RAW BEGIN[^\n]*\n(.*?)DEGAS APJ RAW END',raw,re.S):
  chunks=re.findall(rb'DEGAS APJ RAW ([0-9a-f]{4}) ([0-9a-f]{32})',block)
  if len(chunks)==256 and all(int(o,16)==i*16 for i,(o,_) in enumerate(chunks)):rows.extend(decode(b''.join(bytes.fromhex(v.decode()) for _,v in chunks)))
 rows.extend(decode(raw));text=json.dumps(rows,indent=2)+'\n';print(text)
 if a.output:a.output.write_text(text)
