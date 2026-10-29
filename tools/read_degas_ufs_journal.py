"""Read/export the two diagnostic journal slots; never write to the phone."""
import argparse, datetime, json, struct, zlib
from pathlib import Path

LBA = 29800448
SLOT = 65536

def decode(data):
    valid = []
    if len(data) != SLOT * 2:
        raise ValueError('expected two complete journal slots')
    for i in range(2):
        slot = data[i*SLOT:(i+1)*SLOT]
        if slot[:8] != b'DGSLOG01':
            continue
        version, length, session, sequence, crc, header_crc = struct.unpack_from('<IIQIII', slot, 8)
        if version != 1 or length > 61440 or not sequence:
            continue
        payload = slot[4096:4096+length]
        if zlib.crc32(slot[:32]) != header_crc or zlib.crc32(payload) != crc:
            continue
        valid.append(dict(slot=i, session=session, sequence=sequence, payload=payload))
    return sorted(valid, key=lambda x: (x['session'], x['sequence']))

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--input', type=Path, help='decode a saved raw journal without ADB')
    args = ap.parse_args()
    if args.input:
        data = args.input.read_bytes()
    else:
        from stage_degas_ufs_winpe import shell, verify_gpt
        assert shell('getprop ro.product.device').strip() == b'degas'
        verify_gpt()
        data = shell(f'dd if=/dev/block/sdc bs=4096 skip={LBA} count=32 2>/dev/null')
    out = Path(__file__).resolve().parents[1]/'evidence/raw'/('degas-ufs-journal-'+datetime.datetime.now().strftime('%Y%m%d-%H%M%S'))
    out.mkdir(exist_ok=False)
    (out/'slots.bin').write_bytes(data)
    records = decode(data)
    if not records:
        raise RuntimeError(f'No committed CRC-valid journal. Raw data saved: {out}')
    for r in records:
        (out/f"slot-{r['slot']}.txt").write_bytes(r.pop('payload'))
    (out/'manifest.json').write_text(json.dumps(records, indent=2))
    print(out);print(json.dumps(records, indent=2))

if __name__ == '__main__':
    main()
