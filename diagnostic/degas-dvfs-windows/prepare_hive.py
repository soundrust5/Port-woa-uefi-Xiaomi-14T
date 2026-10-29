"""WSL: add only the demand diagnostic driver and SYSTEM logger services."""
from pathlib import Path
import struct,hivex
p=Path('artifacts/degas/dvfs-windows/install-v1');source=p/'SYSTEM.original';target=p/'SYSTEM.candidate'
b=source.read_bytes();assert b[:4]==b'regf' and b[4:8]==b[8:12]
def snapshot(h):
 r={}
 def visit(n,path):
  r[path]={h.value_key(v):h.value_value(v) for v in h.node_values(n)}
  for c in h.node_children(n):visit(c,path+'\\'+h.node_name(c))
 visit(h.root(),'');return r
h=hivex.Hivex(str(source),write=True);before=snapshot(h);expected=dict(before)
for cs in [h.node_name(n) for n in h.node_children(h.root()) if h.node_name(n).startswith('ControlSet')]:
 for name,kind,start,image in [('DegasDvfs',1,3,r'\SystemRoot\System32\drivers\degasdvfs.sys'),('DegasDvfsLog',16,2,r'%SystemRoot%\System32\DegasDvfsLog.exe')]:
  path=cs+'\\Services\\'+name;assert '\\'+path not in before
  n=h.root()
  for part in path.split('\\'):n=h.node_get_child(n,part) or h.node_add_child(n,part)
  values={k:(4,struct.pack('<I',v)) for k,v in [('Type',kind),('Start',start),('ErrorControl',0)]}
  values['ImagePath']=(2,(image+'\0').encode('utf-16le'))
  values['DisplayName']=(1,(name+'\0').encode('utf-16le'))
  if kind==16:values['ObjectName']=(1,'LocalSystem\0'.encode('utf-16le'))
  for k,(t,v) in values.items():h.node_set_value(n,{'key':k,'t':t,'value':v})
  expected['\\'+path]=values
h.commit(str(target));assert snapshot(hivex.Hivex(str(target)))==expected
b=target.read_bytes();assert b[4:8]==b[8:12]
print('PASS: exact semantic change is two new services; driver demand-start, observer auto-start')
