"""Prepare a deterministic gzip probe candidate on the host, not boot.img."""
import gzip
import hashlib
import io
import json
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
build = ROOT/'diagnostic/build'
data = (build/'entry.bin').read_bytes()
manifest = json.loads((build/'manifest.json').read_text())
assert hashlib.sha256(data).hexdigest() == manifest['image_sha256']
assert data[56:60] == b'ARM\x64'
assert struct.unpack_from('<Q', data, 16)[0] == len(data)
output = io.BytesIO()
with gzip.GzipFile(filename='', mode='wb', fileobj=output, mtime=0, compresslevel=9) as stream:
    stream.write(data)
compressed = output.getvalue()
assert compressed[:4] == b'\x1f\x8b\x08\x00'
assert gzip.decompress(compressed) == data
(build/'entry.bin.gz').write_bytes(compressed)
report = dict(format='gzip, no name/time/optional flags', size=len(compressed),
    sha256=hashlib.sha256(compressed).hexdigest(), uncompressed_sha256=manifest['image_sha256'],
    uncompressed_size=len(data), round_trip_verified=True, phone_tested=False,
    status='HOST_ONLY_KERNEL_CANDIDATE; not Android boot.img; no observation channel',
    evidence='docs/LK-HANDOFF-STATIC.md',
    limitations=['Compression dispatch only checked in exact LK code; full LK decompression not emulated.',
                 'AVB/container acceptance, secure handoff, writable ownership and display update remain unverified.'])
(build/'kernel-candidate.json').write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(report,indent=2))
