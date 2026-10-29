"""Offline test package only: no installation, trust changes or phone access.

Uses the existing local project development key. This is NOT Microsoft signing.
The private key stays outside the output package; no key material is printed.
"""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import sys
from cryptography import x509
from cryptography.hazmat.primitives import hashes
from cryptography.x509.oid import ExtendedKeyUsageOID
from fetch_op12_infverif import OUT as VERIFIER_DIR, EXE_SHA as VERIFIER_SHA

ROOT = Path(__file__).resolve().parents[1]
BUILD = ROOT/'artifacts/oneplus12/touch-windows'
OUT = BUILD/'test-package'
KEY_DIR = ROOT/'artifacts/oneplus12/winpe-log/ufs-binding'
SDK = Path('C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    subprocess.run([sys.executable,str(ROOT/'tools/test_op12_touch_ssdt.py')],check=True)
    source=BUILD/'op12touch.sys'
    data=source.read_bytes()
    pe=struct.unpack_from('<I',data,0x3c)[0]
    assert data[pe:pe+4]==b'PE\0\0'
    assert struct.unpack_from('<H',data,pe+4)[0]==0xaa64
    assert struct.unpack_from('<H',data,pe+24+68)[0]==1  # native subsystem
    assert not (struct.unpack_from('<H',data,pe+24+70)[0]&0x8000), 'Native driver must not be Terminal Server aware'
    inf=ROOT/'diagnostic/oneplus12/touch/windows/op12touch.inf'
    assert (BUILD/inf.name).read_text()==inf.read_text(), 'stale build INF'
    verifier=VERIFIER_DIR/'infverif.exe'
    if not verifier.exists():
        raise RuntimeError('Run tools/fetch_op12_infverif.py first')
    assert sha(verifier)==VERIFIER_SHA, 'Unreviewed INF verifier'
    verification=subprocess.run([str(verifier),'/w','/v',str(inf)],
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (BUILD/'infverif.log').write_bytes(verification.stdout)
    if verification.returncode:
        raise RuntimeError('InfVerif rejected package; see infverif.log')
    cert=x509.load_der_x509_certificate((KEY_DIR/'op12ufs.cer').read_bytes())
    assert cert.issuer==cert.subject
    assert 'development test only' in cert.subject.rfc4514_string()
    assert ExtendedKeyUsageOID.CODE_SIGNING in cert.extensions.get_extension_for_class(x509.ExtendedKeyUsage).value
    key=KEY_DIR/'local-test-only.pfx'
    assert key.is_file()
    OUT.mkdir(parents=True,exist_ok=True)
    assert not list(OUT.glob('*.pfx')) and not list(OUT.glob('*.pem'))
    shutil.copyfile(source,OUT/source.name)
    (OUT/inf.name).write_bytes(inf.read_text().replace('\n','\r\n').encode('ascii'))
    shutil.copyfile(KEY_DIR/'op12ufs.cer',OUT/'op12-development.cer')
    logs=[]
    def run(args):
        result=subprocess.run(args,cwd=OUT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        logs.append(result.stdout.decode('utf-8',errors='replace'))
        if result.returncode:
            (BUILD/'test-signing.log').write_text('\n'.join(logs),encoding='utf-8')
            raise RuntimeError('Packaging tool failed; see test-signing.log')
    run([str(SDK/'signtool.exe'),'sign','/fd','SHA256','/f',str(key),'op12touch.sys'])
    (OUT/'op12touch.cdf').write_text(
        '[CatalogHeader]\nName=op12touch.cat\nPublicVersion=0x0000001\nEncodingType=0x00010001\n'
        'CATATTR1=0x10010001:OSAttr:2:10.0\n[CatalogFiles]\n'
        '<HASH>op12touch.inf=op12touch.inf\n<HASH>op12touch.sys=op12touch.sys\n',encoding='ascii')
    run([str(SDK/'makecat.exe'),'/v','op12touch.cdf'])
    run([str(SDK/'signtool.exe'),'sign','/fd','SHA256','/f',str(key),'op12touch.cat'])
    (BUILD/'test-signing.log').write_text('\n'.join(logs),encoding='utf-8')
    manifest={
        'status':'OFFLINE_TEST_SIGNED_CLIENT_HARDWARE_DISABLED',
        'microsoft_signed':False,'test_signing_required':True,
        'certificate_subject':cert.subject.rfc4514_string(),
        'certificate_sha256':cert.fingerprint(hashes.SHA256()).hex(),
        'files':{n:sha(OUT/n) for n in ['op12touch.sys','op12touch.inf','op12touch.cat','op12-development.cer']},
        'unsigned_sys_sha256':sha(source),'source_inf_sha256':sha(inf),
        'target':r'ACPI\OP120003','hardware_enabled':False,'installed':False,
        'hardware_tested':False,'host_trust_modified':False,'private_key_packaged':False,
        'test_modes':{'0':'disabled','1':'identify and application-info, then hold reset; no HID',
                      '2':'volatile report configuration and HID test'},
        'diagnostic':{'version':1,'record_bytes':112,'trace_records':32,
                      'registry_values':['TouchDiagnostic','TouchTrace'],
                      'persistent_only_after':'WinPE collector copies to verified NTFS session'},
        'infverif':{'mode':'/w','exit_code':verification.returncode,'tool_sha256':VERIFIER_SHA},
        'certification':'InfVerif /w passed. No Microsoft submission or HLK validation',
        'requires_before_deployment':[
            'Real GENI SPB provider with bounded full-duplex SPI and 0xff MOSI',
            'Real GPIO interrupt/output provider and RPMh/clock/pinmux power ownership',
            'For mode 2: measured sensor/HID geometry, orientation and stationary-finger report cadence',
            'PnP/VHF enumeration, IRQ, suspend/resume and hardware tests']}
    (OUT/'package.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(manifest,indent=2))


if __name__=='__main__':
    main()
