"""Execute the complete framebuffer-marker image in Unicorn.

The MRS model is synthetic, as in test_entry.py.  These tests cover linked
entry control flow and bounded memory effects, not real panel or cache/MMIO
behaviour.
"""

from __future__ import annotations

import faulthandler
from pathlib import Path
import unittest

import test_entry as entry_test
from test_entry import (  # Reuse the entry probe's register model and helpers.
    CURRENT_EL,
    DAIF,
    LOAD_BASES,
    MAIR_EL1,
    MAIR_EL2,
    PAGE_SIZE,
    RECORD_SIZE,
    SCTLR_EL1,
    SCTLR_EL2,
    TCR_EL1,
    TCR_EL2,
    TTBR0_EL1,
    TTBR0_EL2,
    EntryArtifacts,
    EmulationResult,
    _u32,
    _u64,
)


MARKER_BUILD_DIR = Path(__file__).resolve().parents[1] / "build-marker"
MARKER_ELF_PATH = MARKER_BUILD_DIR / "entry.elf"
MARKER_BIN_PATH = MARKER_BUILD_DIR / "entry.bin"

FRAMEBUFFER_BASE = 0xFCC2F000
FRAMEBUFFER_SIZE = 0x00CF3000
FRAMEBUFFER_PITCH = 4992
MARKER_WIDTH = 640
MARKER_HEIGHT = 192
MARKER_BAND_HEIGHT = 64
MARKER_BIT_WIDTH = 20
SENTINEL = 0xA5
MAGENTA = 0xFFFF00FF
CYAN = 0xFF00FFFF
INSTRUCTION_LIMIT = 10_000_000


class MarkerEntryArtifacts(EntryArtifacts):
    REQUIRED_SYMBOLS = EntryArtifacts.REQUIRED_SYMBOLS + (
        "__stack_start",
        "__stack_end",
        "diagnostic_marker",
    )

    def __init__(self) -> None:
        # EntryArtifacts intentionally centralizes ELF parsing.  Override its
        # input paths only while it loads the separately linked marker image.
        old_elf, old_bin = entry_test.ELF_PATH, entry_test.BIN_PATH
        entry_test.ELF_PATH = MARKER_ELF_PATH
        entry_test.BIN_PATH = MARKER_BIN_PATH
        try:
            super().__init__()
        finally:
            entry_test.ELF_PATH, entry_test.BIN_PATH = old_elf, old_bin


class MarkerEmulationResult(EmulationResult):
    def __init__(self, *args, **kwargs) -> None:
        self.framebuffer_write_count = 0
        self.out_of_contract_writes: list[tuple[int, int, int]] = []
        super().__init__(*args, **kwargs)

        faulthandler_was_enabled = faulthandler.is_enabled()
        if faulthandler_was_enabled:
            faulthandler.disable()
        try:
            self.uc.mem_map(FRAMEBUFFER_BASE, FRAMEBUFFER_SIZE)
        finally:
            if faulthandler_was_enabled:
                faulthandler.enable()
        self.uc.mem_write(
            FRAMEBUFFER_BASE, bytes([SENTINEL]) * FRAMEBUFFER_SIZE
        )

    def prepare(self, *args, **kwargs) -> "MarkerEmulationResult":
        super().prepare(*args, **kwargs)
        self.framebuffer_write_count = 0
        self.out_of_contract_writes.clear()
        return self

    @staticmethod
    def _is_marker_write(address: int, size: int) -> bool:
        offset = address - FRAMEBUFFER_BASE
        if offset < 0 or offset + size > FRAMEBUFFER_SIZE:
            return False
        row, row_offset = divmod(offset, FRAMEBUFFER_PITCH)
        return row < MARKER_HEIGHT and row_offset + size <= MARKER_WIDTH * 4

    def _hook_write(
        self, _uc, _access: int, address: int, size: int, value: int, _user_data
    ) -> None:
        if self._is_marker_write(address, size):
            self.framebuffer_write_count += size
            return

        record_start = self.base + self.artifacts.symbols["entry_record"]
        stack_start = self.base + self.artifacts.symbols["__stack_start"]
        stack_end = self.base + self.artifacts.symbols["__stack_end"]
        if (
            record_start <= address
            and address + size <= record_start + RECORD_SIZE
        ) or (stack_start <= address and address + size <= stack_end):
            self.writes.append((address, size, value))
        else:
            self.out_of_contract_writes.append((address, size, value))

    def _hook_invalid_write(
        self, _uc, _access: int, address: int, size: int, value: int, _user_data
    ) -> bool:
        self.out_of_contract_writes.append((address, size, value))
        return False

    def run(self) -> "MarkerEmulationResult":
        self.uc.emu_start(
            self.base + self.artifacts.symbols["_start"],
            self.image_end,
            count=INSTRUCTION_LIMIT,
        )
        return self


def _pixel_bytes(color: int) -> bytes:
    return color.to_bytes(4, "little")


def _expected_marker_row(y: int, build_id: bytes) -> bytes:
    if y < MARKER_BAND_HEIGHT:
        return _pixel_bytes(MAGENTA) * MARKER_WIDTH
    if y < MARKER_BAND_HEIGHT * 2:
        return _pixel_bytes(CYAN) * MARKER_WIDTH

    pixels = bytearray()
    for x in range(MARKER_WIDTH):
        bit_index = x // MARKER_BIT_WIDTH
        mask = 1 << (7 - bit_index % 8)
        color = MAGENTA if build_id[bit_index // 8] & mask else CYAN
        pixels.extend(_pixel_bytes(color))
    return bytes(pixels)


class MarkerEntryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.artifacts = MarkerEntryArtifacts()

    def _supported_registers(self, current_el: int) -> dict:
        common = {DAIF: 0x3C0, CURRENT_EL: current_el}
        if current_el == 4:
            return {
                **common,
                SCTLR_EL1: 0,
                TCR_EL1: 0x1111222233334444,
                TTBR0_EL1: 0x76543000,
                MAIR_EL1: 0xFFEEDDCCBBAA0099,
            }
        return {
            **common,
            SCTLR_EL2: 0,
            TCR_EL2: 0x9999AAAABBBBCCCC,
            TTBR0_EL2: 0x123456000,
            MAIR_EL2: 0x0011223344556677,
        }

    def _assert_framebuffer(self, result: MarkerEmulationResult) -> None:
        record = result.read_record()
        build_id = record[128:160]
        sentinel_tail = bytes([SENTINEL]) * (FRAMEBUFFER_PITCH - MARKER_WIDTH * 4)

        for y in range(MARKER_HEIGHT):
            row = bytes(
                result.uc.mem_read(
                    FRAMEBUFFER_BASE + y * FRAMEBUFFER_PITCH,
                    FRAMEBUFFER_PITCH,
                )
            )
            self.assertEqual(row[: MARKER_WIDTH * 4], _expected_marker_row(y, build_id))
            self.assertEqual(row[MARKER_WIDTH * 4 :], sentinel_tail)

        untouched_start = FRAMEBUFFER_BASE + MARKER_HEIGHT * FRAMEBUFFER_PITCH
        untouched_size = FRAMEBUFFER_SIZE - MARKER_HEIGHT * FRAMEBUFFER_PITCH
        self.assertEqual(
            bytes(result.uc.mem_read(untouched_start, untouched_size)),
            bytes([SENTINEL]) * untouched_size,
        )

    def test_el1_and_el2_draw_marker_at_two_relocations(self) -> None:
        for base in LOAD_BASES:
            for current_el in (4, 8):
                with self.subTest(base=hex(base), current_el=current_el):
                    registers = self._supported_registers(current_el)
                    result = MarkerEmulationResult(
                        self.artifacts, base, registers
                    ).run()

                    self.assertEqual(result.stop_symbol, "diagnostic_park")
                    self.assertFalse(result.unknown_system_reads)
                    self.assertEqual(set(result.system_reads), set(registers))
                    self.assertFalse(result.out_of_contract_writes)
                    self.assertTrue(result.writes)  # Record and C call stack.
                    self.assertEqual(
                        result.framebuffer_write_count, MARKER_WIDTH * MARKER_HEIGHT * 4
                    )

                    record = result.read_record()
                    self.assertEqual(record[:8], b"DEGASD01")
                    self.assertEqual(_u32(record, 8), 1)
                    self.assertEqual(_u32(record, 12), RECORD_SIZE)
                    self.assertEqual(_u64(record, 16), 1)
                    self.assertEqual(_u64(record, 24), current_el)
                    self.assertEqual(_u64(record, 112), base)
                    self.assertEqual(_u64(record, 120), len(self.artifacts.image))
                    record_offset = self.artifacts.symbols["entry_record"]
                    self.assertEqual(
                        record[128:160],
                        self.artifacts.image[record_offset + 128 : record_offset + 160],
                    )
                    self._assert_framebuffer(result)

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
                result = MarkerEmulationResult(
                    self.artifacts, LOAD_BASES[0], registers
                ).run()

                self.assertEqual(result.stop_symbol, "diagnostic_unsupported")
                self.assertFalse(result.unknown_system_reads)
                self.assertEqual(result.writes, [])
                self.assertEqual(result.framebuffer_write_count, 0)
                self.assertEqual(result.out_of_contract_writes, [])
                self.assertEqual(
                    bytes(result.uc.mem_read(FRAMEBUFFER_BASE, FRAMEBUFFER_SIZE)),
                    bytes([SENTINEL]) * FRAMEBUFFER_SIZE,
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
