"""WSL only: edit extracted clean Xiaomi target hive copies, no running registry."""
from pathlib import Path
import json,struct,hashlib,hivex
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'artifacts/degas/full-windows'
EXPECTED={'SYSTEM':'72d93ad0a7d1b5fd762a71b819157bd0a16b92754165b3aceb0173ce8c13d57f',
          'SOFTWARE':'6561505bc35253d11e44272eb6c0825c1dbd2b0fe7df7293cef4e8e4cc4a4726'}
def node(h,path,create=True):
 n=h.root()
 for part in path.split('\\'):
  child=h.node_get_child(n,part)
  if not child and create:child=h.node_add_child(n,part)
  assert child;n=child
 return n
def main():
 changes=[]
 for filename in ('SYSTEM','SOFTWARE'):
  source=OUT/'hives'/filename;assert hashlib.sha256(source.read_bytes()).hexdigest()==EXPECTED[filename]
  h=hivex.Hivex(str(source),write=True);edits=[]
  if filename=='SYSTEM':
   sets=[h.node_name(n) for n in h.node_children(h.root()) if h.node_name(n).startswith('ControlSet')];assert sets
   for cs in sets:
    for path,key,value in [(r'Control\Power','HibernateEnabled',0),(r'Control\Session Manager\Power','HiberbootEnabled',0),
      (r'Services\partmgr\Parameters','SanPolicy',3),(r'Services\mountmgr','NoAutoMount',1),
      (r'Control\DeviceGuard','EnableVirtualizationBasedSecurity',0),(r'Control\DeviceGuard\Scenarios\HypervisorEnforcedCodeIntegrity','Enabled',0)]:
     edits.append((cs+'\\'+path,key,4,struct.pack('<I',value)))
    # Windows SDK winnt.h GUID_STANDBY_TIMEOUT/HIBERNATE_TIMEOUT/
    # UNATTEND_SLEEP_TIMEOUT/VIDEO_POWERDOWN_TIMEOUT. Apply to every shipped
    # user power scheme before first boot, not only after SetupComplete.
    base=cs+r'\Control\Power\User\PowerSchemes'
    schemes=[h.node_name(n) for n in h.node_children(node(h,base,False))];assert schemes
    settings=[('238c9fa8-0aad-41ed-83f4-97be242c8f20',x) for x in
      ('29f6c1db-86da-48c5-9fdb-f2b67b1f44da','9d7815a6-7ee4-497e-8888-515a05f02364','7bc4a2f9-d8fc-4469-b07b-33eb785aaca0')]
    settings.append(('7516b95f-f776-4464-8c53-06167f40cc99','3c0bc021-c8a8-4e07-a973-6b14cbcb2b7e'))
    for scheme in schemes:
     for group,setting in settings:
      for key in ('ACSettingIndex','DCSettingIndex'):
       edits.append((base+'\\'+scheme+'\\'+group+'\\'+setting,key,4,struct.pack('<I',0)))
  else:
   cert=json.loads((OUT/'test-cert.json').read_text());blob=(OUT/'test-cert-store-blob.bin').read_bytes()
   for store in ('ROOT','TrustedPublisher'):
    edits.append(('Microsoft\\SystemCertificates\\'+store+'\\Certificates\\'+cert['sha1'],'Blob',3,blob))
  for path,key,t,value in edits:h.node_set_value(node(h,path),{'key':key,'t':t,'value':value})
  target=OUT/'hives'/(filename+'-DEGAS');h.commit(str(target));v=hivex.Hivex(str(target))
  for path,key,t,value in edits:assert v.value_value(v.node_get_value(node(v,path,False),key))==(t,value)
  assert hashlib.sha256(source.read_bytes()).hexdigest()==EXPECTED[filename]
  changes.append(dict(hive=filename,source_sha256=EXPECTED[filename],result_sha256=hashlib.sha256(target.read_bytes()).hexdigest(),keys=[p+'\\'+k for p,k,_,_ in edits]))
 (OUT/'hive-config.json').write_text(json.dumps({'changes':changes,'readback':'passed','host_modified':False},indent=2))
 print('PASS: clean original hives unchanged; target policies verified')
if __name__=='__main__':main()
