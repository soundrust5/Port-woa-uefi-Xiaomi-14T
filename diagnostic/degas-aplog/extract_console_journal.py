"""Extract a complete CRC-checked raw AP journal from firmware console text."""
import argparse
import json
from pathlib import Path
import re
import uuid
import zlib

from ap_journal_v1 import decode, JournalError

BEGIN = re.compile(rb"^DEGAS APJ RAW BEGIN crc=([0-9a-f]{8}) bytes=4096$")
LINE = re.compile(rb"^DEGAS APJ RAW ([0-9a-f]{4}) ([0-9a-f]{32})$")
END = re.compile(rb"^DEGAS APJ RAW END crc=([0-9a-f]{8})$")


def extract_frames(data):
    """Ignore incomplete frames; require every offset and matching CRC/trailer."""
    active = None
    for line in data.splitlines():
        found = BEGIN.fullmatch(line)
        if found:
            active = (int(found[1], 16), bytearray())
            continue
        if active is None:
            continue
        found = LINE.fullmatch(line)
        if found:
            checksum, raw = active
            if int(found[1], 16) != len(raw) or len(raw) >= 4096:
                active = None
            else:
                raw.extend(bytes.fromhex(found[2].decode("ascii")))
            continue
        found = END.fullmatch(line)
        if found:
            checksum, raw = active
            if (len(raw) == 4096 and int(found[1], 16) == checksum and
                    zlib.crc32(raw) & 0xFFFFFFFF == checksum):
                yield bytes(raw)
            active = None
        # Independent BSP IRQ log lines may appear between the journal lines.


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("console", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--attempt", required=True, type=uuid.UUID)
    parser.add_argument("--source-manifest-sha256", "--build-sha256", dest="identity_hash", type=bytes.fromhex)
    args = parser.parse_args()
    matches = []
    for raw in extract_frames(args.console.read_bytes()):
        try:
            result = decode(raw, args.attempt, args.identity_hash)
        except JournalError:
            continue
        matches.append((raw, result))
    if not matches:
        parser.exit(2, "No complete CRC-valid journal matches the requested attempt.\n")
    # Repeated identical startup/pre-overwrite backups are expected. Refuse
    # divergent records sharing an identity rather than silently picking one.
    if any(raw != matches[-1][0] for raw, _ in matches):
        parser.exit(2, "Conflicting raw journals share the requested attempt.\n")
    with args.output.open("xb") as output:
        output.write(matches[-1][0])
    print(json.dumps(matches[-1][1], indent=2))


if __name__ == "__main__":
    main()
