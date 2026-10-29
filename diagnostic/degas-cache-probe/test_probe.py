"""Execute actual assembled probe control flow; ID/SMC results are modeled.

All cache descriptions in these tests are SYNTHETIC, not MT6897 measurements.
No clocks, MMIO, phone commands or firmware modifications occur.
"""
from pathlib import Path
import ctypes
import hashlib
import json
import struct
import subprocess
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'artifacts/degas/cpu-audit/test-deps'))
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE, UC_HOOK_MEM_READ
from unicorn.arm64_const import *
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from decode import *

META = json.loads((HERE / 'generated/cache-probe.json').read_text())
CODE = (HERE / 'generated/cache-probe.bin').read_bytes()
assert hashlib.sha256((HERE / 'CacheProbe.S').read_bytes()).hexdigest() == META['source_sha256']
assert hashlib.sha256(CODE).hexdigest() == META['code_sha256']
INSNS = {i.address: i for i in Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN).disasm(CODE, 0)
         if i.address < META['symbols']['DegasCacheApMailbox']}
RESULTS = []


def encoded(ways, sets, line, ccidx=0):
    log = line.bit_length() - 1
    assert line == 1 << log and 4 <= log <= 11
    return ((ways - 1) << 3) | ((sets - 1) << (32 if ccidx else 13)) | (log - 4)


def run(cpu=1, *, wrapper=False, clidr=None, ccidx=0, csselr=0x10,
        daif=0x3c0, el=4, sctlr=None, wrong_cpu=None, initial_x0=0,
        off_returns=False, base=0x43000000, trap=None, mutate=None, pointer=None):
    code_pa, record_pa = base, base + 0x10000
    record = make_record(cpu, 0x1234567800000001, mode=int(wrapper))
    if mutate:
        mutate(record)
    clidr = (3 | (4 << 3) | (4 << 6) | (3 << 24) | (2 << 21) | (1 << 27)) if clidr is None else clidr
    try:
        selected = selectors(clidr)
    except ValueError:
        selected = []
        for level in range(7):
            kind = (clidr >> (3 * level)) & 7
            if kind == 0 or kind > 4:
                break
            for ind in ((0, 1) if kind == 3 else (1,) if kind == 1 else (0,)):
                selected.append(dict(selector=level * 2 + ind))
    geometry = {row['selector']: encoded(3 + row['selector'], 127 + row['selector'],
                                        64 if row['selector'] % 2 == 0 else 128, int(ccidx == 1))
                for row in selected}
    image = bytearray(CODE)
    struct.pack_into('<Q', image, META['symbols']['DegasCacheApMailbox'], record_pa if pointer is None else pointer)
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(code_pa, 4096)
    u.mem_map(record_pa, 4096)
    u.mem_write(code_pa, bytes(image))
    u.mem_write(record_pa, bytes(record) + b'\xa5' * (4096 - RECORD_BYTES))
    for n in range(31):
        u.reg_write(globals()['UC_ARM64_REG_X' + str(n)], 0x1234000000000000 + n)
    for n in range(32):
        u.reg_write(globals()['UC_ARM64_REG_Q' + str(n)], (0x123456789 << 64) | n)
    return_pc = code_pa + 0xf00
    u.reg_write(UC_ARM64_REG_LR, return_pc)
    u.reg_write(UC_ARM64_REG_SP, 0x1234567890)
    u.reg_write(UC_ARM64_REG_X0, initial_x0 if wrapper else record_pa if pointer is None else pointer)
    nonvolatile = [u.reg_read(globals()['UC_ARM64_REG_X' + str(n)]) for n in range(18, 30)]
    simd = [u.reg_read(globals()['UC_ARM64_REG_Q' + str(n)]) for n in range(32)]
    state = dict(daif=daif, csselr_el1=csselr, currentel=el,
                 mpidr_el1=0x81000000 | ((cpu if wrong_cpu is None else wrong_cpu) << 8),
                 midr_el1=0x410f0000 | cpu, sctlr_el1=(0 if wrapper else 0x1005) if sctlr is None else sctlr,
                 clidr_el1=clidr, id_aa64mmfr2_el1=ccidx << 20)
    writes, reads, events, smcs, outcome, pending_isb = [], [], [], [], 'bound_exhausted', False

    def q(offset):
        return struct.unpack('<Q', u.mem_read(record_pa + offset, 8))[0]

    def event(name, value=None):
        nonlocal outcome
        item = name if value is None else (name, value)
        events.append(item)
        if trap == item:
            outcome = 'modeled_trap'
            u.emu_stop()
            return True
        return False

    def hook(_, pc, size, data):
        nonlocal outcome, pending_isb
        if pc == return_pc:
            outcome = 'returned'
            u.emu_stop()
            return
        ins = INSNS[pc - code_pa]
        if ins.mnemonic == 'mrs':
            reg, name = ins.op_str.split(', ')
            if name == 'ccsidr_el1':
                assert not pending_isb, 'selected CCSIDR read before ISB'
                assert q(64) == (state['csselr_el1'] << 8) | 0x21
                if event('read_ccsidr', state['csselr_el1']): return
                value = geometry[state['csselr_el1']]
            else:
                if event('read_' + name): return
                value = state[name]
            u.reg_write(globals()['UC_ARM64_REG_' + reg.upper()], value)
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif ins.mnemonic == 'msr':
            name, reg = ins.op_str.split(', ')
            if name == 'daifset':
                assert reg == '#0xf'
                state['daif'] |= 0x3c0
                events.append('mask_daif')
            else:
                value = u.reg_read(globals()['UC_ARM64_REG_' + reg.upper()])
                if name == 'csselr_el1':
                    assert q(64) == (0x30 if value == csselr and q(64) == 0x30 else (value << 8) | 0x20)
                    if event('write_csselr', value): return
                    pending_isb = True
                elif name == 'daif':
                    assert q(64) == 0x32
                    if event('restore_daif'): return
                else:
                    raise AssertionError('unwanted architectural mutation: ' + name)
                state[name] = value
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif ins.mnemonic == 'isb':
            pending_isb = False
        elif ins.mnemonic == 'smc':
            assert q(64) == 0x40
            fid = u.reg_read(UC_ARM64_REG_X0)
            assert fid == 0x84000002
            smcs.append(fid)
            if off_returns:
                u.reg_write(UC_ARM64_REG_X0, MASK64)
                u.reg_write(UC_ARM64_REG_PC, pc + 4)
            else:
                outcome = 'modeled_cpu_off'
                u.emu_stop()
        elif ins.mnemonic == 'wfe':
            outcome = 'parked'
            u.emu_stop()

    def write(_, access, address, size, value, data):
        assert record_pa <= address and address + size <= record_pa + RECORD_BYTES, 'out-of-record store'
        writes.append((address - record_pa, size, value))

    def read(_, access, address, size, value, data):
        assert (record_pa <= address and address + size <= record_pa + RECORD_BYTES) or (
            code_pa <= address and address + size <= code_pa + len(CODE)), 'unowned memory/MMIO read'
        reads.append(address)

    u.hook_add(UC_HOOK_CODE, hook)
    u.hook_add(UC_HOOK_MEM_WRITE, write)
    u.hook_add(UC_HOOK_MEM_READ, read)
    start = code_pa + (META['symbols']['DegasCacheApStart'] if wrapper else META['symbols']['DegasCacheCollect'])
    u.emu_start(start, code_pa + 4096, count=3000)
    assert outcome != 'bound_exhausted'
    assert u.reg_read(UC_ARM64_REG_SP) == 0x1234567890
    assert nonvolatile == [u.reg_read(globals()['UC_ARM64_REG_X' + str(n)]) for n in range(18, 30)]
    assert simd == [u.reg_read(globals()['UC_ARM64_REG_Q' + str(n)]) for n in range(32)]
    assert bytes(u.mem_read(code_pa, len(image))) == bytes(image)
    assert bytes(u.mem_read(record_pa, 64)) == bytes(record[:64])
    assert bytes(u.mem_read(record_pa + RECORD_BYTES, 4096 - RECORD_BYTES)) == b'\xa5' * (4096 - RECORD_BYTES)
    if outcome == 'returned':
        assert state['daif'] == daif and state['csselr_el1'] == csselr
        assert u.reg_read(UC_ARM64_REG_LR) == return_pc
    observed = bytes(u.mem_read(record_pa, RECORD_BYTES))
    result = dict(cpu=cpu, wrapper=wrapper, outcome=outcome, raw=observed,
                  status=q(72), phase=q(64), commit=q(168), smcs=smcs, events=events,
                  state=state, x0=u.reg_read(UC_ARM64_REG_X0), writes=writes)
    RESULTS.append({k: v for k, v in result.items() if k not in ('raw', 'writes', 'events', 'state')})
    return result


class ProbeInstructions(unittest.TestCase):
    def test_all_eight_cpus_legacy_and_extended(self):
        for cpu in range(8):
            for ccidx in (0, 1):
                for high in (False, True):
                    r = run(cpu, wrapper=cpu != 0, ccidx=ccidx,
                            base=0x300000000 if high else 0x43000000)
                    decoded = decode_record(r['raw'], expected_epoch=0x1234567800000001, expected_cpu=cpu)
                    self.assertEqual((r['status'], decoded['geometry_complete']), (0, True))
                    self.assertEqual(decoded['clidr_fields']['loc'], 3)
                    self.assertEqual([c['selector'] for c in decoded['caches']], [0, 1, 2, 4])
                    self.assertEqual(decoded['caches'][0]['line_bytes'], 64)
                    self.assertEqual(decoded['caches'][1]['line_bytes'], 128)
                    self.assertNotEqual(decoded['caches'][0]['maintenance_span_bytes'], decoded['caches'][1]['maintenance_span_bytes'])
                    self.assertEqual(r['outcome'], 'modeled_cpu_off' if cpu else 'returned')
                    self.assertEqual(r['smcs'], [0x84000002] if cpu else [])

    def test_every_cache_type_maximum_entries_and_first_none_stop(self):
        for types, expected in [([0, 7, 7], []), ([1], [1]), ([2], [0]),
                                ([3], [0, 1]), ([4], [0]), ([3] * 7, list(range(14)))]:
            clidr = sum(kind << (i * 3) for i, kind in enumerate(types))
            r = run(0, clidr=clidr)
            d = decode_record(r['raw'])
            self.assertEqual([c['selector'] for c in d['caches']], expected)
            for selector in expected:
                self.assertIn(('read_ccsidr', selector), r['events'])
            self.assertEqual(r['state']['csselr_el1'], 0x10)

    def test_errors_restore_daif_and_observed_csselr(self):
        cases = [(dict(el=8), 4), (dict(el=12), 4),
                 (dict(wrong_cpu=2), 3), (dict(sctlr=1 << 25), 5),
                 (dict(ccidx=2), 6), (dict(ccidx=15), 6),
                 (dict(clidr=5), 7), (dict(clidr=3 | (6 << 3)), 7),
                 (dict(mutate=lambda r: struct.pack_into('<Q', r, 0, 0)), 2),
                 (dict(mutate=lambda r: struct.pack_into('<Q', r, 8, 4096)), 2),
                 (dict(mutate=lambda r: struct.pack_into('<Q', r, 16, 0)), 2),
                 (dict(mutate=lambda r: struct.pack_into('<Q', r, 24, 1)), 2)]
        for arguments, status in cases:
            r = run(1, daif=0, csselr=0x14, **arguments)
            self.assertEqual((r['outcome'], r['status'], r['x0'], r['commit']),
                             ('returned', status, status, COMMIT))
            self.assertEqual((r['state']['daif'], r['state']['csselr_el1']), (0, 0x14))
        for bits in (1, 4, 0x1000, 0x1005):
            r = run(1, wrapper=True, sctlr=bits)
            self.assertEqual((r['status'], r['outcome']), (5, 'modeled_cpu_off'))
            self.assertFalse(any(isinstance(e, tuple) and e[0] == 'write_csselr' for e in r['events']))

    def test_ap_ignores_lost_high_or_unrelated_x0_and_cpu0_never_off(self):
        for incoming in (0, 0x1122334455667788, MASK64):
            r = run(7, wrapper=True, initial_x0=incoming)
            self.assertEqual(decode_record(r['raw'])['incoming_x0'], hex(incoming))
            self.assertEqual((r['status'], len(r['smcs'])), (0, 1))
        r = run(0, wrapper=True)
        self.assertEqual((r['outcome'], r['status'], r['smcs'], r['phase']), ('parked', 10, [], 0x42))

    def test_cpu_off_return_and_invalid_record_pointer(self):
        r = run(1, wrapper=True, off_returns=True)
        d = decode_record(r['raw'])
        self.assertEqual((r['status'], r['phase'], d['cpu_off_return']), (9, 0x41, hex(MASK64)))
        for wrapper in (False, True):
            for pointer in (0, 1):
                r = run(1, wrapper=wrapper, pointer=pointer)
                self.assertFalse(r['writes'])
                self.assertFalse(r['smcs'])

    def test_traps_leave_precise_phase_no_false_completion_and_deadline(self):
        cases = [('read_clidr_el1', 8), ('read_csselr_el1', 9),
                 (('write_csselr', 0), 0x20), (('read_ccsidr', 1), 0x121),
                 (('write_csselr', 0x10), 0x30), ('restore_daif', 0x32)]
        for trap, phase in cases:
            r = run(1, wrapper=True, trap=trap)
            self.assertEqual((r['outcome'], r['phase'], r['commit'], r['smcs']),
                             ('modeled_trap', phase, 0, []))
            before = monitor(r['raw'], expected_epoch=0x1234567800000001, expected_cpu=1,
                             elapsed_ns=499_999_999)
            after = monitor(r['raw'], expected_epoch=0x1234567800000001, expected_cpu=1,
                            elapsed_ns=500_000_000)
            self.assertEqual((before['state'], after['state']), ('pending', 'timeout_requires_reset'))

    def test_no_unwanted_architectural_or_mmio_operations(self):
        for instruction in INSNS.values():
            self.assertNotIn(instruction.mnemonic, ('at', 'dc', 'ic', 'tlbi', 'hvc'))
            if instruction.mnemonic == 'msr':
                self.assertIn(instruction.op_str.split(',')[0], ('daif', 'daifset', 'csselr_el1'))


class DecoderTests(unittest.TestCase):
    def test_known_legacy_geometry_unknown_policy_and_nonpowers(self):
        # Arm A55 TRM example is a decoder fixture, NOT a device measurement.
        d = decode_ccsidr(0x700fe01a, 0)
        self.assertEqual((d['line_bytes'], d['ways'], d['sets'], d['maintenance_span_bytes']),
                         (64, 4, 128, 32768))
        d = decode_ccsidr(encoded(3, 127, 2048) | 0xf0000000, 0)
        self.assertEqual((d['ways'], d['sets'], d['line_bytes']), (3, 127, 2048))
        self.assertEqual(d['legacy_unknown_bits31_28'], 15)
        self.assertIsNone(d['physical_cache_capacity_bytes'])
        self.assertIsNone(d['shared_cpu_ids'])

    def test_extended_fields_maximum_and_invalid_encodings(self):
        d = decode_ccsidr(encoded(65537, 1234567, 128, 1), 1)
        self.assertEqual((d['ways'], d['sets'], d['line_bytes']), (65537, 1234567, 128))
        for ccidx, ways, sets in ((0, 1 << 10, 1 << 15), (1, 1 << 21, 1 << 24)):
            d = decode_ccsidr(encoded(ways, sets, 2048, ccidx), ccidx)
            self.assertEqual(d['maintenance_span_bytes'], ways * sets * 2048)
        for raw, ccidx in ((1 << 32, 0), (1 << 24, 1), (1 << 56, 1), (0, 2), (0, 15)):
            with self.assertRaises(ValueError): decode_ccsidr(raw, ccidx)

    def test_record_identity_count_selector_and_no_false_off_proof(self):
        r = run(1, wrapper=True)
        with self.assertRaises(ValueError): decode_record(r['raw'], expected_epoch=9)
        with self.assertRaises(ValueError): decode_record(r['raw'], expected_cpu=7)
        for offset, value in ((144, 15), (144, 0), (256, 1)):
            bad = bytearray(r['raw'])
            struct.pack_into('<Q', bad, offset, value)
            with self.assertRaises(ValueError): decode_record(bad)
        pending = monitor(r['raw'], expected_epoch=0x1234567800000001, expected_cpu=1, elapsed_ns=1)
        done = monitor(r['raw'], expected_epoch=0x1234567800000001, expected_cpu=1,
                       elapsed_ns=1, ap_off_observed=True)
        self.assertEqual((pending['state'], done['state']), ('pending', 'complete_ap_off'))
        self.assertFalse(pending['observation']['cpu_off_proved'])
        self.assertTrue(done['observation']['cpu_off_proved'])

    def test_abi_header_layout_matches_raw_record(self):
        class Entry(ctypes.Structure):
            _fields_ = [('selector', ctypes.c_uint64), ('raw', ctypes.c_uint64)]
        class Record(ctypes.Structure):
            _fields_ = [('head', ctypes.c_uint64 * 32), ('entries', Entry * 14), ('tail', ctypes.c_ubyte * 32)]
        self.assertEqual((ctypes.sizeof(Record), Record.entries.offset), (512, 256))
        header = (HERE / 'ProbeAbi.h').read_text()
        self.assertIn(hex(MAGIC)[2:].upper(), header.upper())
        self.assertIn(hex(COMMIT)[2:].upper(), header.upper())


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.wasSuccessful():
        report = dict(status='PASS_HOST_ONLY_SYNTHETIC', tests=result.testsRun,
                      instruction_cases=len(RESULTS), code_bytes=META['code_bytes'],
                      code_sha256=META['code_sha256'], source_sha256=META['source_sha256'],
                      scope='Actual assembled control flow; privileged registers, traps, and CPU_OFF are modeled. No actual MT6897 geometry/coherency/power measurements.',
                      cases=RESULTS)
        (HERE / 'generated/test-report.json').write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(not result.wasSuccessful())
