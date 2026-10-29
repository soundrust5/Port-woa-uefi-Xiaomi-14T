"""Execute the complete framebuffer readback image in Unicorn.

MRS values are synthetic, matching the host-only model in test_entry.py.  The
test exercises the final linked payload and its memory effects; it does not
model physical display hardware or cache coherency.
"""

from __future__ import annotations

from pathlib import Path
import unittest

import test_entry as entry_test
import test_marker_entry as marker_test
from test_entry import (
    CURRENT_EL,
    DAIF,
    LOAD_BASES,
    RECORD_SIZE,
    SCTLR_EL1,
    SCTLR_EL2,
    EntryArtifacts,
)
from test_marker_entry import (
    FRAMEBUFFER_BASE,
    FRAMEBUFFER_SIZE,
    MARKER_HEIGHT,
    MARKER_WIDTH,
    SENTINEL,
    MarkerEmulationResult,
)


READBACK_BUILD_DIR = Path(__file__).resolve().parents[1] / "build-readback"
READBACK_ELF_PATH = READBACK_BUILD_DIR / "entry.elf"
READBACK_BIN_PATH = READBACK_BUILD_DIR / "entry.bin"
INSTRUCTION_LIMIT = 20_000_000


class ReadbackEntryArtifacts(EntryArtifacts):
    REQUIRED_SYMBOLS = EntryArtifacts.REQUIRED_SYMBOLS + (
        "__stack_start",
        "__stack_end",
        "diagnostic_marker_readback",
        "marker_verify",
        "diagnostic_readback_ok",
        "diagnostic_readback_failed",
    )

    def __init__(self) -> None:
        old_elf, old_bin = entry_test.ELF_PATH, entry_test.BIN_PATH
        entry_test.ELF_PATH = READBACK_ELF_PATH
        entry_test.BIN_PATH = READBACK_BIN_PATH
        try:
            super().__init__()
        finally:
            entry_test.ELF_PATH, entry_test.BIN_PATH = old_elf, old_bin


class ReadbackEmulationResult(MarkerEmulationResult):
    def __init__(self, *args, corrupt_before_verify: bool = False, **kwargs) -> None:
        self.corrupt_before_verify = corrupt_before_verify
        self.corruption_injected = False
        self.marker_verify_entries = 0
        super().__init__(*args, **kwargs)

    def prepare(self, *args, **kwargs) -> "ReadbackEmulationResult":
        super().prepare(*args, **kwargs)
        self.corruption_injected = False
        self.marker_verify_entries = 0
        return self

    def _hook_code(self, uc, address: int, size: int, user_data) -> None:
        symbols = self.artifacts.symbols
        if address == self.base + symbols["marker_verify"]:
            self.marker_verify_entries += 1
            if self.corrupt_before_verify and not self.corruption_injected:
                first_byte = bytes(uc.mem_read(FRAMEBUFFER_BASE, 1))[0]
                uc.mem_write(FRAMEBUFFER_BASE, bytes([first_byte ^ 1]))
                self.corruption_injected = True

        if address == self.base + symbols["diagnostic_readback_ok"]:
            self.stop_symbol = "diagnostic_readback_ok"
            uc.emu_stop()
        elif address == self.base + symbols["diagnostic_readback_failed"]:
            self.stop_symbol = "diagnostic_readback_failed"
            uc.emu_stop()
        else:
            super()._hook_code(uc, address, size, user_data)

    def run(self) -> "ReadbackEmulationResult":
        self.uc.emu_start(
            self.base + self.artifacts.symbols["_start"],
            self.image_end,
            count=INSTRUCTION_LIMIT,
        )
        return self


class ReadbackEntryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.artifacts = ReadbackEntryArtifacts()

    def _supported_registers(self, current_el: int) -> dict:
        return marker_test.MarkerEntryTests._supported_registers(self, current_el)

    def _assert_success_contract(self, result: ReadbackEmulationResult) -> None:
        self.assertEqual(result.stop_symbol, "diagnostic_readback_ok")
        self.assertEqual(result.marker_verify_entries, 1)
        self.assertFalse(result.unknown_system_reads)
        self.assertFalse(result.out_of_contract_writes)
        self.assertTrue(result.writes)  # Capture record and linked internal stack.
        # Drawing accounts for every framebuffer write.  A write by verification
        # would make this larger, proving the successful readback path is read-only.
        self.assertEqual(
            result.framebuffer_write_count, MARKER_WIDTH * MARKER_HEIGHT * 4
        )
        marker_test.MarkerEntryTests._assert_framebuffer(self, result)

    def test_good_readback_reaches_success_at_el1_and_el2(self) -> None:
        for base in LOAD_BASES:
            for current_el in (4, 8):
                with self.subTest(base=hex(base), current_el=current_el):
                    registers = self._supported_registers(current_el)
                    result = ReadbackEmulationResult(
                        self.artifacts, base, registers
                    ).run()
                    self.assertEqual(set(result.system_reads), set(registers))
                    self._assert_success_contract(result)

    def test_corruption_immediately_before_verify_reaches_failure(self) -> None:
        registers = self._supported_registers(4)
        result = ReadbackEmulationResult(
            self.artifacts,
            LOAD_BASES[0],
            registers,
            corrupt_before_verify=True,
        ).run()

        self.assertEqual(result.stop_symbol, "diagnostic_readback_failed")
        self.assertEqual(result.marker_verify_entries, 1)
        self.assertTrue(result.corruption_injected)
        self.assertFalse(result.unknown_system_reads)
        self.assertFalse(result.out_of_contract_writes)
        self.assertEqual(
            result.framebuffer_write_count, MARKER_WIDTH * MARKER_HEIGHT * 4
        )

    def test_unsupported_m_c_ee_and_el3_do_not_write(self) -> None:
        cases = (
            (4, {SCTLR_EL1: 1 << 0}),
            (4, {SCTLR_EL1: 1 << 2}),
            (8, {SCTLR_EL2: 1 << 25}),
            (12, {}),
        )
        for current_el, extra in cases:
            with self.subTest(current_el=current_el, extra=extra):
                registers = {DAIF: 0x3C0, CURRENT_EL: current_el, **extra}
                result = ReadbackEmulationResult(
                    self.artifacts, LOAD_BASES[0], registers
                ).run()

                self.assertEqual(result.stop_symbol, "diagnostic_unsupported")
                self.assertEqual(result.marker_verify_entries, 0)
                self.assertFalse(result.unknown_system_reads)
                self.assertEqual(result.writes, [])
                self.assertEqual(result.framebuffer_write_count, 0)
                self.assertEqual(result.out_of_contract_writes, [])
                self.assertEqual(
                    bytes(result.uc.mem_read(FRAMEBUFFER_BASE, FRAMEBUFFER_SIZE)),
                    bytes([SENTINEL]) * FRAMEBUFFER_SIZE,
                )
                record_offset = self.artifacts.symbols["entry_record"]
                self.assertEqual(
                    result.read_record(),
                    self.artifacts.image[record_offset : record_offset + RECORD_SIZE],
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
