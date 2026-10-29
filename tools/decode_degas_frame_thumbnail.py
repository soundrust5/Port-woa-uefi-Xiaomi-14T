"""Decode a complete, checksum-verified UEFI RGB565 RAM thumbnail into PPM."""
import argparse
from pathlib import Path
import re


def decode(data):
    marker = b'DEGAS FRAME RGB565 width=122 height=272\n'
    # Last begin wins: never silently fall back to an older successful capture.
    body = data.replace(b'\r\n', b'\n').rsplit(marker, 1)
    if len(body) != 2:
        raise ValueError('No thumbnail header')
    lines = body[1].splitlines()
    if len(lines) < 273:
        raise ValueError('Truncated thumbnail')
    pixels = bytearray()
    checksum = 2166136261
    for row in range(272):
        match = re.fullmatch(rb'DEGAS FRAME row=(\d{3}) ([0-9a-f]{488})', lines[row])
        if not match or int(match[1]) != row:
            raise ValueError(f'Missing, reordered or invalid row {row}')
        raw = bytes.fromhex(match[2].decode('ascii'))
        for byte in raw:
            checksum = ((checksum ^ byte) * 16777619) & 0xffffffff
        for pos in range(0, len(raw), 2):
            value = int.from_bytes(raw[pos:pos + 2], 'big')
            r, g, b = value >> 11, (value >> 5) & 63, value & 31
            pixels.extend(((r << 3) | (r >> 2), (g << 2) | (g >> 4),
                           (b << 3) | (b >> 2)))
    end = re.fullmatch(rb'DEGAS FRAME END fnv=([0-9A-Fa-f]{8})', lines[272])
    if not end or int(end[1], 16) != checksum:
        raise ValueError('Thumbnail checksum mismatch or missing end')
    return b'P6\n122 272\n255\n' + pixels


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('log', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    ppm = decode(args.log.read_bytes())
    args.output.write_bytes(ppm)
    print(f'Complete RGB thumbnail: 122x272, {len(ppm)} bytes, {args.output}')


if __name__ == '__main__':
    main()
