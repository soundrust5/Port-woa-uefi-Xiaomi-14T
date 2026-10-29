"""Compile/run the actual BSP helper and cross-check its schema/dump in Python."""
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
import uuid

import ap_journal_v1 as j
from extract_console_journal import extract_frames
from test_ap_journal_asm import tool_args, tool_path

ROOT = Path(__file__).resolve().parents[2]


class BspTests(unittest.TestCase):
    def test_actual_helper_cross_language_and_backup(self):
        with tempfile.TemporaryDirectory(prefix="degas-apj-bsp-") as temporary:
            out = Path(temporary)
            stub = out / "Library"
            stub.mkdir()
            (stub / "SerialPortLib.h").write_text("// SerialPortWrite provided by host test.\n")
            binary = out / "helper"
            source = Path(__file__).with_name("test_ap_journal_bsp.c")
            subprocess.run(tool_args("clang", "-std=c11", "-Wall", "-Wextra",
                                     "-Wno-unused-function", "-Werror", "-I", tool_path(out),
                                     tool_path(source), "-o", tool_path(binary)),
                           check=True, capture_output=True)
            canary = ROOT / "artifacts/degas/aplog-retention-v1/zone.bin"
            subprocess.run(tool_args(tool_path(binary), tool_path(out / "zone.bin"),
                                     tool_path(out / "console.txt"), tool_path(canary)),
                           check=True, capture_output=True)
            raw = (out / "zone.bin").read_bytes()
            attempt = uuid.UUID(bytes=raw[0x50:0x60])
            result = j.decode(raw, attempt)
            self.assertEqual(result["flags"], 1)
            self.assertEqual(result["counter"], 2)
            self.assertEqual(result["identity_kind"], "source_manifest_sha256")
            self.assertFalse(result["complete"])
            self.assertEqual(result, j.decode(j.export_reference_pstore(raw), attempt))
            frames = list(extract_frames((out / "console.txt").read_bytes()))
            self.assertEqual(frames, [canary.read_bytes()])
            console = (out / "console.txt").read_bytes()
            self.assertEqual(list(extract_frames(console[:-4])), [])
            self.assertEqual(list(extract_frames(console.replace(b"DEGAS APJ RAW 0020", b"DEGAS APJ RAW 0030"))), [])
            manifest = json.loads((canary.parent / "manifest.json").read_text())
            retained = j.decode(frames[0], uuid.UUID(manifest["attempt"]),
                                bytes.fromhex(manifest["build_sha256"]))
            self.assertEqual(retained["flags"], 0)
            # Cross-check the firmware's committed-slot validator against an
            # AP record sealed by Python, including raw architectural MPIDR bits.
            committed = bytearray(canary.read_bytes())
            telemetry = [1, 0x81000100, 0x30D00800, 4, 0x100,
                         0xFFFFF80000010000, 1, 0]
            j.commit_reference_slot(committed, 1, list(range(8)), telemetry,
                                    [0xFFFF800000123000, 0xFFFF800000456000, 1, 0x100, 0])
            (out / "committed.bin").write_bytes(committed)
            subprocess.run(tool_args(tool_path(binary), tool_path(out / "zone.bin"),
                                     tool_path(out / "console.txt"), tool_path(out / "committed.bin")),
                           check=True, capture_output=True)
            self.assertEqual(list(extract_frames((out / "console.txt").read_bytes())),
                             [bytes(committed)])


if __name__ == "__main__":
    unittest.main()
