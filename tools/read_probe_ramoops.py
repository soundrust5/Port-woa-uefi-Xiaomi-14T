"""Validate our build ID and CRC inside an Android console-ramoops file."""
import argparse
import json
from pathlib import Path
import re
import zlib


def decode(data, expected_build_id):
    results = []
    pattern = rb'DEGAS-RAMOOPS-V1 BUILD=([0-9a-f]{64})\n.*?CRC32=([0-9a-f]{8})\n'
    for match in re.finditer(pattern, data, re.DOTALL):
        body = match.group().rsplit(b'CRC32=', 1)[0]
        actual = zlib.crc32(body)
        if actual != int(match[2], 16):
            continue
        if match[1].decode() != expected_build_id:
            continue
        results.append(dict(offset=match.start(), build_id=expected_build_id,
                            crc32=f'{actual:08x}', text=match.group().decode('ascii')))
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('console', type=Path)
    parser.add_argument('--manifest', type=Path, default=Path('diagnostic/build-ramoops/manifest.json'))
    args = parser.parse_args()
    result = decode(args.console.read_bytes(), json.loads(args.manifest.read_text())['build_id'])
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result else 1)
