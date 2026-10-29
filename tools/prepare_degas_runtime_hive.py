from pathlib import Path
import struct,hivex
root=Path(__file__).resolve().parents[1]/'artifacts/degas/runtime-log'
source=root/'SYSTEM';target=root/'SYSTEM.runtime'
b=source.read_bytes();assert b[:4]==b'regf' and b[4:8]==b[8:12], 'Dirty source hive'
def snapshot(h):
 result={}
 def visit(n,p):
  result[p]={h.value_key(v):h.value_value(v) for v in h.node_values(n)}
  for c in h.node_children(n):visit(c,p+'\\'+h.node_name(c))
 visit(h.root(),'');return result
h=hivex.Hivex(str(source),write=True);before=snapshot(h);expected=dict(before)
for cs in [h.node_name(n) for n in h.node_children(h.root()) if h.node_name(n).startswith('ControlSet')]:
 path=cs+'\\Services\\DegasRuntime';n=h.root()
 for part in path.split('\\'):n=h.node_get_child(n,part) or h.node_add_child(n,part)
 assert '\\'+path not in before, 'Service already exists'
 values={k:(4,struct.pack('<I',v)) for k,v in [('Type',16),('Start',2),('ErrorControl',0)]}
 for k,v in [('ImagePath',r'\SystemRoot\System32\DegasRuntime.exe'),('ObjectName','LocalSystem'),('DisplayName','Degas runtime CPU diagnostics')]:values[k]=(2 if k=='ImagePath' else 1,(v+'\0').encode('utf-16le'))
 for k,(t,v) in values.items():h.node_set_value(n,dict(key=k,t=t,value=v))
 expected['\\'+path]=values
h.commit(str(target));assert snapshot(hivex.Hivex(str(target)))==expected
b=target.read_bytes();assert b[4:8]==b[8:12]
print('PASS: only DegasRuntime service added; setup and drivers unchanged')
