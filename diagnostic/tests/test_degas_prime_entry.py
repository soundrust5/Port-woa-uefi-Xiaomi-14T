"""Two emulated CPUs execute the actual opt-in boot-core handoff instructions.
PSCI, sysregs and shared physical RAM are modeled; this is not an ATF test.
"""
from pathlib import Path
import hashlib,json,struct,sys,unittest
ROOT=Path(__file__).resolve().parents[2];A=ROOT/'artifacts/degas/cpu-audit'
sys.path.insert(0,str(A/'test-deps'))
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE,UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
CODE=(A/'prime-entry.bin').read_bytes();META=json.loads((A/'prime-entry.json').read_text())
assert hashlib.sha256(CODE).hexdigest()==META['code_sha256']
assert hashlib.sha256((ROOT/META['source']).read_bytes()).hexdigest()==META['source_sha256']
SYMS={x.split()[2]:int(x.split()[0],16) for x in (A/'prime-entry-symbols.txt').read_text().splitlines()}
B=0x40011000;REC=B+SYMS['DegasPrimeRecord'];TARGET=0x40000040;WDT=0x1c00a000
class PrimeEntry(unittest.TestCase):
 def run_case(self,on_result=0,ap_state_on_failure=1,off_denied=False,ap_launch=True,bsp_mpidr=0x81000000,ap_mpidr=0x81000700,bsp_sctlr=0x30d00800,ap_sctlr=0x30d00800,bsp_el=4,ap_el=4):
  cpus=[Uc(UC_ARCH_ARM64,UC_MODE_ARM),Uc(UC_ARCH_ARM64,UC_MODE_ARM)]
  states=['ON','OFF'];parked=[False,False];booted=[];on_calls=[];aff_calls=[];wdt=[];daif=[0x280,0x3c0]
  args=[0x47c80000,0xa1b2c3d4,0x12345678,0xfedcba98];sp=0x50808000
  for cpu,u in enumerate(cpus):
   u.mem_map(B,4096);u.mem_write(B,CODE);u.mem_map(0x40000000,4096);u.mem_map(WDT,4096)
   u.reg_write(UC_ARM64_REG_SP,sp if cpu==0 else 0)
   for reg,value in zip([UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3],args if cpu==0 else [0]*4):u.reg_write(reg,value)
   u.reg_write(UC_ARM64_REG_PC,B if cpu==0 else B+SYMS['DegasPrimeApEntry'])
  def make_hook(cpu):
   def hook(uc,pc,size,_):
    if pc==TARGET:
     self.assertEqual(states[1-cpu],'OFF','two live boot processors');booted.append(cpu)
     self.assertEqual([uc.reg_read(r) for r in [UC_ARM64_REG_X0,UC_ARM64_REG_X1,UC_ARM64_REG_X2,UC_ARM64_REG_X3]],args)
     self.assertEqual(uc.reg_read(UC_ARM64_REG_SP),sp);self.assertEqual(daif[cpu],0x280);parked[cpu]=True;uc.emu_stop();return
    op=struct.unpack('<I',uc.mem_read(pc,4))[0]
    if op in [0xd53800a1,0xd5381001,0xd5384241,0xd53b4225]:
     value=([bsp_mpidr,ap_mpidr][cpu] if op==0xd53800a1 else [bsp_sctlr,ap_sctlr][cpu] if op==0xd5381001 else [bsp_el,ap_el][cpu] if op==0xd5384241 else daif[cpu])
     uc.reg_write(UC_ARM64_REG_X5 if op==0xd53b4225 else UC_ARM64_REG_X1,value);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
    if op in [0xd5034fdf,0xd51b4225]:
     daif[cpu]=0x3c0 if op==0xd5034fdf else uc.reg_read(UC_ARM64_REG_X5);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
    if op in [0xd2a02014,0xd2a00034]: # Shorten only bounded test poll counters.
     uc.reg_write(UC_ARM64_REG_X20,128);uc.reg_write(UC_ARM64_REG_PC,pc+4);return
    if op==0xd4000003:
     fid=uc.reg_read(UC_ARM64_REG_X0);a1=uc.reg_read(UC_ARM64_REG_X1);a2=uc.reg_read(UC_ARM64_REG_X2)
     if fid==0xc4000003:
      self.assertEqual(cpu,0);self.assertEqual(a1,0x700);self.assertEqual(a2,B+SYMS['DegasPrimeApEntry']);self.assertEqual(uc.reg_read(UC_ARM64_REG_X3),0);on_calls.append(fid)
      if on_result==0:states[1]='ON' if ap_launch else 'PENDING'
      elif ap_state_on_failure!=1:states[1]='ON';parked[1]=True # An unrelated already-running CPU.
      uc.reg_write(UC_ARM64_REG_X0,on_result&0xffffffffffffffff)
     elif fid==0xc4000004:
      self.assertEqual(a2,0);target=0 if a1==0 else 1 if a1==0x700 else -1;self.assertNotEqual(target,-1);aff_calls.append((cpu,target));uc.reg_write(UC_ARM64_REG_X0,1 if states[target]=='OFF' else 0)
     elif fid==0x84000002:
      if off_denied and cpu==0:uc.reg_write(UC_ARM64_REG_X0,0xffffffffffffffff)
      else:states[cpu]='OFF';parked[cpu]=True
     else:self.fail(f'unexpected SMC {fid:x}')
     uc.reg_write(UC_ARM64_REG_PC,pc+4);uc.emu_stop();return
    if op==0xd503205f:parked[cpu]=True;uc.emu_stop()
   return hook
  def make_write(cpu):
   def write(uc,access,pa,size,value,_):
    if REC<=pa and pa+size<=B+len(CODE):
     cpus[1-cpu].mem_write(pa,(int(value)&((1<<(size*8))-1)).to_bytes(size,'little'))
    elif WDT<=pa and pa+size<=WDT+12:wdt.append((cpu,pa,value))
    else:self.fail(f'unexpected write {pa:x}')
   return write
  for cpu,u in enumerate(cpus):u.hook_add(UC_HOOK_CODE,make_hook(cpu));u.hook_add(UC_HOOK_MEM_WRITE,make_write(cpu))
  for tick in range(5000):
   for cpu,u in enumerate(cpus):
    if states[cpu]=='ON' and not parked[cpu]:u.emu_start(u.reg_read(UC_ARM64_REG_PC),TARGET+0x20000,count=32)
   if all(parked[i] or states[i]!='ON' for i in range(2)):break
  else:self.fail('bounded protocol did not finish')
  safe_bsp_fallback=(bsp_mpidr&0xffffff)!=0 or bsp_mpidr>>32&255 or bsp_sctlr&0x1005 or bsp_sctlr&(1<<25) or bsp_el!=4
  ap_bad=ap_sctlr&0x1005 or ap_sctlr&(1<<25) or ap_el!=4 or ap_mpidr&0xffffff!=0x700
  if safe_bsp_fallback:self.assertEqual(booted,[0]);self.assertEqual(on_calls,[])
  elif on_result!=0 and ap_state_on_failure==1:self.assertEqual(booted,[0]);self.assertEqual(len(on_calls),1)
  elif on_result==0 and ap_launch and not off_denied and not ap_bad:self.assertEqual(booted,[1]);self.assertEqual(states[0],'OFF');self.assertEqual(wdt,[])
  elif ap_bad:self.assertTrue(booted==[0] or not booted)
  else:self.assertEqual(booted,[]);self.assertTrue(wdt,'ambiguous ownership did not quarantine with watchdog')
  return booted
 def test_prime_handoff_and_lk_abi(self):self.run_case()
 def test_cpu_on_failure_only_falls_back_when_off(self):
  self.run_case(on_result=-1);self.run_case(on_result=-4,ap_state_on_failure=0)
 def test_failed_boot_core_shutdown_and_missing_ap_quarantine(self):
  self.run_case(off_denied=True);self.run_case(ap_launch=False)
 def test_bsp_guards(self):
  for kwargs in [dict(bsp_el=8),dict(bsp_sctlr=1),dict(bsp_sctlr=4),dict(bsp_sctlr=0x1000),dict(bsp_sctlr=1<<25),dict(bsp_mpidr=0x81000100)]:self.run_case(**kwargs)
 def test_ap_guards(self):
  for kwargs in [dict(ap_el=8),dict(ap_sctlr=1),dict(ap_sctlr=4),dict(ap_sctlr=0x1000),dict(ap_sctlr=1<<25),dict(ap_mpidr=0x81000600)]:self.run_case(**kwargs)
if __name__=='__main__':unittest.main()
