"""Exact loaded-module pin and host execution of its read-only .get path.

Only host RAM is mapped. No probe is installed and no phone command exists.
"""
from pathlib import Path
import hashlib
import json
import re
import struct
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / 'tools/vendor/python'),
                str(ROOT / 'artifacts/degas/cpu-audit/test-deps')]
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from elftools.elf.elffile import ELFFile
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *

MODULE = ROOT / 'artifacts/degas/dvfs-active-modules/ramdisk00/mediatek-cpufreq-hw.ko'
LOADED_NOTE = ROOT / 'artifacts/degas/dvfs-active-modules/loaded-cpufreq.note.gnu.build-id'
SHA = 'f12addd9a3b5c677e8fec7b11903561e2d4073dff831a5ce4bd4dc7057df51fc'
EXPECTED_NOTE = bytes.fromhex('040000001400000003000000474e5500068e6f5000761002c3541edbe177a210f2299352')
TEXT, BSS, RAM, MMIO = 0x10000000, 0x10010000, 0x20000000, 0x21000000
GET = 0xc04
CAPTURE = ROOT / 'evidence/raw/2026-10-29-degas-dvfs-requests-v1/capture.txt'


def decode_capture():
    text = CAPTURE.read_text()
    lines = text.splitlines()
    rounds, entries, observations = [], {}, []
    for i, line in enumerate(lines):
        match = re.fullmatch(r'ROUND (\d+) POLICY (\d+)', line)
        if match:
            rounds.append(dict(round=int(match[1]), cpu=int(match[2]),
                               cpuinfo_cur_khz=int(lines[i + 1]),
                               scaling_cur_khz=int(lines[i + 2]),
                               available_khz=[int(v) for v in lines[i + 3].split()]))
        if 'entry: (' in line and 'mtk_cpufreq_hw_get+0x24/' in line:
            pid = int(re.search(r'cat-(\d+)', line)[1])
            fields = dict(re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', line))
            entries[pid] = {key: int(value, 0) for key, value in fields.items()}
        elif 'get: (' in line and 'mtk_cpufreq_hw_get+0x30/' in line:
            pid = int(re.search(r'cat-(\d+)', line)[1])
            fields = {key: int(value, 0) for key, value in
                      re.findall(r'(\w+)=(0x[0-9a-f]+|\d+)', line)}
            pre = entries.pop(pid)
            assert pre['domain'] == fields['domain']
            fields.update(pre)
            observations.append(fields)
    assert not entries and len(observations) == len(rounds) == 12
    by_cpu = {}
    for sample, observation in zip(rounds, observations):
        assert sample['cpu'] == observation['cpu']
        assert sample['cpuinfo_cur_khz'] == observation['raw']
        assert observation['mode_flag'] == 1 and observation['fdvfs_flag'] == 0
        assert len(sample['available_khz']) == observation['nr_opp']
        cpu = sample['cpu']
        expected_mask = {0: 0xf, 4: 0x70, 7: 0x80}[cpu]
        assert observation['cpus'] == expected_mask
        domain = by_cpu.setdefault(str(cpu), dict(mode_flag=1, fdvfs_flag=0,
              cpumask=hex(expected_mask), nr_opp=observation['nr_opp'],
              available_khz=sample['available_khz'], samples=[]))
        assert domain['available_khz'] == sample['available_khz']
        domain['samples'].append(dict(round=sample['round'], raw_khz=observation['raw'],
             scaling_cur_khz=sample['scaling_cur_khz'],
             raw_is_lut_entry=observation['raw'] in sample['available_khz']))
    return dict(capture=str(CAPTURE), capture_sha256=hashlib.sha256(CAPTURE.read_bytes()).hexdigest(),
                paired_reads=len(observations), domains=by_cpu,
                conclusion='All12existingMMIOreads agree with corresponding cpuinfo_cur values. '
                           'All domains use kHz direct mode with FDVFS off. CPU4 readback includes '
                           'intermediate values outside exported OPP entries. Physical clocks not measured.')


def source():
    raw = MODULE.read_bytes()
    assert hashlib.sha256(raw).hexdigest() == SHA
    assert LOADED_NOTE.read_bytes() == EXPECTED_NOTE
    with MODULE.open('rb') as f:
        elf = ELFFile(f)
        assert elf['e_machine'] == 'EM_AARCH64'
        assert elf.get_section_by_name('.note.gnu.build-id').data() == EXPECTED_NOTE
        text = elf.get_section_by_name('.text').data()
        offsets = elf.get_section_by_name('.rodata').data()[0x190:0x19c]
        symbols = {s.name: dict(value=s['st_value'], size=s['st_size'], section=s['st_shndx'])
                   for s in elf.get_section_by_name('.symtab').iter_symbols() if s.name}
        assert symbols['mtk_cpufreq_hw_get']['value'] == GET
        assert symbols['mtk_cpufreq_hw_get']['size'] == 0x88
        assert symbols['freq_scaling_disabled']['value'] == 0x118
        assert symbols['fdvfs_enabled']['value'] == 0x11c
        assert struct.unpack('<6H', offsets) == (0, 0x84, 0x88, 0x8c, 0x90, 0x114)
        assert text[0xc28:0xc2c] == bytes.fromhex('800e40f9')
        assert text[0xc34:0xc38] == bytes.fromhex('880240f9')
        assert text[0x84c:0x850] == bytes.fromhex('750240b9')
        return text, symbols


def run_get(text, cpu, raw, frequencies, mode, fdvfs):
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(TEXT, 0x10000)
    u.mem_map(BSS, 0x10000)
    u.mem_map(RAM, 0x10000)
    u.mem_map(MMIO, 0x1000)
    code = bytearray(text)
    # Relocate ONLY the two .get instructions naming .bss+8. Logging argument
    # relocations are immaterial because external trace callbacks are modeled.
    delta_pages = (BSS - ((TEXT + 0xc1c) & ~0xfff)) >> 12
    adrp_x8 = 0x90000008 | ((delta_pages & 3) << 29) | (((delta_pages >> 2) & 0x7ffff) << 5)
    struct.pack_into('<II', code, 0xc1c, adrp_x8, 0x91002108)
    u.mem_write(TEXT, bytes(code))
    domain, table = RAM + 0x1000, RAM + 0x2000
    u.mem_write(BSS + 8 + cpu * 8, struct.pack('<Q', domain))
    u.mem_write(BSS + 0x118, bytes([mode]))
    u.mem_write(BSS + 0x11c, bytes([fdvfs]))
    u.mem_write(domain, struct.pack('<Q', table))
    for offset, value in [(8, MMIO), (0x10, MMIO + 0x84),
                           (0x18, MMIO + 0x88), (0x20, MMIO + 0x8c),
                           (0x28, MMIO + 0x90), (0x30, MMIO + 0x114)]:
        u.mem_write(domain + offset, struct.pack('<Q', value))
    u.mem_write(domain + 0x38, struct.pack('<I', len(frequencies)))
    for i, frequency in enumerate(frequencies):
        u.mem_write(table + 12 * i + 8, struct.pack('<I', frequency))
    u.mem_write(MMIO + 0x88, struct.pack('<I', raw))
    u.reg_write(UC_ARM64_REG_SP, RAM + 0xf000)
    u.reg_write(UC_ARM64_REG_X0, cpu)
    end = TEXT + 0xf000
    u.reg_write(UC_ARM64_REG_X30, end)
    observations, reads, writes = {}, [], []

    def instruction(_, pc, size, data):
        if pc in [TEXT + 0x848, TEXT + 0x868]:  # external read tracing only
            # These are unresolved BL callsites, not external callee entries.
            u.reg_write(UC_ARM64_REG_X30, pc + 4)
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif pc == TEXT + 0xc28:
            assert u.reg_read(UC_ARM64_REG_X0) == cpu
            assert u.reg_read(UC_ARM64_REG_X20) == domain
            map_address = u.reg_read(UC_ARM64_REG_X8)
            assert map_address == BSS + 8
            observations['pre'] = dict(cpu=cpu, mode_flag=u.mem_read(map_address + 272, 1)[0],
                                       fdvfs_flag=u.mem_read(map_address + 276, 1)[0])
        elif pc == TEXT + 0xc34:
            observations['post'] = dict(raw=u.reg_read(UC_ARM64_REG_X0),
                                        request_pointer=struct.unpack('<Q', u.mem_read(domain + 24, 8))[0])

    def memory(_, access, address, size, value, data):
        if MMIO <= address < MMIO + 0x1000:
            (reads if access == 16 else writes).append(dict(address=hex(address), size=size))

    u.hook_add(UC_HOOK_CODE, instruction)
    u.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, memory)
    u.emu_start(TEXT + GET, end, count=300)
    assert u.reg_read(UC_ARM64_REG_PC) == end
    assert reads == [dict(address=hex(MMIO + 0x88), size=4)] and not writes
    assert observations['pre'] == dict(cpu=cpu, mode_flag=mode, fdvfs_flag=fdvfs)
    assert observations['post']['raw'] == raw
    result = u.reg_read(UC_ARM64_REG_X0) & 0xffffffff
    expected = frequencies[raw] if raw <= 31 else (raw if frequencies[-1] <= raw <= frequencies[0] else 0xffffffff)
    assert result == expected
    return dict(cpu=cpu, raw_word=raw, returned_khz=result, mode_flag=mode,
                fdvfs_flag=fdvfs, hardware_model_reads=reads, hardware_model_writes=writes,
                probe_snapshots=observations)


if __name__ == '__main__':
    text, symbols = source()
    little = [2200000, 2100000, 2000000, 1900000, 1800000, 1700000, 1600000,
              1500000, 1400000, 1300000, 1200000, 1100000, 1000000, 900000,
              800000, 700000, 600000, 480000]
    capture = decode_capture()
    cases = [run_get(text, cpu, raw, little, mode, fdvfs)
             for cpu, raw, mode, fdvfs in [(0, 8, 0, 0), (0, 1400000, 1, 0),
                                           (4, 8, 1, 1), (7, 1300000, 0, 0),
                                           (7, 5000000, 1, 0)]]
    for value in [2446000, 1250000, 1328000, 1302000]:
        cases.append(run_get(text, 4, value, capture['domains']['4']['available_khz'], 1, 0))
    selected = ['mtk_cpufreq_hw_get', 'mtk_cpufreq_hw_target_index',
                'mtk_cpufreq_hw_fast_switch', 'mtk_cpufreq_hw_cpu_init',
                'mtk_cpufreq_hw_driver_probe', 'readl_relaxed']
    report = dict(module_sha256=SHA, loaded_note_identical=True,
                  gnu_build_id=EXPECTED_NOTE[16:].hex(),
                  function_extents={name: symbols[name] for name in selected},
                  get_pre_probe=dict(symbol='mediatek_cpufreq_hw:mtk_cpufreq_hw_get+0x24',
                    text_offset='0xc28', instruction='LDR X0,[X20,#0x18]',
                    fetch='cpu=%x0:u32 map=%x8:x64 mode_flag=+272(%x8):u8 fdvfs_flag=+276(%x8):u8'),
                  get_post_probe=dict(symbol='mediatek_cpufreq_hw:mtk_cpufreq_hw_get+0x30',
                    text_offset='0xc34', instruction='LDR X8,[X20]',
                    fetch='raw=%x0:x32 domain=%x20:x64 req_va=+24(%x20):x64 lut_va=+8(%x20):x64 nr_opp=+56(%x20):u32 cpus=+64(%x20):x64'),
                  mode_semantics='Compiled .bss+118:1 selects kHz,0 selects index; .bss+11c:1 selects FDVFS.',
                  captured_android=capture,
                  cases=cases, tests_passed=len(cases),
                  scope='Exact .get instruction execution in Unicorn with two .bss relocations applied, '
                        'external read-logging callbacks modeled, MMIO is host RAM only. '
                        'Pre/post probe expressions qualified; no phone/probe/control operation.')
    assert hashlib.sha256(MODULE.read_bytes()).hexdigest() == SHA
    Path(__file__).with_name('active-cpufreq-audit.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({k:v for k,v in report.items() if k not in ['cases', 'function_extents']}, indent=2))
