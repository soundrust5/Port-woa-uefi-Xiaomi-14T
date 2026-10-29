"""Compile the exact current DPM handler body with host substitutes for kernel helpers."""
from pathlib import Path
import hashlib
import json
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'diagnostic/degas-dvfs-windows'
OUT = ROOT / 'artifacts/degas/dvfs-windows'
text = (SRC / 'degas_pep.c').read_text()

def function(signature):
    start = text.index(signature)
    brace = text.index('{', start)
    level = 1
    end = brace + 1
    while level:
        level += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]

wdk = ROOT / 'artifacts/toolchains/wdk-arm64-10.0.26100.6584/c/Include/10.0.26100.0/km/pep_x.h'
defines = '\n'.join('#define '+name+' '+value for name,value in re.findall(
    r'^#define (PEP_DPM_\w+)\s+(0x[0-9A-Fa-f]+)', wdk.read_text(), re.M))
body = function('BOOLEAN DegasAcceptDevice(ULONG Notification, PVOID Data)')
work_body = function('static BOOLEAN DegasConstraintWork(')
unit = ('#include "provider_handler_host_prefix.h"\n'+defines+'\n'+
        function('static int DegasPathUid(')+'\n'+
        function('static DEGAS_CPU *DegasCpuFromHandle(')+'\n'+
        work_body+'\n'+body+'\n'+
        (SRC / 'tests/provider_handler_host_cases.c').read_text())
OUT.mkdir(parents=True,exist_ok=True)
unit_path = OUT / 'provider_handler_host.c'
unit_path.write_text(unit)
cmd = ['wsl','--exec','bash','-lc',
       'clang -std=c11 -Wall -Wextra -Werror -I diagnostic/degas-dvfs-windows/tests '
       'artifacts/degas/dvfs-windows/provider_handler_host.c '
       'diagnostic/degas-dvfs-windows/degas_protocol.c '
       '-o artifacts/degas/dvfs-windows/test_provider_handler && '
       'artifacts/degas/dvfs-windows/test_provider_handler']
subprocess.run(cmd,cwd=ROOT,check=True)
(OUT/'provider-handler-test.json').write_text(json.dumps(dict(
    passed=True, handler_sha256=hashlib.sha256(body.encode()).hexdigest(),
    work_handler_sha256=hashlib.sha256(work_body.encode()).hexdigest(),
    exact_body_extracted=True, kernel_helpers_modeled=True),indent=2)+'\n')
