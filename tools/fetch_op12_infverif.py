"""Extract the pinned Microsoft INF verifier locally; no WDK installation."""
import hashlib
from pathlib import Path
import urllib.request
import zipfile

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/toolchains/wdk-x64-validation-10.0.26100.6584'
URL=('https://api.nuget.org/v3-flatcontainer/microsoft.windows.wdk.x64/'
     '10.0.26100.6584/microsoft.windows.wdk.x64.10.0.26100.6584.nupkg')
SHA='c393d03dfb640b5c92f546b32f6770ef68cd3aaf691956e7d66d8e2c28a1b55e'
EXE_SHA='859e311fc5fcdbc041750e50f3673258cbf90f46dbb170c27d7f9fc4f2afdf3a'


def main():
    OUT.mkdir(parents=True,exist_ok=True)
    pkg=OUT/'package.nupkg'
    if not pkg.exists():
        temp=OUT/'package.tmp'
        with urllib.request.urlopen(URL,timeout=40) as src,temp.open('wb') as dst:
            count=0
            while chunk:=src.read(1024*1024):
                count+=len(chunk)
                if count>128*1024*1024:raise ValueError('Unexpected WDK size')
                dst.write(chunk)
        if hashlib.sha256(temp.read_bytes()).hexdigest()!=SHA:
            raise ValueError('WDK hash mismatch')
        temp.replace(pkg)
    if hashlib.sha256(pkg.read_bytes()).hexdigest()!=SHA:
        raise ValueError('Cached WDK hash mismatch')
    with zipfile.ZipFile(pkg) as archive:
        for member in ['c/tools//10.0.26100.0/x64/infverif.exe',
                       'c/build/10.0.26100.0/bin/x64/infverif.dll',
                       'c/build/10.0.26100.0/bin/x64/infverifhlk.dll','LICENSE.txt']:
            (OUT/Path(member).name).write_bytes(archive.read(member))
    if hashlib.sha256((OUT/'infverif.exe').read_bytes()).hexdigest()!=EXE_SHA:
        raise ValueError('Verifier hash mismatch')
    print('Pinned InfVerif ready; no host installation or driver changes')


if __name__=='__main__':main()
