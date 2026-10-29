"""Reference-format checks only; these do not demonstrate reset retention."""
import importlib.util
from pathlib import Path
import struct
import unittest
import uuid

SPEC = importlib.util.spec_from_file_location(
    "ap_journal_v1", Path(__file__).parent / "ap_journal_v1.py")
j = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(j)


class JournalTests(unittest.TestCase):
    def setUp(self):
        self.attempt = uuid.UUID("62ba6e15-f0df-48e7-88bd-16e704b3f059")
        self.build = bytes(range(32))
        self.zone = j.make_reference_zone(self.attempt, self.build, 0xD0000000)

    def commit(self, cpu):
        telemetry = [1, 0x81000000 | cpu << 8, 0x30D00800, 4, cpu << 8,
                     0xFFFFF80000100000, 1, 0]
        context = [0xFFFF800000123000, 0xFFFF800000456000, cpu, cpu << 8, 0]
        j.commit_reference_slot(self.zone, cpu, list(range(8)), telemetry, context)

    def test_pstore_export_and_raw_zone_match(self):
        for cpu in range(1, 8):
            self.commit(cpu)
        raw = j.decode(bytes(self.zone), self.attempt, self.build)
        exported = j.decode(j.export_reference_pstore(self.zone), self.attempt, self.build)
        self.assertEqual(raw, exported)
        self.assertTrue(raw["complete"])
        self.assertEqual(raw["records"][0]["snapshot"]["mailbox_e38"], "0x0000000000000007")

    def test_missing_and_torn_slots_are_independent(self):
        self.commit(1)
        self.commit(2)
        self.zone[0x240] ^= 1
        records = j.decode(bytes(self.zone), self.attempt)["records"]
        self.assertTrue(records[0]["valid"])
        self.assertEqual(records[1]["status"], "CRC mismatch")
        self.assertEqual(records[2]["status"], "not committed")
        self.zone[0x100:0x108] = bytes(8)
        self.assertEqual(j.decode(bytes(self.zone), self.attempt)["records"][0]["status"], "not committed")

    def test_publication_is_immutable(self):
        self.commit(1)
        with self.assertRaises(ValueError):
            self.commit(1)

    def test_wrong_attempt_and_build_are_rejected(self):
        with self.assertRaisesRegex(j.JournalError, "attempt"):
            j.decode(bytes(self.zone), uuid.UUID(int=0))
        with self.assertRaisesRegex(j.JournalError, "build"):
            j.decode(bytes(self.zone), self.attempt, bytes(32))

    def test_torn_global_header_and_wrong_zone_are_rejected(self):
        self.zone[j.HEADER_OFFSET + 0x20] ^= 1
        with self.assertRaisesRegex(j.JournalError, "CRC"):
            j.decode(bytes(self.zone), self.attempt)
        self.zone = j.make_reference_zone(self.attempt, self.build, 0xD0000000)
        struct.pack_into("<I", self.zone, 4, j.PAYLOAD_BYTES + 1)
        with self.assertRaisesRegex(j.JournalError, "metadata"):
            j.decode(bytes(self.zone), self.attempt)

    def test_wrong_affinity_is_rejected_even_with_valid_crc(self):
        self.commit(1)
        struct.pack_into("<Q", self.zone, 0x120, 0x200)
        j._seal(self.zone, 0x100, 0xF0, j.SLOT_MAGIC)
        self.assertIn("affinity", j.decode(bytes(self.zone), self.attempt)["records"][0]["status"])

    def test_boundaries_and_export_length(self):
        self.assertLess(12 + len(j.KMSG_HEADER), j.HEADER_OFFSET)
        self.assertEqual(j.HEADER_OFFSET + j.HEADER_BYTES, 0xC0)
        self.assertEqual(7 * j.SLOT_BYTES + j.SLOT_BYTES, j.END_OFFSET)
        self.assertEqual(j.ZONE_BASE + j.ZONE_BYTES, 0x480AF000)
        with self.assertRaises(j.JournalError):
            j.decode(j.export_reference_pstore(self.zone)[:-1], self.attempt)
        self.assertFalse(any(self.zone[j.END_OFFSET:]))

    def test_monitor_payload_is_not_reported_as_windows_ap_release(self):
        snapshot=[0,(3<<32)|1,0x5c,0x23,4,0xfffff7b840001860,0xd,13000000]
        telemetry=[1,0x81000200,0x30d00800,4,0x200,0x44101000,0,10]
        context=[0xfffff8037a200000,0xfffff8036fb39520,2,0x200,0x4243473100000000]
        j.commit_reference_slot(self.zone,2,snapshot,telemetry,context)
        result=j.decode(bytes(self.zone),self.attempt,self.build)['records'][1]
        self.assertTrue(result['valid'])
        self.assertEqual(result['record_kind'],'bugcheck_monitor_v1')
        self.assertEqual(result['context']['nt_base'],'0xfffff8037a200000')
        self.assertNotIn('initial_tpidr_el1',result['context'])
        self.assertEqual(result['bugcheck_monitor']['code'],'0x5c')
        self.assertEqual(result['bugcheck_monitor']['parameters'],['0x0000000000000023','0x0000000000000004','0xfffff7b840001860','0x000000000000000d'])
        self.assertEqual(result['telemetry']['os_release_observed'],0)


if __name__ == "__main__":
    unittest.main()
