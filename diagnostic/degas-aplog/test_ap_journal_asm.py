"""Assemble the current reference writer and emulate it; no device operations."""
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import unittest

import ap_journal_v1 as j
from test_ap_journal_v1 import JournalTests

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "artifacts/degas/cpu-audit/test-deps"))
from unicorn import Uc, UC_ARCH_ARM64, UC_MODE_ARM, UC_HOOK_MEM_WRITE
from unicorn.arm64_const import *


def tool_args(name, *args):
    return (["wsl.exe", "--exec"] if os.name == "nt" else []) + [name, *args]


def tool_path(path):
    path = Path(path).resolve()
    if os.name == "nt":
        return "/mnt/" + path.drive[0].lower() + path.as_posix()[2:]
    return str(path)


class AssemblyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="degas-ap-journal-")
        out = Path(cls.temporary.name)
        source = Path(__file__).with_name("ap_journal_commit.S")
        subprocess.run(tool_args("clang", "--target=aarch64-none-elf", "-march=armv8-a",
                                 "-nostdlib", "-fuse-ld=lld", "-Wl,-Ttext=0", "-Wl,--image-base=0", "-Wl,--build-id=none",
                                 "-Wl,-e,DegasApJournalCommit", tool_path(source),
                                 "-o", tool_path(out / "writer.elf")), check=True,
                       capture_output=True)
        subprocess.run(tool_args("llvm-objcopy", "-O", "binary",
                                 tool_path(out / "writer.elf"), tool_path(out / "writer.bin")),
                       check=True, capture_output=True)
        symbols = subprocess.run(tool_args("llvm-nm", "-n", tool_path(out / "writer.elf")),
                                 check=True, capture_output=True, text=True).stdout
        cls.symbols = {line.split()[2]: int(line.split()[0], 16)
                       for line in symbols.splitlines() if len(line.split()) == 3}
        cls.code = (out / "writer.bin").read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_code_budget(self):
        self.assertEqual(self.symbols["DegasApJournalCommitEnd"] -
                         self.symbols["DegasApJournalCommit"], 120)
        self.assertEqual(self.symbols["DegasApJournalCrc32End"] -
                         self.symbols["DegasApJournalCrc32"], 64)

    def test_exact_writes_crc_registers_and_read_only_mailbox(self):
        for cpu, origin in [(1, 0x43000000), (7, 0x140000000)]:
            with self.subTest(cpu=cpu, origin=origin):
                fixture = JournalTests()
                fixture.setUp()
                u = Uc(UC_ARCH_ARM64, UC_MODE_ARM)
                mailbox = 0xD0000000 + cpu * 0x1000
                slot = j.ZONE_BASE + cpu * j.SLOT_BYTES
                u.mem_map(origin, 0x2000)
                u.mem_map(mailbox, 0x1000)
                u.mem_map(j.ZONE_BASE, j.ZONE_BYTES)
                u.mem_write(origin, self.code)
                u.mem_write(j.ZONE_BASE, bytes(fixture.zone))
                snap = [0x1122334455667700 + k for k in range(8)]
                telemetry = [1, 0x81000000 | cpu << 8, 0x30D00800, 4, cpu << 8,
                             0xFFFFF80000010000, 1, 0]
                context = [0xFFFF800000123000, 0xFFFF800000456000, cpu, cpu << 8, 0]
                u.mem_write(mailbox, bytes([0xA5]) * 0x1000)
                u.mem_write(mailbox + 0xE00, struct.pack("<8Q", *snap))
                u.mem_write(mailbox + 0xF00, struct.pack("<8Q", *telemetry))
                before_mailbox = bytes(u.mem_read(mailbox, 0x1000))
                registers = [globals()[f"UC_ARM64_REG_X{k}"] for k in range(31)]
                for k, register in enumerate(registers):
                    u.reg_write(register, 0xABCD0000 + k)
                inputs = [slot, mailbox, *context]
                for register, value in zip(registers, inputs):
                    u.reg_write(register, value)
                u.reg_write(UC_ARM64_REG_X30, origin + 0x1000)
                u.reg_write(UC_ARM64_REG_SP, 0xBAD00000)
                preserved = {register: u.reg_read(register)
                             for register in registers[:7] + registers[14:30] + [UC_ARM64_REG_SP]}
                writes = []
                def track(uc, access, address, size, value, data):
                    self.assertGreaterEqual(address, slot)
                    self.assertLessEqual(address + size, slot + j.SLOT_BYTES)
                    writes.append((address, size))
                u.hook_add(UC_HOOK_MEM_WRITE, track)
                u.emu_start(origin, origin + 0x1000, count=100000)
                self.assertEqual(u.reg_read(UC_ARM64_REG_PC), origin + 0x1000)
                self.assertEqual(writes[-1], (slot, 8), "commit must be one final aligned store")
                self.assertEqual(bytes(u.mem_read(mailbox, 0x1000)), before_mailbox)
                for register, value in preserved.items():
                    self.assertEqual(u.reg_read(register), value)
                j.commit_reference_slot(fixture.zone, cpu, snap, telemetry, context)
                actual = bytes(u.mem_read(j.ZONE_BASE, j.ZONE_BYTES))
                self.assertEqual(actual, bytes(fixture.zone))
                record = j.decode(actual, fixture.attempt)["records"][cpu - 1]
                self.assertTrue(record["valid"], record)


if __name__ == "__main__":
    unittest.main()
