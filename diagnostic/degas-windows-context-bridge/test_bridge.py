"""Actual pinned native AP instructions, host only; no Windows boot emulation."""
from pathlib import Path
import json
import struct
import sys
import unittest

from bridge_fixture import *
sys.path.insert(0, str(ROOT / 'tools/vendor/python'))
sys.path.insert(0, str(ROOT / 'artifacts/degas/cpu-audit/test-deps'))
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN
from unicorn import Uc, UcError, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_CODE
from unicorn.arm64_const import *

TABLE_BASE = 0x43000000
COMMON_PAGE = 0x44000000
TTBR = 0x45000000
RESULTS = []
BUILDER_RESULTS = []
PSCI_RESULTS = []


def r(n):
    return globals()['UC_ARM64_REG_X' + str(n)]


def run_builder(image):
    """Execute pinned498170..49825C; model only its external MM helpers."""
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(PREFERRED_BASE, len(image))
    u.mem_write(PREFERRED_BASE, bytes(image))
    u.mem_map(COMMON_PAGE, 0x1000)
    ram, size = 0x46000000, 0x20000
    u.mem_map(ram, size)
    descriptor = ram + 0x100
    u.mem_write(COMMON_PAGE, b'\xa5' * 0x1000)
    u.mem_write(descriptor + 8, struct.pack('<Q', TTBR))
    u.mem_write(descriptor + 0x18, struct.pack('<QQ', COMMON_PAGE, COMMON_PAGE))
    u.reg_write(UC_ARM64_REG_SP, ram + size - 0x100)
    u.reg_write(r(2), descriptor)
    end = PREFERRED_BASE + IMAGE_SIZE - 0x1000
    u.reg_write(r(30), end)
    calls = []

    def external_mm(_, pc, size, data):
        target = pc - PREFERRED_BASE
        if target not in [0x632060, 0x631860, 0x39cdc0, 0x25c1d8, 0x481bd0]:
            return
        x0, x1, x2 = [u.reg_read(r(i)) for i in range(3)]
        calls.append(hex(target))
        if target == 0x632060:
            assert (x0, x1, x2) == (COMMON_PAGE + 0x10, 0, 0xff0)
            u.mem_write(x0, bytes([x1]) * x2)
        elif target == 0x631860:
            assert (x0, x1, x2) == (COMMON_PAGE + 0x10,
                                    PREFERRED_BASE + TEMPLATE_START, 0x180)
            u.mem_write(x0, bytes(u.mem_read(x1, x2)))
        elif target == 0x39cdc0:
            assert x0 == COMMON_PAGE + 0x10
            # Synthetic identity mapping for this bounded host fixture.
        elif target == 0x25c1d8:
            assert (x0, x1) == (COMMON_PAGE, 0x1000)
        elif target == 0x481bd0:
            assert (x0, x1, x2) == (COMMON_PAGE, 1, 0)
            u.reg_write(r(0), 0)
        u.reg_write(UC_ARM64_REG_PC, u.reg_read(r(30)))

    u.hook_add(UC_HOOK_CODE, external_mm)
    u.emu_start(PREFERRED_BASE + 0x498170, end, count=500)
    assert u.reg_read(UC_ARM64_REG_PC) == end
    page = bytes(u.mem_read(COMMON_PAGE, 0x1000))
    entry = struct.unpack('<Q', u.mem_read(PREFERRED_BASE + 0xe06760, 8))[0]
    alias = struct.unpack('<Q', u.mem_read(PREFERRED_BASE + 0xe06798, 8))[0]
    return page, dict(external_helpers=calls, physical_entry=hex(entry),
                      virtual_page=hex(alias), descriptor_physical_cleared=
                      bytes(u.mem_read(descriptor + 0x18, 8)) == b'\0' * 8)


def run_native_cpu_on(image, cpu, provider_result=0):
    """Pinned caller/wrapper/SMC dispatch; model CFGcheck and external SMC."""
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(PREFERRED_BASE, len(image))
    u.mem_write(PREFERRED_BASE, bytes(image))
    ram = 0x46000000
    u.mem_map(ram, 0x20000)
    descriptors, entry_pointer = ram + 0x1000, ram + 0x200
    u.mem_write(PREFERRED_BASE + 0xe06788, struct.pack('<Q', descriptors))
    u.mem_write(PREFERRED_BASE + 0xe04760, struct.pack('<I', 1))
    u.mem_write(PREFERRED_BASE + 0xe04740, struct.pack('<Q', PREFERRED_BASE + 0x498440))
    mailbox = TABLE_BASE + cpu * 0x1000
    u.mem_write(descriptors + cpu * 64 + 0x18, struct.pack('<Q', mailbox))
    u.mem_write(descriptors + cpu * 64 + 0x30, struct.pack('<Q', cpu << 8))
    u.mem_write(entry_pointer, struct.pack('<Q', COMMON_PAGE + 0x10))
    u.reg_write(UC_ARM64_REG_SP, ram + 0x1ff00)
    u.reg_write(r(0), cpu)
    u.reg_write(r(1), entry_pointer)
    end = PREFERRED_BASE + IMAGE_SIZE - 0x1000
    u.reg_write(r(30), end)
    calls = []

    def external(_, pc, size, data):
        if pc == PREFERRED_BASE + 0x626980:  # CFG target validator
            assert u.reg_read(r(15)) == PREFERRED_BASE + 0x498440
            u.reg_write(UC_ARM64_REG_PC, u.reg_read(r(30)))
        elif pc == PREFERRED_BASE + 0x6261d8:  # realSMC instruction
            calls.append([u.reg_read(r(i)) for i in range(4)])
            u.reg_write(r(0), provider_result & ((1 << 64) - 1))
            u.reg_write(UC_ARM64_REG_PC, pc + 4)

    u.hook_add(UC_HOOK_CODE, external)
    u.emu_start(PREFERRED_BASE + 0x47f8c8, end, count=500)
    assert u.reg_read(UC_ARM64_REG_PC) == end
    return dict(cpu=cpu, calls=[[hex(x) for x in row] for row in calls],
                return_status=hex(u.reg_read(r(0)) & 0xffffffff))


def run_prefix(image, cpu, incoming, daif=0x3c0, table_base=TABLE_BASE):
    """Use HAL's exact copy/fixup layout; execute through first context reads.

    The real external MM functions are not emulated. Common page identity and
    higher virtual continuation literals are fixed exactly as the builder does.
    CPU MMU is kept disabled; sysreg writes and cache/TLBI instructions execute
    in Unicorn. Stop at physical prefix end before MMU enable/virtual transfer.
    """
    u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
    u.mem_map(table_base, 0x8000)
    u.mem_map(COMMON_PAGE, 0x1000)
    u.mem_map(TTBR, 0x1000)
    u.mem_write(COMMON_PAGE + 0x10, bytes(image[TEMPLATE_START:TEMPLATE_END]))
    # Builder4981A0: [page+0] virtualpage, [page+8] identityTTBR.
    u.mem_write(COMMON_PAGE, struct.pack('<QQ', COMMON_PAGE, TTBR))
    # Builder4981F4/498218 rewrite copied target/virtualtarget literals.
    u.mem_write(COMMON_PAGE + 0x20, struct.pack('<Q', COMMON_PAGE + 0x30))
    u.mem_write(COMMON_PAGE + 0x28, struct.pack('<Q', COMMON_PAGE + 0x130))
    mailbox = table_base + cpu * 0x1000
    context = mailbox + 0x20
    # Real native mailbox/context layout; distinct per-CPU markers.
    tpidr = 0xfffff80060000980 + cpu * 0x10000
    u.mem_write(mailbox + 0x10, struct.pack('<Q', 0xfffff80070000020 + cpu * 0x1000))
    u.mem_write(context + 0x10, struct.pack('<Q', 0x1111111111111111 + cpu))
    u.mem_write(context + 0x18, struct.pack('<Q', 0x2222222222222222 + cpu))
    u.mem_write(context + 0x20, struct.pack('<Q', tpidr))
    u.mem_write(context + 0xc0, struct.pack('<Q', 0))
    u.mem_write(context + 0x308, struct.pack('<Q', 0xff))
    u.mem_write(context + 0x440, struct.pack('<Q', mailbox + 0xf00))
    # Unicorn's MPIDR_EL1 is read-only and fixed0x80000000. Model ONLY this
    # hardware input at the exact decoded MRS, then execute all recovery math.
    # This is not an injected context value: X0 receives the CPU's raw MPIDR.
    def hardware_input(_, pc, size, data):
        if bytes(u.mem_read(pc, 4)) == struct.pack('<I', 0xd53800a0):
            u.reg_write(r(0), 0x81000000 | (cpu << 8))
            u.reg_write(UC_ARM64_REG_PC, pc + 4)
    u.hook_add(UC_HOOK_CODE, hardware_input)
    u.reg_write(UC_ARM64_REG_PSTATE, daif | 5)  # EL1h, originalDAIFcondition
    for i in range(31):
        u.reg_write(r(i), 0xa5000000 + i)
    u.reg_write(r(0), incoming)
    stop = COMMON_PAGE + 0xf4  # source200184 MSR SCTLR: stop BEFORE MMU enable
    try:
        u.emu_start(COMMON_PAGE + 0x10, stop, count=200)
        error = None
    except UcError as e:
        error = str(e)
    return dict(cpu=cpu, incoming=hex(incoming), mailbox=hex(mailbox),
                pc=hex(u.reg_read(UC_ARM64_REG_PC)), error=error,
                x0=hex(u.reg_read(r(0))), x1=hex(u.reg_read(r(1))),
                x2=hex(u.reg_read(r(2))), x4=hex(u.reg_read(r(4))),
                ttbr0=hex(u.reg_read(UC_ARM64_REG_TTBR0_EL1)),
                ttbr1=hex(u.reg_read(UC_ARM64_REG_TTBR1_EL1)),
                tpidr_el0=hex(u.reg_read(UC_ARM64_REG_TPIDR_EL0)),
                tpidrro_el0=hex(u.reg_read(UC_ARM64_REG_TPIDRRO_EL0)),
                tpidr_el1=hex(u.reg_read(UC_ARM64_REG_TPIDR_EL1)),
                x18=hex(u.reg_read(r(18))), sp=hex(u.reg_read(UC_ARM64_REG_SP)),
                expected_tpidr=hex(tpidr), stop=hex(stop))


class BridgeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.original = source_mapped()
        cls.patched = patch_fixture(cls.original, PREFERRED_BASE, TABLE_BASE)

    def test_original_valid_nonzero_context(self):
        for cpu in range(8):
            result = run_prefix(self.original, cpu, TABLE_BASE + cpu * 0x1000)
            self.assertIsNone(result['error'])
            self.assertEqual(result['pc'], result['stop'])
            self.assertEqual(int(result['x2'], 16), TABLE_BASE + cpu * 0x1000 + 0x20)
            self.assertEqual(result['tpidr_el0'], hex(0x1111111111111111 + cpu))
            RESULTS.append(dict(mode='original-valid-context', **result))

    def test_actual_global_builder_copy_and_fixups(self):
        for mode, image in [('original', self.original), ('bridge', self.patched)]:
            page, result = run_builder(image)
            expected = bytearray(0x1000)
            expected[:16] = struct.pack('<QQ', COMMON_PAGE, TTBR)
            expected[0x10:0x190] = image[TEMPLATE_START:TEMPLATE_END]
            struct.pack_into('<Q', expected, 0x20, COMMON_PAGE + 0x30)
            struct.pack_into('<Q', expected, 0x28, COMMON_PAGE + 0x130)
            self.assertEqual(page, bytes(expected))
            self.assertEqual(result['physical_entry'], hex(COMMON_PAGE + 0x10))
            self.assertEqual(result['virtual_page'], hex(COMMON_PAGE))
            self.assertTrue(result['descriptor_physical_cleared'])
            BUILDER_RESULTS.append(dict(mode=mode, **result))

    def test_native_caller_context_reaches_smc_unchanged(self):
        for cpu in [1, 4, 7]:
            result = run_native_cpu_on(self.original, cpu)
            self.assertEqual(result['calls'], [[hex(0xc4000003), hex(cpu << 8),
                                               hex(COMMON_PAGE + 0x10),
                                               hex(TABLE_BASE + cpu * 0x1000)]])
            self.assertEqual(result['return_status'], '0x0')
            PSCI_RESULTS.append(result)

    def test_original_context_loss_fails(self):
        for cpu in range(8):
            result = run_prefix(self.original, cpu, 0)
            self.assertIsNotNone(result['error'])
            self.assertEqual(result['x2'], '0x20')
            # Actual LDR X4,[X2,#C0] at200100 dereferences unmapped0xE0.
            self.assertEqual(int(result['pc'], 16), COMMON_PAGE + 0x70)
            RESULTS.append(dict(mode='original-lost-context', **result))

    def test_bridge_matches_original_prefix_architecture(self):
        for cpu in range(8):
            expected = run_prefix(self.original, cpu, TABLE_BASE + cpu * 0x1000)
            for incoming in [0, TABLE_BASE + cpu * 0x1000, 0xdeadbeefcafef00d]:
                result = run_prefix(self.patched, cpu, incoming)
                self.assertIsNone(result['error'])
                for key in ['pc', 'x0', 'x1', 'x2', 'x4', 'ttbr0', 'ttbr1',
                            'tpidr_el0', 'tpidrro_el0', 'tpidr_el1', 'x18', 'sp']:
                    self.assertEqual(result[key], expected[key], key)
                RESULTS.append(dict(mode='bridge', **result))

    def test_unmasked_entry_stays_in_failure_loop(self):
        # Original checks DAIF.I. Both paths refuse execution if it is clear.
        for mode, image in [('original', self.original), ('bridge', self.patched)]:
            result = run_prefix(image, 1, 0, daif=0x340)
            self.assertIsNone(result['error'])
            expected_pc = COMMON_PAGE + (0x38 if mode == 'original' else 0x34)
            self.assertEqual(int(result['pc'], 16), expected_pc)

    def test_allocation_immediates_and_page_boundaries(self):
        for base in [0x1000, 0x4100b000, 0xffff8000]:
            patched = patch_fixture(self.original, PREFERRED_BASE, base)
            for cpu in [0, 1, 7]:
                expected = run_prefix(self.original, cpu, base + cpu * 0x1000, table_base=base)
                result = run_prefix(patched, cpu, 0, table_base=base)
                for key in ['pc', 'error', 'x0', 'x1', 'x2', 'x4', 'ttbr0', 'ttbr1',
                            'tpidr_el0', 'tpidrro_el0', 'tpidr_el1', 'x18', 'sp']:
                    self.assertEqual(result[key], expected[key], key)
                RESULTS.append(dict(mode='bridge-allocation-boundary', **result))

    def test_only_prefix_changes_and_decodes(self):
        self.assertEqual(len(self.patched), len(self.original))
        self.assertEqual(self.original[:PREFIX_START], self.patched[:PREFIX_START])
        self.assertEqual(self.original[PREFIX_END:], self.patched[PREFIX_END:])
        cs = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
        insns = list(cs.disasm(bytes(self.patched[PREFIX_START:PREFIX_END]), PREFIX_START))
        self.assertEqual(len(insns), 14)
        self.assertEqual(insns[3].op_str, 'x0, mpidr_el1')
        self.assertEqual(insns[7].op_str, 'x0, x2, x0, lsl #12')

    def test_runtime_relocation_and_rejections(self):
        relocated = bytearray(self.original)
        runtime_base = 0xfffff800d3c00000
        struct.pack_into('<Q', relocated, 0x200218, runtime_base + 0x2001fc)
        validate_loaded(relocated, runtime_base)
        patch_fixture(relocated, runtime_base, TABLE_BASE)
        for at in [0x2000c0, 0x200150, 0x200218, 0x49835c]:
            changed = bytearray(relocated)
            changed[at] ^= 1
            with self.assertRaises(ValueError):
                patch_fixture(changed, runtime_base, TABLE_BASE)
        with self.assertRaises(ValueError):
            patch_fixture(relocated[:-1], runtime_base, TABLE_BASE)
        for base in [0, 1, 0xffff9000, 0x100000000]:
            with self.assertRaises(ValueError):
                patch_fixture(relocated, runtime_base, base)

    def test_physical_image_address_differs_from_relocation_va(self):
        image = bytearray(self.original)
        physical_base, virtual_base = 0x50000000, 0xfffff800d3c00000
        struct.pack_into('<Q', image, 0x200218, virtual_base + 0x2001fc)
        identity = validate_physical_candidate(image, physical_base)
        self.assertEqual(identity['virtual_base'], virtual_base)
        self.assertEqual(identity['prefix_physical'], physical_base + PREFIX_START)
        with self.assertRaises(ValueError):
            validate_loaded(image, physical_base)
        # An unrelocated preferred pointer is not accepted as a live kernel VA.
        with self.assertRaises(ValueError):
            validate_physical_candidate(self.original, physical_base)
        with self.assertRaises(ValueError):
            validate_physical_candidate(image, physical_base + 1)


if __name__ == '__main__':
    before = sha(SOURCE.read_bytes())
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BridgeTests)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    assert sha(SOURCE.read_bytes()) == before == SOURCE_SHA256
    cs = Cs(CS_ARCH_ARM64, CS_MODE_LITTLE_ENDIAN)
    prefix = struct.pack('<14I', *prefix_words(TABLE_BASE))
    summary = dict(identity_summary(), tests_run=result.testsRun,
                   successful=result.wasSuccessful(), instruction_cases=len(RESULTS),
                   source_unchanged=True, synthetic_reserved_base=hex(TABLE_BASE),
                   prefix_disassembly=[f'{i.address:08X}: {i.bytes.hex()} {i.mnemonic} {i.op_str}'
                                       for i in cs.disasm(prefix, PREFIX_START)],
                   actual_builder_cases=BUILDER_RESULTS,
                   actual_native_psci_cases=PSCI_RESULTS,
                   cases=RESULTS,
                   limits='Host Unicorn native AP prefix; MPIDR_EL1 hardware read modeled. No EBS discovery, GZ execution, '
                          'GIC, live cache coherency, MMU transition, PatchGuard, HVCI or Windows boot validation.')
    output = Path(__file__).with_name('host-results.json')
    output.write_text(json.dumps(summary, indent=2) + '\n')
    print(json.dumps({k: v for k, v in summary.items() if k not in ['cases', 'prefix_disassembly']}, indent=2))
    sys.exit(0 if result.wasSuccessful() else 1)
