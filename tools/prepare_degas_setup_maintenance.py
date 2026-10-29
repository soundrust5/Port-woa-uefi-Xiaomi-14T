"""Host-side maintenance inputs; no phone changes or raw journal."""
from pathlib import Path
import json,hashlib,shutil,subprocess,xml.etree.ElementTree as ET
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/setup-maintenance'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    ns='urn:schemas-microsoft-com:unattend';ET.register_namespace('',ns);ET.register_namespace('wcm','http://schemas.microsoft.com/WMIConfig/2002/State')
    tree=ET.parse(ROOT/'artifacts/degas/full-windows/unattend.xml')
    shell=tree.getroot().find("{%s}settings[@pass='oobeSystem']/{%s}component[@name='Microsoft-Windows-Shell-Setup']"%(ns,ns));assert shell is not None
    auto=ET.SubElement(shell,'{'+ns+'}AutoLogon')
    password=ET.SubElement(auto,'{'+ns+'}Password')
    for parent,name,text in [(password,'Value',''),(password,'PlainText','true'),(auto,'Enabled','true'),(auto,'LogonCount','1'),(auto,'Username','Xiaomi'),(auto,'Domain','DEGAS-PC')]:ET.SubElement(parent,'{'+ns+'}'+name).text=text
    ET.SubElement(shell.find('{'+ns+'}OOBE'),'{'+ns+'}HideEULAPage').text='true'
    tree.write(OUT/'unattend-retry.xml',encoding='utf-8',xml_declaration=True)
    shutil.copyfile(ROOT/'tools/degas_setup_maintenance.cmd',OUT/'maintenance.cmd')
    shutil.copyfile(ROOT/'artifacts/degas/full-windows/degas-install-verify.exe',OUT/'degas-maintenance-verify.exe')
    (OUT/'maintenance.once').write_text('Authorized Xiaomi degas UFS0.2 and failed specialize retry; no image apply.\n')
    source=json.loads((ROOT/'artifacts/degas/full-windows/install-pe/package.json').read_text())
    original=Path(source['candidate']);assert sha(original)==source['candidate_sha256']
    wim=OUT/'boot-maintenance.wim';assert not wim.exists();shutil.copyfile(original,wim)
    exe=OUT/'degas-install-bootstrap.exe';assert b'DGSLOG01' not in exe.read_bytes()
    command='add "'+str(exe)+'" "/Windows/System32/degas-install-bootstrap.exe"\n'
    subprocess.run(['wimlib-imagex','update',str(wim),'2','--check'],input=command,text=True,check=True)
    subprocess.run(['wimlib-imagex','verify',str(wim)],check=True)
    rb=OUT/'pe-readback';rb.mkdir(exist_ok=True)
    subprocess.run(['wimlib-imagex','extract',str(wim),'2','Windows/System32/degas-install-bootstrap.exe','--dest-dir='+str(rb)],check=True)
    assert sha(rb/exe.name)==sha(exe)
    (OUT/'pe-package.json').write_text(json.dumps(dict(wim=str(wim),sha256=sha(wim),startup_sha256=sha(exe),verified=True,raw_journal=False),indent=2))
    print('Maintenance PE verified, no install/apply path selected')
if __name__=='__main__':main()
