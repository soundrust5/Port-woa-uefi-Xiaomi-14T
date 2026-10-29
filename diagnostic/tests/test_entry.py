"""Host-only Unicorn tests for the position-independent AArch64 entry probe.

Unicorn does not model a change to CurrentEL from PSTATE reliably.  The tests
therefore intercept MRS and supply explicit system-register values.  This
checks the entry code's control flow and memory effects; it is not evidence of
the privilege or cache behaviour of the Xiaomi 14T hardware.
"""

from __future__ import annotations

import faulthandler
import json
from pathlib import Path
import struct
import sys
import unittest


REPO_ROOT = Path(__file__).resolve().parents[2]
VENDORED_PYTHON = REPO_ROOT / "tools" / "vendor" / "python"
sys.path.insert(0, str(VENDORED_PYTHON))

from elftools.elf.elffile import ELFFile  # noqa: E402
from unicorn import (  # noqa: E402
    UC_ARCH_ARM64,
    UC_HOOK_CODE,
    UC_HOOK_INSN,
    UC_HOOK_MEM_WRITE,
    UC_HOOK_MEM_WRITE_UNMAPPED,
    UC_MODE_ARM,
    Uc,
)
from unicorn.arm64_const import (  # noqa: E402
    UC_ARM64_INS_MRS,
    UC_ARM64_REG_PC,
    UC_ARM64_REG_SP,
    UC_ARM64_REG_X0,
    UC_ARM64_REG_X1,
    UC_ARM64_REG_X2,
    UC_ARM64_REG_X3,
)


BUILD_DIR = REPO_ROOT / "diagnostic" / "build"
ELF_PATH = BUILD_DIR / "entry.elf"
BIN_PATH = BUILD_DIR / "entry.bin"
MANIFEST_PATH = BUILD_DIR / "manifest.json"

PAGE_SIZE = 0x1000
RECORD_SIZE = 160
STACK_SIZE = 16 * 1024
MAGIC = b"DEGASD01"
# Cross the 4-GiB boundary so relocation tests also catch address truncation.
LOAD_BASES = (0x40000000, 0x100000000)
FRAMEBUFFER_BASE = 0xFCC2F000
FRAMEBUFFER_END = 0xFFFFF000

# (op0, op1, CRn, CRm, op2), per the Arm system-register encoding.
DAIF = (3, 3, 4, 2, 1)
CURRENT_EL = (3, 0, 4, 2, 2)
SCTLR_EL1 = (3, 0, 1, 0, 0)
TCR_EL1 = (3, 0, 2, 0, 2)
TTBR0_EL1 = (3, 0, 2, 0, 0)
MAIR_EL1 = (3, 0, 10, 2, 0)
SCTLR_EL2 = (3, 4, 1, 0, 0)
TCR_EL2 = (3, 4, 2, 0, 2)
TTBR0_EL2 = (3, 4, 2, 0, 0)
MAIR_EL2 = (3, 4, 10, 2, 0)

ARGUMENTS = (
    0x0123456789ABCDEF,
    0xFEDCBA9876543210,
    0x1122334455667788,
    0x8877665544332211,
)

# Retaining the small engines also keeps their bound hook callbacks alive until
# interpreter shutdown on the Windows Unicorn binding.
_LIVE_EMULATIONS: list["EmulationResult"] = []

def _align_up(value: int, alignment: int) -> int:
    return (value + alignment - 1) & -alignment


def _u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def _u64(data: bytes, offset: int) -> int:
    return struct.unpack_from("<Q", data, offset)[0]


class EntryArtifacts:
    REQUIRED_SYMBOLS = (
        "_start",
        "entry_record",
        "diagnostic_park",
        "diagnostic_unsupported",
        "__image_end",
    )

    def __init__(self) -> None:
        if not ELF_PATH.is_file() or not BIN_PATH.is_file():
            raise AssertionError(
                "missing diagnostic build artifacts; run diagnostic/build.py first"
            )

        self.image = BIN_PATH.read_bytes()
        with ELF_PATH.open("rb") as stream:
            elf = ELFFile(stream)
            self.machine = elf["e_machine"]
            self.elf_type = elf["e_type"]
            self.entry = elf["e_entry"]
            symbol_table = elf.get_section_by_name(".symtab")
            if symbol_table is None:
                raise AssertionError("entry.elf has no .symtab")

            self.symbols: dict[str, int] = {}
            self.symbol_sections: dict[str, int | str] = {}
            for name in self.REQUIRED_SYMBOLS:
                matches = symbol_table.get_symbol_by_name(name) or []
                if len(matches) != 1:
                    raise AssertionError(f"expected exactly one ELF symbol {name!r}")
                self.symbols[name] = int(matches[0]["st_value"])
                self.symbol_sections[name] = matches[0]["st_shndx"]

            record_section_index = self.symbol_sections["entry_record"]
            record_section = elf.get_section(record_section_index)
            self.record_section_writable = bool(record_section["sh_flags"] & 1)

            self.load_segments = []
            for segment in elf.iter_segments():
                if segment["p_type"] == "PT_LOAD":
                    self.load_segments.append(
                        {
                            "vaddr": int(segment["p_vaddr"]),
                            "paddr": int(segment["p_paddr"]),
                            "filesz": int(segment["p_filesz"]),
                            "memsz": int(segment["p_memsz"]),
                            "data": segment.data(),
                        }
                    )


class EmulationResult:
    def __init__(
        self,
        artifacts: EntryArtifacts,
        base: int,
        system_registers: dict[tuple[int, int, int, int, int], int],
    ) -> None:
        self.artifacts = artifacts
        self.base = base
        self.system_registers = system_registers
        self.system_reads: list[tuple[int, int, int, int, int]] = []
        self.unknown_system_reads: list[tuple[int, int, int, int, int]] = []
        self.writes: list[tuple[int, int, int]] = []
        self.stop_symbol: str | None = None

        image = artifacts.image
        self.image_start = base
        self.image_end = base + len(image)
        self.uc = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
        # Unicorn's Windows memory mapper uses a handled access violation that
        # pytest's faulthandler reports as a fatal exception even though the API
        # succeeds.  Suppress tracing only for this native call.
        faulthandler_was_enabled = faulthandler.is_enabled()
        if faulthandler_was_enabled:
            faulthandler.disable()
        try:
            self.uc.mem_map(base, _align_up(len(image), PAGE_SIZE))
        finally:
            if faulthandler_was_enabled:
                faulthandler.enable()
        self.uc.mem_write(base, image)

        self.uc.hook_add(UC_HOOK_INSN, self._hook_mrs, None, 1, 0, UC_ARM64_INS_MRS)
        self.uc.hook_add(UC_HOOK_CODE, self._hook_code)
        # Unicorn uses different callback ABIs for mapped and unmapped writes,
        # so these must be installed as two hooks rather than one bit mask.
        self.uc.hook_add(UC_HOOK_MEM_WRITE, self._hook_write)
        self.uc.hook_add(UC_HOOK_MEM_WRITE_UNMAPPED, self._hook_invalid_write)
        _LIVE_EMULATIONS.append(self)
        self.prepare(base, system_registers)

    def prepare(
        self,
        base: int,
        system_registers: dict[tuple[int, int, int, int, int], int],
    ) -> "EmulationResult":
        self.base = base
        self.image_start = base
        self.image_end = base + len(self.artifacts.image)
        self.system_registers = system_registers
        self.system_reads.clear()
        self.unknown_system_reads.clear()
        self.stop_symbol = None

        # Restore the image and record after the preceding scenario.  Unicorn's
        # API write does not invoke emulated-memory hooks; clear defensively.
        self.uc.mem_write(base, self.artifacts.image)
        self.writes.clear()
        for register, value in zip(
            (UC_ARM64_REG_X0, UC_ARM64_REG_X1, UC_ARM64_REG_X2, UC_ARM64_REG_X3),
            ARGUMENTS,
        ):
            self.uc.reg_write(register, value)

        # Deliberately outside the mapped image: entry code must only capture
        # this value before switching to its internal stack.
        self.original_sp = _align_up(self.image_end + 0x20000, 16)
        self.uc.reg_write(UC_ARM64_REG_SP, self.original_sp)
        return self

    def _hook_mrs(self, uc: Uc, register: int, cp_reg, _user_data) -> bool:
        key = (cp_reg.op0, cp_reg.op1, cp_reg.crn, cp_reg.crm, cp_reg.op2)
        self.system_reads.append(key)
        if key not in self.system_registers:
            self.unknown_system_reads.append(key)
            uc.emu_stop()
            return True
        uc.reg_write(register, self.system_registers[key])
        # The bundled backend repeatedly re-enters EL2 MRS instructions because
        # its CPU stays at EL1 even when the MRS callback handles the value.
        # Advance over those intercepted instructions explicitly.
        if key[1] == 4:
            uc.reg_write(UC_ARM64_REG_PC, uc.reg_read(UC_ARM64_REG_PC) + 4)
        return True  # Skip Unicorn's incomplete default system-register model.

    def _hook_code(self, uc: Uc, address: int, _size: int, _user_data) -> None:
        offsets = self.artifacts.symbols
        if address == self.base + offsets["diagnostic_park"]:
            self.stop_symbol = "diagnostic_park"
            uc.emu_stop()
        elif address == self.base + offsets["diagnostic_unsupported"]:
            self.stop_symbol = "diagnostic_unsupported"
            uc.emu_stop()

    def _hook_write(
        self, _uc: Uc, _access: int, address: int, size: int, value: int, _user_data
    ) -> None:
        self.writes.append((address, size, value))

    def _hook_invalid_write(
        self, _uc: Uc, _access: int, address: int, size: int, value: int, _user_data
    ) -> bool:
        self.writes.append((address, size, value))
        return False

    def run(self) -> "EmulationResult":
        self.uc.emu_start(
            self.base + self.artifacts.symbols["_start"],
            self.image_end,
            count=256,
        )
        return self

    def read_record(self) -> bytes:
        address = self.base + self.artifacts.symbols["entry_record"]
        return bytes(self.uc.mem_read(address, RECORD_SIZE))


class EntryProbeTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.artifacts = EntryArtifacts()

    def test_elf_and_linux_image_header_contract(self) -> None:
        artifact = self.artifacts
        image = artifact.image
        symbols = artifact.symbols

        self.assertEqual(artifact.machine, "EM_AARCH64")
        self.assertEqual(artifact.elf_type, "ET_EXEC")
        self.assertEqual(artifact.entry, 0)
        self.assertEqual(symbols["_start"], 0)
        self.assertEqual(symbols["__image_end"], len(image))
        self.assertTrue(artifact.record_section_writable)

        branch = _u32(image, 0)
        self.assertEqual(branch & 0xFC000000, 0x14000000)
        displacement = branch & 0x03FFFFFF
        if displacement & 0x02000000:
            displacement -= 1 << 26
        branch_target = displacement << 2
        self.assertEqual(branch_target, 0x40)  # The fixed-size Image header.

        self.assertEqual(_u32(image, 4), 0)
        self.assertEqual(_u64(image, 8), 0)  # text_offset
        self.assertEqual(_u64(image, 16), len(image))  # image_size
        self.assertEqual(image[56:60], b"ARM\x64")
        self.assertEqual(_u32(image, 60), 0)

        record = symbols["entry_record"]
        self.assertGreaterEqual(len(image), record + RECORD_SIZE + STACK_SIZE)
        self.assertEqual(image[record : record + 8], b"\0" * 8)
        self.assertEqual(_u32(image, record + 8), 1)
        self.assertEqual(_u32(image, record + 12), RECORD_SIZE)
        self.assertEqual(image[-STACK_SIZE:], b"\0" * STACK_SIZE)

        self.assertTrue(artifact.load_segments)
        self.assertEqual(min(segment["vaddr"] for segment in artifact.load_segments), 0)
        self.assertEqual(
            max(segment["vaddr"] + segment["filesz"] for segment in artifact.load_segments),
            len(image),
        )
        for segment in artifact.load_segments:
            self.assertEqual(segment["vaddr"], segment["paddr"])
            self.assertEqual(segment["filesz"], segment["memsz"])
            start = segment["vaddr"]
            self.assertEqual(image[start : start + segment["filesz"]], segment["data"])

        manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
        self.assertEqual(image[record + 128 : record + 160].hex(), manifest["build_id"])

    def test_supported_el1_and_el2_capture_at_two_relocations(self) -> None:
        cases = {
            4: {
                SCTLR_EL1: 0xA5A5000000000200,
                TCR_EL1: 0x1111222233334444,
                TTBR0_EL1: 0x0000000076543000,
                MAIR_EL1: 0xFFEEDDCCBBAA0099,
            },
            8: {
                SCTLR_EL2: 0x5A5A000000000200,
                TCR_EL2: 0x9999AAAABBBBCCCC,
                TTBR0_EL2: 0x0000000123456000,
                MAIR_EL2: 0x0011223344556677,
            },
        }
        daif = 0x3C0

        for base in LOAD_BASES:
            for current_el, el_registers in cases.items():
                with self.subTest(base=hex(base), current_el=current_el):
                    registers = {DAIF: daif, CURRENT_EL: current_el, **el_registers}
                    result = EmulationResult(self.artifacts, base, registers).run()

                    self.assertEqual(result.stop_symbol, "diagnostic_park")
                    self.assertFalse(result.unknown_system_reads)
                    self.assertEqual(set(result.system_reads), set(registers))
                    self.assertTrue(result.writes)
                    self.assertTrue(
                        all(
                            result.image_start <= address
                            and address + size <= result.image_end
                            for address, size, _value in result.writes
                        )
                    )
                    self.assertTrue(
                        all(
                            address + size <= FRAMEBUFFER_BASE or address >= FRAMEBUFFER_END
                            for address, size, _value in result.writes
                        )
                    )

                    record = result.read_record()
                    self.assertEqual(record[0:8], MAGIC)
                    self.assertEqual(_u32(record, 8), 1)
                    self.assertEqual(_u32(record, 12), RECORD_SIZE)
                    self.assertEqual(_u64(record, 16), 1)
                    self.assertEqual(_u64(record, 24), current_el)
                    self.assertEqual(_u64(record, 32), result.original_sp)
                    self.assertEqual(_u64(record, 40), daif)

                    expected_values = list(el_registers.values())
                    self.assertEqual(_u64(record, 48), expected_values[0])
                    self.assertEqual(_u64(record, 56), expected_values[1])
                    self.assertEqual(_u64(record, 64), expected_values[2])
                    self.assertEqual(_u64(record, 72), expected_values[3])
                    self.assertEqual(
                        tuple(_u64(record, offset) for offset in (80, 88, 96, 104)),
                        ARGUMENTS,
                    )
                    self.assertEqual(_u64(record, 112), base)
                    self.assertEqual(_u64(record, 120), len(self.artifacts.image))
                    initial_record = self.artifacts.symbols["entry_record"]
                    self.assertEqual(
                        record[128:160],
                        self.artifacts.image[initial_record + 128 : initial_record + 160],
                    )

                    self.assertEqual(
                        tuple(
                            result.uc.reg_read(register)
                            for register in (
                                UC_ARM64_REG_X0,
                                UC_ARM64_REG_X1,
                                UC_ARM64_REG_X2,
                                UC_ARM64_REG_X3,
                            )
                        ),
                        ARGUMENTS,
                    )
                    final_sp = result.uc.reg_read(UC_ARM64_REG_SP)
                    self.assertEqual(final_sp & 0xF, 0)
                    self.assertGreaterEqual(final_sp, base)
                    self.assertLessEqual(final_sp, result.image_end)

                    record_address = base + self.artifacts.symbols["entry_record"]
                    magic_writes = [
                        index
                        for index, (address, size, _value) in enumerate(result.writes)
                        if address == record_address and size == 8
                    ]
                    stage_writes = [
                        index
                        for index, (address, size, _value) in enumerate(result.writes)
                        if address == record_address + 16 and size == 8
                    ]
                    self.assertEqual(len(magic_writes), 1)
                    self.assertEqual(len(stage_writes), 1)
                    self.assertGreater(magic_writes[0], stage_writes[0])
                    self.assertEqual(magic_writes[0], len(result.writes) - 1)

    def test_invalid_exception_levels_park_without_memory_writes(self) -> None:
        for base in LOAD_BASES:
            for current_el in (0, 12, 16):
                with self.subTest(base=hex(base), current_el=current_el):
                    result = EmulationResult(
                        self.artifacts,
                        base,
                        {DAIF: 0x3C0, CURRENT_EL: current_el},
                    ).run()
                    self.assertEqual(result.stop_symbol, "diagnostic_unsupported")
                    self.assertFalse(result.unknown_system_reads)
                    self.assertEqual(set(result.system_reads), {DAIF, CURRENT_EL})
                    self.assertEqual(result.writes, [])
                    self.assertEqual(
                        result.read_record(),
                        self.artifacts.image[
                            self.artifacts.symbols["entry_record"] :
                            self.artifacts.symbols["entry_record"] + RECORD_SIZE
                        ],
                    )

    def test_mmu_or_data_cache_enabled_parks_without_memory_writes(self) -> None:
        cases = (
            (4, SCTLR_EL1, 1 << 0),
            (4, SCTLR_EL1, 1 << 2),
            (4, SCTLR_EL1, (1 << 0) | (1 << 2)),
            (4, SCTLR_EL1, 1 << 25),
            (8, SCTLR_EL2, 1 << 0),
            (8, SCTLR_EL2, 1 << 2),
            (8, SCTLR_EL2, (1 << 0) | (1 << 2)),
            (8, SCTLR_EL2, 1 << 25),
        )
        for base in LOAD_BASES:
            for current_el, sctlr, value in cases:
                with self.subTest(base=hex(base), current_el=current_el, sctlr=hex(value)):
                    result = EmulationResult(
                        self.artifacts,
                        base,
                        {DAIF: 0xC0, CURRENT_EL: current_el, sctlr: value},
                    ).run()
                    self.assertEqual(result.stop_symbol, "diagnostic_unsupported")
                    self.assertFalse(result.unknown_system_reads)
                    self.assertEqual(set(result.system_reads), {DAIF, CURRENT_EL, sctlr})
                    self.assertEqual(result.writes, [])


if __name__ == "__main__":
    unittest.main(verbosity=2)
