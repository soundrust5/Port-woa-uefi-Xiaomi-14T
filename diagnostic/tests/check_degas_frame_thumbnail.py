"""Cross-check actual C encoder output, including truncation/corruption rejection."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from decode_degas_frame_thumbnail import decode

data = Path(sys.argv[1]).read_bytes()
ppm = decode(data)
header = b'P6\n122 272\n255\n'
assert ppm.startswith(header) and len(ppm) == len(header) + 122 * 272 * 3
pixels = ppm[len(header):]
assert pixels[:3] == b'\xff\x00\x00'
assert pixels[-3:] == b'\x00\x00\xff'
assert not any(pixels[3:-3]), 'padding or unexpected pixel in image'
assert decode(data.replace(b'\n', b'\r\n')) == ppm
bad = [data[:-20], data.replace(b'row=001', b'row=000', 1),
       data.replace(b'f800', b'07e0', 1),
       data + b'DEGAS FRAME RGB565 width=122 height=272\n']
for item in bad:
    try:
        decode(item)
    except ValueError:
        pass
    else:
        raise AssertionError('Accepted corrupted/incomplete thumbnail')
print('Thumbnail decoder PASS: RGB channels, edges, padding, CRLF, corruption, stale-tail rejection')
