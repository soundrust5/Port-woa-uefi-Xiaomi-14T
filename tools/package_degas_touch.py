"""Locally test-sign the experimental degas touch package; no host installation.

The optional WinPE VHF child fallback matches HID_DEVICE_SYSTEM_VHF broadly.
It is packaged separately from the source device and is not an instruction to
replace an operational inbox HID stack in full Windows. Private keys stay out.
"""
import json
import shutil
import struct
import subprocess
from package_op12_touch import ROOT, KEY_DIR, SDK, VERIFIER_DIR, VERIFIER_SHA, sha

BUILD = ROOT / 'artifacts/degas/touch-windows'
OUT = BUILD / 'test-package'
SOURCE = ROOT / 'diagnostic/degas/touch/windows'
DONOR = ROOT / 'artifacts/oneplus12/touch-windows/mshidkmdf-donor/mshidkmdf.sys'
DONOR_SHA = 'c363d97c5f036acc7a863e98d949c32873a056b37aabef02a17b37a3a9fe272c'


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    verifier = VERIFIER_DIR / 'infverif.exe'
    assert sha(verifier) == VERIFIER_SHA
    logs = []

    def run(args):
        r = subprocess.run([str(x) for x in args], cwd=OUT, capture_output=True)
        logs.append((r.stdout + r.stderr).decode(errors='replace'))
        (BUILD / 'package.log').write_text('\n'.join(logs))
        if r.returncode:
            raise RuntimeError('Package validation/signing failed; inspect package.log')

    for name in ['degastouch', 'degasvhf']:
        run([verifier, '/w', '/v', SOURCE / (name + '.inf')])
        text = (SOURCE / (name + '.inf')).read_text()
        (OUT / (name + '.inf')).write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('ascii'))
    binary = BUILD / 'degastouch.sys'
    b = binary.read_bytes()
    pe = struct.unpack_from('<I', b, 60)[0]
    assert b[pe:pe+4] == b'PE\0\0' and struct.unpack_from('<H', b, pe+4)[0] == 0xaa64
    assert struct.unpack_from('<H', b, pe+24+68)[0] == 1
    assert not (struct.unpack_from('<H', b, pe+24+70)[0] & 0x8000)
    assert sha(DONOR) == DONOR_SHA, 'Unmodified, verified Microsoft HID miniport required'
    shutil.copyfile(binary, OUT / binary.name)
    shutil.copyfile(DONOR, OUT / DONOR.name)
    shutil.copyfile(KEY_DIR / 'op12ufs.cer', OUT / 'development.cer')
    key = KEY_DIR / 'local-test-only.pfx'
    run([SDK / 'signtool.exe', 'sign', '/fd', 'SHA256', '/f', key, 'degastouch.sys'])
    for stem, files in [('degastouch', ['degastouch.inf', 'degastouch.sys']),
                        ('degasvhf', ['degasvhf.inf', 'mshidkmdf.sys'])]:
        cdf = BUILD / (stem + '.cdf')
        cdf.write_text('[CatalogHeader]\nName=' + stem + '.cat\nPublicVersion=0x0000001\n'
                       'EncodingType=0x00010001\nCATATTR1=0x10010001:OSAttr:2:10.0\n'
                       '[CatalogFiles]\n' + ''.join('<HASH>' + f + '=' + f + '\n' for f in files))
        run([SDK / 'makecat.exe', '/v', cdf])
        run([SDK / 'signtool.exe', 'sign', '/fd', 'SHA256', '/f', key, stem + '.cat'])
    assert sha(OUT / 'mshidkmdf.sys') == DONOR_SHA
    assert not list(OUT.glob('*.pfx'))
    manifest = dict(target='ACPI\\DGS0002', service='DegasTouch',
                    microsoft_signed=False, test_signing_required=True,
                    host_trust_modified=False, hardware_tested=False,
                    vhf_fallback_target='HID_DEVICE_SYSTEM_VHF',
                    vhf_fallback_scope='experimental WinPE only',
                    files={p.name: sha(p) for p in sorted(OUT.iterdir()) if p.is_file() and p.name != 'package.json'},
                    sources={str(p.relative_to(ROOT)): sha(p) for p in sorted(SOURCE.glob('*')) if p.is_file()})
    (OUT / 'package.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print('PASS: ARM64 test signatures and INF validation; no host installation')


if __name__ == '__main__':
    main()
