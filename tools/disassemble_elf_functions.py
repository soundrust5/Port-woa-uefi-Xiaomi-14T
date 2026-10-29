"""Disassemble exact ELF symbol extents with relocation/source-string evidence."""
import argparse
import hashlib
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).parent/'vendor/python'))
from capstone import Cs,CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN
from elftools.elf.elffile import ELFFile
from elftools.elf.relocation import RelocationSection


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('input',type=Path)
    ap.add_argument('symbols',nargs='+')
    ap.add_argument('--output',type=Path,required=True)
    args=ap.parse_args();lines=[str(args.input.resolve()),'SHA256 '+hashlib.sha256(args.input.read_bytes()).hexdigest()]
    with args.input.open('rb') as f:
        elf=ELFFile(f)
        if elf['e_machine']!='EM_AARCH64':raise ValueError('Expected AArch64 ELF')
        syms=list(elf.get_section_by_name('.symtab').iter_symbols())
        reloc={}
        for section in elf.iter_sections():
            if not isinstance(section,RelocationSection):continue
            linked=elf.get_section(section['sh_link'])
            for r in section.iter_relocations():
                symbol=linked.get_symbol(r['r_info_sym']);add=r.entry.get('r_addend',0)
                name=symbol.name or (elf.get_section(symbol['st_shndx']).name if isinstance(symbol['st_shndx'],int) else str(symbol['st_shndx']))
                text=f"reloc{r['r_info_type']} {name}{add:+#x}"
                if isinstance(symbol['st_shndx'],int):
                    target=elf.get_section(symbol['st_shndx']);offset=symbol['st_value']+add-target['sh_addr']
                    if target.name.startswith('.rodata.str') and 0<=offset<target['sh_size']:
                        raw=target.data()[offset:].split(b'\0',1)[0][:200]
                        text+=' '+repr(raw.decode('ascii',errors='replace'))
                reloc.setdefault((section['sh_info'],r['r_offset']),[]).append(text)
        md=Cs(CS_ARCH_ARM64,CS_MODE_LITTLE_ENDIAN)
        for name in args.symbols:
            candidates=[s for s in syms if s.name==name and s['st_info']['type']=='STT_FUNC' and isinstance(s['st_shndx'],int)]
            if len(candidates)!=1:raise ValueError(f'Expected one function {name}, got {len(candidates)}')
            symbol=candidates[0];section=elf.get_section(symbol['st_shndx']);start=symbol['st_value'];size=symbol['st_size'];offset=start-section['sh_addr']
            code=section.data()[offset:offset+size]
            if len(code)!=size:raise ValueError('Function exceeds section')
            lines+=['',f'{name} {section.name}+{start:#x} size={size}']
            consumed=0
            for ins in md.disasm(code,start):
                consumed+=ins.size
                lines.append(f'{ins.address:08x} {ins.bytes.hex()} {ins.mnemonic:8} {ins.op_str}')
                lines.extend('    '+r for r in reloc.get((symbol['st_shndx'],ins.address),[]))
            if consumed!=size:lines.append(f'UNDECODED: {size-consumed} bytes; do not infer completeness')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text('\n'.join(lines)+'\n',encoding='utf-8')
    print(f'Disassembled {len(args.symbols)} functions to {args.output}')


if __name__=='__main__':main()
