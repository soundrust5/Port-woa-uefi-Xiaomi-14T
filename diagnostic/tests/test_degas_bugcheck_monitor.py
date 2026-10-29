"""Execute linked monitor/reader/journal instructions; model EL1 registers/time.

Requires windows-bugcheck.{bin,json}, windows-bugcheck-symbols.txt and the
matching parking-bugcheck fixtures. Unicorn does not model hardware coherency,
MMU translation, reset retention, or a concurrently executing Windows kernel.
The software Windows page walker runs unmodified against synthetic tables.
"""
from pathlib import Path
import hashlib
import json
import struct
import sys
import unittest
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[2]
A = ROOT / 'artifacts/degas/cpu-audit'
sys.path.insert(0, str(A / 'test-deps'))
sys.path.insert(0, str(ROOT / 'diagnostic/degas-aplog'))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *
import ap_journal_v1 as journal

O = 0x44100000
B = 0x43002000  # Own CPU2 mailbox; AP1 is one page earlier.
R = 0x44000000
PRIVATE_ROOT = R + 0x70000
RET = B - 0x2000
BASE = 0xfffff80040000000
PC = BASE + 0xa91ba0
LPB = 0xfffff80010000000
TCR = 0x0695b5513511
DATA = BASE + 0xdbb9a0
INIT = BASE + 0xc57000
ACTIVE = BASE + 0xd9796c
RSDS = bytes.fromhex('52534453c06cd400a1e2483e6ac6c224799cfc2301000000')
TAG = 0x42434732 << 32
HAL = (0x5c, 0x203, 4, 0xfffff80041234560, 0xd)
DETAIL_RVAS = (0xe051e8, 0xe051f8, 0xe05218, 0xe05228, 0xe05230)
CONTROLLER = 0xfffff79c40001860
START_FILE = BASE + 0xaa5988
ENTRY_FILE = BASE + 0x633950


def fixture(stem):
    code = (A / (stem + '.bin')).read_bytes()
    meta = json.loads((A / (stem + '.json')).read_text())
    if hashlib.sha256(code).hexdigest() != meta['code_sha256']:
        raise AssertionError(stem + ': binary differs from manifest')
    for name, digest in meta['sources'].items():
        if hashlib.sha256((ROOT / name).read_bytes()).hexdigest() != digest:
            raise AssertionError(stem + ': stale source ' + name)
    symbols = {}
    for line in (A / (stem + '-symbols.txt')).read_text().splitlines():
        parts = line.split()
        if len(parts) == 3:
            symbols[parts[2]] = int(parts[0], 16)
    return code, symbols


def xreg(number):
    return globals()['UC_ARM64_REG_X' + str(number)]


class BugcheckMonitor(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.code, cls.symbols = fixture('windows-bugcheck')
        cls.parking, cls.parking_symbols = fixture('parking-bugcheck')

    def run_case(self, case, zero_params=False):
        u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        u.mem_map(B - 0x2000, 0x4000)
        u.mem_map(O, 4096)
        u.mem_map(R, 0x80000)
        u.mem_map(journal.ZONE_BASE, journal.ZONE_BYTES)
        code = bytearray(self.code)
        struct.pack_into('<Q', code, self.symbols['DegasWindowsObserverRoot'], PRIVATE_ROOT)
        u.mem_write(O, bytes(code))
        u.mem_write(B + 0x800, self.parking)

        def q(address, value):
            u.mem_write(address, struct.pack('<Q', value))

        def getq(address):
            return struct.unpack('<Q', u.mem_read(address, 8))[0]

        # Build real four-level Windows page tables inside the reader whitelist.
        next_table = R + 0x1000
        next_leaf = R + 0x20000
        leaf_pages = {}
        leaf_entries = {}

        def map_va(va):
            nonlocal next_table, next_leaf
            page = va & ~0xfff
            if page in leaf_pages:
                return leaf_pages[page] + (va & 0xfff)
            table = R
            for shift, mask in [(39, 255), (30, 511), (21, 511)]:
                address = table + ((va >> shift) & mask) * 8
                entry = getq(address)
                if not entry:
                    entry = next_table | 3
                    next_table += 4096
                    q(address, entry)
                table = entry & ~0xfff
            physical = next_leaf
            next_leaf += 4096
            leaf_entry = table + ((va >> 12) & 511) * 8
            q(leaf_entry, physical | 3)
            leaf_pages[page] = physical
            leaf_entries[page] = leaf_entry
            return physical + (va & 0xfff)

        data_pa, init_pa, active_pa = [map_va(v) for v in (DATA, INIT, ACTIVE)]
        detail_pas = [map_va(BASE + offset) for offset in DETAIL_RVAS]
        details = ((0x12, 0xd, CONTROLLER, ENTRY_FILE, 401) if case == 'entry_problem'
                   else (0xd, 0xc0000001, CONTROLLER, START_FILE, 1386))
        # Neighbor halves are nonzero canaries: extracting the wrong U32 or
        # sign-extending status must not accidentally pass.
        raw_details = ((details[0] << 32) | 0x12345678,
                       (0x87654321 << 32) | details[1], details[2], details[3],
                       (details[4] << 32) | 0x55667788)
        for address, value in zip(detail_pas, raw_details):
            q(address, value)
        final_tuple = (0x5c, 0, 0, 0, 0) if zero_params else HAL

        def tuple_write(values):
            u.mem_write(data_pa, struct.pack('<5Q', *values))

        tuple_write((0, 0, 0, 0, 0) if case in ('partial', 'timeout') else final_tuple)
        q(init_pa, 1)
        # Active is four bytes into the aligned qword the reader resolves.
        q(active_pa - 4, (3 << 32) | 0xabcdef01)
        if case == 'unmapped':
            # No root descriptor: the software reader must fail, not dereference
            # Windows VA directly or continue polling a failed translation.
            q(R + ((BASE >> 39) & 255) * 8, 0)

        u.mem_write(B, b'\xa5' * 0x800)
        q(B, 0xffffffff)
        q(B + 8, 0)
        q(B + 0xfd0, B + 0x800 + self.parking_symbols['DegasApJournalCommit'])
        for offset, value in [(0xf00, 1), (0xf08, 0x81000200),
                              (0xf10, 0x30d00800), (0xf18, 4),
                              (0xf20, 0x200), (0xf28, O + self.symbols['DegasBugcheckMonitorStart']),
                              (0xf30, 0)]:
            q(B + offset, value)
        source = B - 4096
        for offset, value in [(0xe00, 0), (0xe08, PC), (0xe10, 0xa9bd53f3d503237f),
                              (0xe18, 0x300905a4d), (0xe20, 0x22aa6400004550),
                              (0xf0, R), (0xe0, TCR), (0x368, LPB)]:
            q(source + offset, value)
        u.mem_write(source + 0xe28, RSDS)
        if case == 'identity':
            q(source + 0xe28, 0xdeadbeef)

        attempt = uuid.UUID('12345678-1234-5678-9abc-def012345678')
        build = bytes(range(32))
        zone = journal.make_reference_zone(attempt, build, B - 8192)
        u.mem_write(journal.ZONE_BASE, bytes(zone))
        commit1 = journal.ZONE_BASE + 256
        if case != 'no_ap1':
            u.mem_write(commit1, b'DGAPS001')
        zone_before = bytes(u.mem_read(journal.ZONE_BASE, journal.ZONE_BYTES))
        source_before = bytes(u.mem_read(source, 4096))
        os_before = bytes(u.mem_read(B, 0x800))

        initial = dict(sctlr=0x30d00800, ttbr0=0x12345000,
                       ttbr1=0x23456000, tcr=0xabcdefff, mair=0xaabbccdd)
        if case == 'unsafe_sctlr':
            initial['sctlr'] |= 5
        state = dict(initial)
        preserved = [xreg(i) for i in (0, 1, 10)] + [xreg(i) for i in range(18, 31)] + [UC_ARM64_REG_SP]
        for i, register in enumerate(preserved):
            u.reg_write(register, 0x1234567800000000 + i * 0x1010)
        u.reg_write(UC_ARM64_REG_X0, B)
        u.reg_write(UC_ARM64_REG_X1, 0x12345670)
        u.reg_write(UC_ARM64_REG_X10, 0x200)
        if case == 'wrong_cpu':
            u.reg_write(UC_ARM64_REG_X10, 0x300)
        u.reg_write(UC_ARM64_REG_X30, RET)
        before = [u.reg_read(register) for register in preserved]
        ticks = 0
        returned = False
        samples = 0
        reads = []
        journal_writes = []
        committed_at = None
        final_installed_at = 0
        step = 60000000 if case == 'no_ap1' else 100000000 if case == 'timeout' else 13000
        release_target = 0x15eaf7010

        def release():
            q(B, 0x200)
            q(B + 8, release_target)

        if case == 'release':
            release()
            os_before = bytes(u.mem_read(B, 0x800))
        if case in ('release_id_only', 'release_entry_only'):
            if case == 'release_id_only':
                q(B, 0x200)
            else:
                q(B + 8, release_target)
            os_before = bytes(u.mem_read(B, 0x800))

        def instruction(uc, pc, size, _):
            nonlocal returned, ticks, samples, final_installed_at
            if pc == RET:
                returned = True
                uc.emu_stop()
                return
            opcode = int.from_bytes(uc.mem_read(pc, 4), 'little')
            masked, rn = opcode & ~31, opcode & 31
            if masked == 0xd53800a0:  # MPIDR_EL1
                uc.reg_write(xreg(rn), 0x81000200)
                uc.reg_write(UC_ARM64_REG_PC, pc + 4)
                return
            mrs = {0xd5381000: 'sctlr', 0xd5384240: 'el', 0xd5382000: 'ttbr0',
                   0xd5382020: 'ttbr1', 0xd5382040: 'tcr', 0xd538a200: 'mair'}
            msr = {0xd5181000: 'sctlr', 0xd5182000: 'ttbr0', 0xd5182020: 'ttbr1',
                   0xd5182040: 'tcr', 0xd518a200: 'mair'}
            if masked in mrs:
                name = mrs[masked]
                uc.reg_write(xreg(rn), (8 if case == 'unsafe_el' else 4) if name == 'el' else state[name])
                uc.reg_write(UC_ARM64_REG_PC, pc + 4)
                return
            if masked in msr:
                state[msr[masked]] = 0 if rn == 31 else uc.reg_read(xreg(rn))
                if msr[masked] == 'sctlr' and state['sctlr'] & 5 == 5:
                    self.assertEqual([state[k] for k in ('ttbr0', 'ttbr1', 'tcr', 'mair')],
                                     [PRIVATE_ROOT, 0, 0x100803019, 0xff0044])
                uc.reg_write(UC_ARM64_REG_PC, pc + 4)
                return
            if masked in (0xd53be040, 0xd53be020):  # CNTVCT_EL0 / CNTPCT_EL0
                ticks += step
                uc.reg_write(xreg(rn), ticks)
                uc.reg_write(UC_ARM64_REG_PC, pc + 4)
                return
            if masked == 0xd53be000:  # CNTFRQ_EL0
                uc.reg_write(xreg(rn), 13000000)
                uc.reg_write(UC_ARM64_REG_PC, pc + 4)
                return
            self.assertNotIn(masked, (0xd50b7e20, 0xd50b7a20, 0xd50b7b20, 0xd5087620, 0xd50b7520),
                             'monitor/reader executed cache maintenance')
            self.assertNotIn(opcode, (0xd508751f, 0xd508711f), 'monitor invalidated instruction cache')
            if opcode == 0xd508871f:  # TLBI VMALLE1; architectural state modeled above
                uc.reg_write(UC_ARM64_REG_PC, pc + 4)
                return
            if state['sctlr'] & 5 == 5:
                self.assertTrue(O <= pc < O + len(self.code), 'execution outside private NC code with MMU enabled')
            if pc == O + self.symbols['DegasPrcbRead64']:
                requested = uc.reg_read(UC_ARM64_REG_X0)
                if requested == DATA:
                    samples += 1
                    if case == 'partial' and samples == 8:
                        tuple_write(final_tuple)
                        final_installed_at = ticks
                    if case == 'changed' and samples == 3:
                        tuple_write((0x5c, 0x203, 4, 0xfffff80049999000, 0xd))
                        final_installed_at = ticks
                    if case == 'release_midway' and samples == 2:
                        release()
                if case.startswith('unmapped_details_'):
                    stage = int(case.rsplit('_', 1)[1])
                    if requested == BASE + DETAIL_RVAS[stage - 13]:
                        q(leaf_entries[requested & ~0xfff], 0)

        def read(uc, access, address, size, value, _):
            if R <= address < R + 0x70000:
                self.assertEqual(state['sctlr'] & 5, 5, 'uncached physical read of Windows RAM')
                reads.append((address, size))

        def write(uc, access, address, size, value, _):
            nonlocal committed_at
            if B + 0xe00 <= address and address + size <= B + 4096:
                return
            slot = journal.ZONE_BASE + 512
            self.assertTrue((address == slot and size == 8) or
                            slot + 0x40 <= address and address + size <= slot + 0xf8,
                            f'monitor wrote outside private snapshot/owned journal: {address:x}')
            self.assertEqual(state, initial, 'journal commit ran before architectural state restoration')
            journal_writes.append((address, size))
            if address == slot:
                committed_at = ticks

        u.hook_add(UC_HOOK_CODE, instruction)
        u.hook_add(UC_HOOK_MEM_READ, read)
        u.hook_add(UC_HOOK_MEM_WRITE, write)
        u.emu_start(O + self.symbols['DegasBugcheckMonitorStart'], RET + 4, count=600000)
        self.assertTrue(returned, 'monitor exceeded bounded instruction budget')
        self.assertEqual(state, initial)
        self.assertEqual([u.reg_read(register) for register in preserved], before)
        self.assertEqual(bytes(u.mem_read(source, 4096)), source_before, 'monitor modified AP1 state')
        if case == 'release_midway':
            expected_os = bytearray(os_before)
            struct.pack_into('<I', expected_os, 0, 0x200)
            struct.pack_into('<Q', expected_os, 8, release_target)
            os_before = bytes(expected_os)
        self.assertEqual(bytes(u.mem_read(B, 0x800)), os_before, 'monitor modified OS-owned startup state')
        snapshot = list(struct.unpack('<8Q', u.mem_read(B + 0xe00, 64)))
        actual_zone = bytes(u.mem_read(journal.ZONE_BASE, journal.ZONE_BYTES))
        if case in ('release', 'release_midway', 'unsafe_el', 'unsafe_sctlr', 'wrong_cpu'):
            self.assertEqual(journal_writes, [], 'released Windows AP committed monitor output')
            self.assertEqual(actual_zone, zone_before)
            if case != 'release_midway':
                self.assertEqual(reads, [])
            return
        detail_failure = case.startswith('unmapped_details_')
        status = 3 if detail_failure else {'timeout': 1, 'no_ap1': 1, 'identity': 2, 'unmapped': 3}.get(case, 0)
        self.assertEqual(snapshot[0] & 0xffffffff, status)
        if detail_failure:
            self.assertEqual(snapshot[0] >> 32, int(case.rsplit('_', 1)[1]))
        self.assertEqual(snapshot[7], ticks, 'snapshot must retain final sampled counter')
        if case in ('identity', 'no_ap1'):
            self.assertEqual(reads, [], 'monitor dereferenced Windows RAM before committed valid identity')
        if case == 'no_ap1':
            # Entry sample60M, exact deadline at840M, result sample900M.
            # This discriminates 780000000 from an adjacent encoded deadline.
            self.assertEqual(ticks, 900000000)
        if not status or detail_failure:
            expected = (0x5c, 0x203, 4, 0xfffff80049999000, 0xd) if case == 'changed' else final_tuple
            self.assertEqual(snapshot[1], (3 << 32) | 1)
            self.assertEqual(tuple(snapshot[2:7]), expected)
            self.assertGreaterEqual(samples, 2)
            if case in ('partial', 'changed'):
                self.assertGreaterEqual(committed_at - final_installed_at, 130000,
                                        'committed before final tuple settled')
        self.assertGreater(len(journal_writes), 1)
        self.assertEqual(journal_writes[-1], (journal.ZONE_BASE + 512, 8))
        self.assertEqual(actual_zone[:512], zone_before[:512])
        self.assertEqual(actual_zone[768:], zone_before[768:])
        slot = actual_zone[512:768]
        self.assertEqual(slot[:8], b'DGAPS001')
        self.assertEqual(list(struct.unpack_from('<8Q', slot, 0x40)), snapshot)
        verified = case not in ('identity', 'no_ap1')
        expected_details = list(details) if not status else [0] * 5
        if detail_failure:
            completed = int(case.rsplit('_', 1)[1]) - 13
            expected_details[:completed] = details[:completed]
        self.assertEqual(list(struct.unpack('<5Q', u.mem_read(B + 0xe40, 40))), expected_details)
        packed_problem = (expected_details[0] << 32) | expected_details[1]
        self.assertEqual(getq(B + 0xf38), packed_problem)
        self.assertEqual(struct.unpack_from('<Q', slot, 0xb8)[0], packed_problem)
        self.assertEqual(struct.unpack_from('<5Q', slot, 0xc0),
                         (BASE if verified else 0, expected_details[2], expected_details[3],
                          expected_details[4], TAG | (snapshot[0] & 0xffffffff)))
        # Monitor context no longer carries physical CPU/LocalId: these must
        # remain independently correct in the slot header and telemetry.
        self.assertEqual(struct.unpack_from('<2Q', slot, 0x20), (0x200, 0x200))
        self.assertEqual(struct.unpack_from('<Q', slot, 0x88)[0], 0x81000200)
        self.assertEqual(struct.unpack_from('<Q', slot, 0xa0)[0], 0x200)
        crc, inverse = struct.unpack_from('<II', slot, 0xf0)
        self.assertEqual(crc, zlib.crc32(slot[8:0xf0]))
        self.assertEqual(crc ^ inverse, 0xffffffff)
        decoded = journal.decode(actual_zone, attempt, build)['records'][1]
        self.assertTrue(decoded['valid'], decoded['status'])
        self.assertEqual(decoded['record_kind'], 'bugcheck_monitor_v2')
        self.assertEqual(decoded['bugcheck_monitor']['counter'], ticks)
        self.assertEqual(decoded['context']['status'], status)
        self.assertEqual(decoded['context']['kind_tag'], 'BCG2')
        self.assertEqual(decoded['context']['last_problem_controller'], f'0x{expected_details[2]:016x}')
        self.assertEqual(decoded['context']['last_problem_file'], f'0x{expected_details[3]:016x}')
        self.assertEqual(decoded['context']['last_problem_line'], expected_details[4])

    def test_stable_hal_tuple(self):
        self.run_case('stable')

    def test_all_zero_parameters_are_valid(self):
        self.run_case('stable', zero_params=True)

    def test_initialized_precedes_tuple_publication(self):
        self.run_case('partial')

    def test_changed_tuple_restarts_settling(self):
        self.run_case('changed')

    def test_bounded_failures(self):
        for case in ('timeout', 'no_ap1', 'identity', 'unmapped'):
            with self.subTest(case=case):
                self.run_case(case)

    def test_windows_release_aborts_without_consuming_state(self):
        for case in ('release', 'release_midway'):
            with self.subTest(case=case):
                self.run_case(case)

    def test_release_requires_both_predicates(self):
        for case in ('release_id_only', 'release_entry_only'):
            with self.subTest(case=case):
                self.run_case(case)

    def test_architectural_and_cpu_guards(self):
        for case in ('unsafe_el', 'unsafe_sctlr', 'wrong_cpu'):
            with self.subTest(case=case):
                self.run_case(case)

    def test_fixture_layout(self):
        self.assertLessEqual(len(self.code), 4096)
        self.assertLessEqual(len(self.parking), 0x600)
        self.assertLess(self.symbols['DegasBugcheckMonitorStart'], self.symbols['DegasWindowsObserverEnd'])

    def test_start_failure_details_with_negative_status(self):
        self.run_case('start_problem')

    def test_unrecognized_interrupt_entry_details(self):
        self.run_case('entry_problem')

    def test_every_detail_walk_failure_is_bounded(self):
        for stage in range(13, 18):
            with self.subTest(stage=stage):
                self.run_case('unmapped_details_' + str(stage))


if __name__ == '__main__':
    unittest.main()
