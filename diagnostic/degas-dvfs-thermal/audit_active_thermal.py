from pathlib import Path
import hashlib, json, sys
ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT/'tools/vendor/python'), str(ROOT/'artifacts/degas/cpu-audit/test-deps')]
from elftools.elf.elffile import ELFFile
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
HERE = Path(__file__).resolve().parent
CAP = ROOT/'evidence/raw/2026-10-29-degas-dvfs-thermal-android-v1'
path = CAP/'thermal_interface.ko'
sha = hashlib.sha256(path.read_bytes()).hexdigest()
assert sha == 'f2e0d8af09a5129afa22c7d727d2d4d68b9d4d638d03e374c2d776c2351b721c'
with path.open('rb') as f:
    elf = ELFFile(f)
    note = elf.get_section_by_name('.note.gnu.build-id').data()
    assert note == (CAP/'module.note').read_bytes()
    assert note[16:].hex() == '60a16f5fd2cc864a1f90a4550e3ff92f9b2021d0'
    symbols = {s.name:s for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
    names = ['get_cpu_temp','get_cpu_ceiling_freq','get_dsu_temp','get_dsu_ceiling_freq',
             'therm_intf_read_csram_s32','therm_intf_read_csram',
             'cpu_info_show','cpu_temp_show','is_cpu_limit_show','therm_intf_probe','readl']
    md = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    output=[]
    for name in names:
        if name not in symbols: continue
        sym = symbols[name]
        section = elf.get_section(sym['st_shndx'])
        output.append(f"\n{name} {section.name}+{sym['st_value']:x} size{sym['st_size']:x}")
        output += [f'{ins.address:08x}: {ins.bytes.hex()} {ins.mnemonic} {ins.op_str}'
                   for ins in md.disasm(section.data()[sym['st_value']:sym['st_value']+sym['st_size']],sym['st_value'])]
    for section in elf.iter_sections():
        if not hasattr(section,'iter_relocations'): continue
        symtab = elf.get_section(section['sh_link'])
        output.append('\n'+section.name)
        for reloc in section.iter_relocations():
            sym = symtab.get_symbol(reloc['r_info_sym'])
            output.append(f"{reloc['r_offset']:x}: {sym.name or sym['st_shndx']} + {reloc['r_addend']:x}")
    (HERE/'active-thermal.disasm.txt').write_text('\n'.join(output)+'\n')
    report = {'module_sha256':sha,'loaded_note_identical':True,'build_id':note[16:].hex(),
              'symbols':{n:{'offset':hex(symbols[n]['st_value']),'size':hex(symbols[n]['st_size']),
                           'section':elf.get_section(symbols[n]['st_shndx']).name}
                         for n in names+['tm_data','thermal_csram_base','thermal_cputcm_base','eas_previous_temp'] if n in symbols},
              'phone_accessed':False}
    (HERE/'active-thermal-identity.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))
