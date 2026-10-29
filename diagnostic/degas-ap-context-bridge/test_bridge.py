"""Execute assembled AP instructions in Unicorn; no device access.

System-register reads/writes are explicit fixture inputs. Unicorn is NOT a
cache, MMU, interconnect, power-state or GZ world-switch hardware emulator.
"""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import sys
import unittest

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
sys.path.insert(0, str(ROOT / 'artifacts/degas/cpu-audit/test-deps'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from bridge import *

META = json.loads((HERE / 'generated/ap-context.json').read_text())
CODE = (HERE / 'generated/ap-context.bin').read_bytes()
assert hashlib.sha256((HERE / 'ApContext.S').read_bytes()).hexdigest() == META['source_sha256']
assert hashlib.sha256(CODE).hexdigest() == META['binary_sha256']
DECODER = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
INSNS = {i.address: i for i in DECODER.disasm(CODE, 0)
         if i.address < META['symbols']['DegasApContextConfig']}
RESULTS = []


def execute(model, mpidr, *, table=None, config=None, sctlr=0, el=4,
            initial_x0=0, initial_daif=0x3c0, expected_entry=None):
    uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    image = bytearray(CODE)
    at = META['symbols']['DegasApContextConfig']
    image[at:at + 64] = model.config() if config is None else config
    uc.mem_map(model.code_pa, PAGE)
    uc.mem_map(model.table_pa, PAGE)
    uc.mem_write(model.code_pa, bytes(image))
    uc.mem_write(model.table_pa, bytes(model.table if table is None else table))
    entry = model.entry_low + 0x1000 if expected_entry is None else expected_entry
    if entry & -PAGE not in (model.code_pa, model.table_pa):
        uc.mem_map(entry & -PAGE, PAGE)
    for n in range(31):
        uc.reg_write(globals()['UC_ARM64_REG_X' + str(n)], 0xfeed000000000000 + n)
    uc.reg_write(UC_ARM64_REG_X0, initial_x0)
    uc.reg_write(UC_ARM64_REG_SP, 0x123456780)
    for n in range(32):
        uc.reg_write(globals()['UC_ARM64_REG_Q' + str(n)], (0x123456789abcdef << 64) | n)
    preserved = {n: uc.reg_read(globals()['UC_ARM64_REG_X' + str(n)]) for n in range(18, 31)}
    simd = [uc.reg_read(globals()['UC_ARM64_REG_Q' + str(n)]) for n in range(32)]
    state = dict(daif=initial_daif, sctlr_el1=sctlr, currentel=el, mpidr_el1=mpidr)
    writes, system, trace, outcome = [], [], [], 'instruction_bound'

    def read_register(name):
        return uc.reg_read(globals()['UC_ARM64_REG_' + name.upper()])

    def hook(_, pc, size, data):
        nonlocal outcome
        if pc == entry:
            outcome = 'dispatched'
            uc.emu_stop()
            return
        instruction = INSNS[pc - model.code_pa]
        trace.append(instruction.mnemonic)
        if instruction.mnemonic == 'mrs':
            reg, name = instruction.op_str.split(', ')
            uc.reg_write(globals()['UC_ARM64_REG_' + reg.upper()], state[name])
            uc.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif instruction.mnemonic == 'msr':
            name, reg = instruction.op_str.split(', ')
            system.append((name, reg))
            if name == 'daifset':
                state['daif'] |= 0x3c0
            elif name == 'daif':
                state['daif'] = read_register(reg)
            else:
                raise AssertionError('unexpected architectural state mutation: ' + name)
            uc.reg_write(UC_ARM64_REG_PC, pc + 4)
        elif instruction.mnemonic == 'wfe':
            outcome = 'parked'
            uc.emu_stop()

    def write(_, access, address, size, value, data):
        writes.append((address, size, value))

    uc.hook_add(UC_HOOK_CODE, hook)
    uc.hook_add(UC_HOOK_MEM_WRITE, write)
    uc.emu_start(model.code_pa, model.code_pa + PAGE, count=1000)
    assert outcome != 'instruction_bound', 'unbounded or escaped AP code'
    assert bytes(uc.mem_read(model.code_pa, len(image))) == bytes(image), 'code/config modified'
    assert uc.reg_read(UC_ARM64_REG_SP) == 0x123456780
    assert preserved == {n: uc.reg_read(globals()['UC_ARM64_REG_X' + str(n)]) for n in range(18, 31)}
    assert simd == [uc.reg_read(globals()['UC_ARM64_REG_Q' + str(n)]) for n in range(32)]
    if outcome == 'dispatched':
        cpu = cpu_index(mpidr)
        assert writes == [(model.table_pa + HEADER + cpu * STRIDE, 8, CLAIMED)]
        assert state['daif'] == initial_daif
        assert 'ldar' in trace and trace[-2:] == ['msr', 'br']
        assert trace.index('str') < trace.index('dsb') < len(trace) - 2
        assert system == [('daifset', '#0xf'), ('daif', 'x15')]
    else:
        assert not writes, 'rejected descriptor modified RAM'
        assert system == [('daifset', '#0xf')]
    result = dict(outcome=outcome, mpidr=hex(mpidr), x0=uc.reg_read(UC_ARM64_REG_X0),
                  pc=uc.reg_read(UC_ARM64_REG_PC), instructions=len(trace),
                  table=bytes(uc.mem_read(model.table_pa, PAGE)),
                  preserves_x18_x30_sp_simd=True, system_writes=system)
    RESULTS.append({k: v for k, v in result.items() if k != 'table'})
    return result


class ApInstructionTests(unittest.TestCase):
    def fixture(self, cpu=1, context=0, **kwargs):
        model = PublicationModel(**kwargs)
        model.publish(cpu << 8, model.entry_low + 0x1000, context)
        return model

    def test_full_context_all_aps_and_high_physical_placement(self):
        for high in (False, True):
            for cpu in range(1, 8):
                for context in (0, 1, 0x1122334455667788, MASK64):
                    with self.subTest(high=high, cpu=cpu, context=hex(context)):
                        addresses = dict(code_pa=0x300000000, table_pa=0x300010000,
                                         entry_low=0x310000000, entry_high=0x310010000) if high else {}
                        model = self.fixture(cpu, context, **addresses)
                        r = execute(model, (1 << 31) | (1 << 24) | cpu << 8)
                        self.assertEqual(r['outcome'], 'dispatched')
                        self.assertEqual(r['x0'], context)
                        self.assertEqual(r['pc'], model.entry_low + 0x1000)

    def test_incoming_context_is_never_trusted(self):
        model = self.fixture(7, 0x1234567800000000)
        for incoming in (0, 1, 0xffffffffffffffff):
            self.assertEqual(execute(model, 0x700, initial_x0=incoming)['x0'], 0x1234567800000000)

    def test_invalid_mpidr_rejected_without_memory_writes(self):
        model = self.fixture()
        for mpidr in (0, 1, 0x101, 0x800, 0x10000, 0x10100, 0x100000100, 0xff00000100):
            with self.subTest(mpidr=hex(mpidr)):
                self.assertEqual(execute(model, mpidr)['outcome'], 'parked')

    def test_stale_magic_epoch_state_and_entry_rejected(self):
        model = self.fixture()
        slot = HEADER + STRIDE
        faults = [(0, 0), (8, 2), (slot, EMPTY), (slot, RESERVED), (slot, CLAIMED),
                  (slot, CANCELLED), (slot + 8, 0x200), (slot + 16, 0),
                  (slot + 16, 2), (slot + 40, 0), (slot + 24, 0),
                  (slot + 24, model.entry_low - 4), (slot + 24, model.entry_high),
                  (slot + 24, model.entry_low + 1), (slot + 24, model.code_pa)]
        for offset, value in faults:
            with self.subTest(offset=offset, value=hex(value)):
                table = bytearray(model.table)
                struct.pack_into('<Q', table, offset, value)
                self.assertEqual(execute(model, 0x100, table=table)['outcome'], 'parked')
        for offset, value in ((0, 0), (8, 0), (16, 0), (16, model.table_pa + 1), (16, 1 << 36)):
            config = bytearray(model.config())
            struct.pack_into('<Q', config, offset, value)
            self.assertEqual(execute(model, 0x100, config=config)['outcome'], 'parked')

    def test_unsafe_architectural_state_is_rejected(self):
        model = self.fixture()
        for sctlr in (1, 4, 0x1000, 0x1005, 1 << 25):
            self.assertEqual(execute(model, 0x100, sctlr=sctlr)['outcome'], 'parked')
        for el in (0, 8, 12):
            self.assertEqual(execute(model, 0x100, el=el)['outcome'], 'parked')
        self.assertEqual(execute(model, 0x100, sctlr=(1 << 29), initial_daif=0)['outcome'], 'dispatched')

    def test_duplicate_entry_and_reset_stale_descriptor(self):
        model = self.fixture()
        first = execute(model, 0x100)
        self.assertEqual(first['outcome'], 'dispatched')
        self.assertEqual(execute(model, 0x100, table=first['table'])['outcome'], 'parked')
        # A new code/config epoch cannot accept a previous boot's READY slot.
        old_table = bytes(model.table)
        model.native_result(model.pending[1], 0)
        model.reset(2, all_targets_off=True)
        self.assertEqual(execute(model, 0x100, table=old_table)['outcome'], 'parked')
        self.assertEqual(execute(model, 0x100)['outcome'], 'parked')

    def test_original_gz_output_feeds_actual_assembled_bridge(self):
        # Execute the pinned ORIGINAL GZ CPU_ON handler and final LDP/MSR
        # instructions; its modeled ERET PC/X0 become AP fixture inputs.
        path = ROOT / 'diagnostic/degas-prcb-walk/test_gz_psci_context.py'
        spec = importlib.util.spec_from_file_location('gz_original_bridge_fixture', path)
        gz = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(gz)
        for cpu in (1, 7):
            for context in (0, 0x1122334455667788, MASK64):
                model = self.fixture(cpu, context)
                gz.ENTRY = model.code_pa
                original = gz.run_case(context, cpu, False)
                restored = original['actual_final_restore']
                self.assertEqual(int(restored['x0'], 16), 0)
                self.assertEqual(int(restored['pc'], 16), model.code_pa)
                self.assertEqual(int(restored['spsr'], 16), 0x3c5)
                r = execute(model, cpu << 8, initial_x0=int(restored['x0'], 16))
                self.assertEqual((r['outcome'], r['x0']), ('dispatched', context))


class PublicationTests(unittest.TestCase):
    def test_release_clean_barrier_precedes_native_call_for_every_line_size(self):
        for line_bytes in (16, 32, 64, 128):
            model = PublicationModel(line_bytes=line_bytes)
            start = len(model.events)
            call = model.publish(0x81000700, 0x40481000, MASK64)
            events = model.events[start:]
            self.assertEqual(events[0][0], 'write_descriptor')
            release = next(i for i, e in enumerate(events) if e[0] == 'stlr_ready')
            self.assertEqual(events[release - 1], ('dsb_sy',))
            self.assertEqual(events[-2], ('dsb_sy',))
            self.assertEqual(events[-1], ('native_cpu_on', 0xc4000003, 0x700, model.code_pa, 0))
            slot = model.table_pa + HEADER + 7 * STRIDE
            self.assertEqual([e[1] for e in events[1:release - 1]],
                             list(range(slot, slot + STRIDE, line_bytes)))
            self.assertEqual(events[release + 1], ('dc_cvac', slot))
            self.assertEqual(call.arguments, events[-1][1:])

    def test_duplicate_outstanding_native_failure_and_reset(self):
        model = PublicationModel()
        call = model.publish(0x100, 0x40481000, 0)
        with self.assertRaises(RuntimeError): model.publish(0x100, 0x40481000, 1)
        with self.assertRaises(RuntimeError): model.reset(2, all_targets_off=True)
        self.assertEqual(model.native_result(call, -4), -4) # ALREADY_ON is retained
        self.assertEqual(struct.unpack_from('<Q', model.table, HEADER + STRIDE)[0], CANCELLED)
        with self.assertRaises(RuntimeError): model.publish(0x100, 0x40481000, 1)
        with self.assertRaises(RuntimeError): model.native_result(call, 0)
        with self.assertRaises(RuntimeError): model.reset(2)
        with self.assertRaises(ValueError): model.reset(1, all_targets_off=True)
        model.reset(2, all_targets_off=True)
        model.publish(0x100, 0x40481000, 1)

    def test_normal_permissions_and_separate_pages(self):
        for pte in (CODE_PTE, DATA_PTE):
            self.assertEqual((MAIR >> (((pte >> 2) & 7) * 8)) & 0xff,
                             0x44 if pte == CODE_PTE else 0xff)
            self.assertEqual((pte >> 8) & 3, 3)
        self.assertFalse(CODE_PTE & (1 << 53)) # EL1 executable
        self.assertEqual((CODE_PTE >> 6) & 3, 2) # EL1 read-only
        self.assertTrue(DATA_PTE & (1 << 53)) # data PXN
        self.assertTrue(DATA_PTE & (1 << 54)) # data UXN
        self.assertEqual((DATA_PTE >> 6) & 3, 0)
        with self.assertRaises(ValueError): PublicationModel(code_pa=0x43010000)
        for name, value in (('epoch', 0), ('table_pa', 0), ('line_bytes', 256)):
            with self.assertRaises(ValueError): PublicationModel(**{name: value})

    def test_backing_ram_visibility_is_required(self):
        model = PublicationModel(line_bytes=16)
        model.publish(0x100, 0x40481000, 0x1122334455667788)
        # RAM with no READY publication is rejected, even if the BSP cache
        # has a complete descriptor. No real cache behavior is emulated.
        ram = bytearray(model.table)
        struct.pack_into('<Q', ram, HEADER + STRIDE, RESERVED)
        self.assertEqual(execute(model, 0x100, table=ram)['outcome'], 'parked')
        # A valid stale backing-RAM context can pass structural checks. This
        # deliberately demonstrates why LDAR/DSB cannot substitute for clean.
        ram = bytearray(model.table)
        struct.pack_into('<Q', ram, HEADER + STRIDE + 32, 0xdeadbeef)
        stale = execute(model, 0x100, table=ram)
        self.assertEqual(stale['x0'], 0xdeadbeef)
        self.assertEqual(execute(model, 0x100)['x0'], 0x1122334455667788)


if __name__ == '__main__':
    suite = unittest.defaultTestLoader.loadTestsFromModule(sys.modules[__name__])
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    if result.wasSuccessful():
        report = dict(status='PASS_HOST_ONLY', binary_sha256=META['binary_sha256'],
                      source_sha256=META['source_sha256'], unittest_methods=result.testsRun,
                      assembled_execution_cases=len(RESULTS),
                      original_gz_instruction_cases=6,
                      scope='Actual assembled AP instructions; system registers and backing RAM supplied by fixture. Original pinned GZ selected handler+restore instructions run separately with modeled external helpers. No cache/MMU/coherency/power/hardware proof.',
                      cases=RESULTS)
        (HERE / 'generated/test-report.json').write_text(json.dumps(report, indent=2) + '\n')
    raise SystemExit(not result.wasSuccessful())
