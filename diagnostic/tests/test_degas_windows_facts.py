"""Offline evidence parser and ACPI draft tests; no device access."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
import prepare_degas_windows_facts as facts


class FactsTest(unittest.TestCase):
    def setUp(self):
        self.dt = json.loads(facts.DT.read_text())
        self.history = json.loads(facts.HISTORY.read_text())

    def test_real_capture_and_cpu_reg_not_unit_address(self):
        report = facts.extract(self.dt, self.history)
        cpus = {c['path']: c['mpidr'] for c in report['cpus']}
        self.assertEqual(cpus['/cpus/cpu@100'], '0x400')
        self.assertEqual(cpus['/cpus/cpu@200'], '0x700')
        self.assertIsNone(report['gic']['per_cpu_redistributor_bases'])
        self.assertEqual(report['gic']['maintenance']['intid'], 25)
        self.assertEqual(report['timer']['dt_frequency_hz'], 13000000)
        self.assertGreater(report['candidate_bytes'], 8 * 1024**3)
        self.assertEqual(len(report['unresolved_dynamic_reservations']), 4)

    def test_candidates_exclude_all_reservations_and_stay_in_dram(self):
        report = facts.extract(self.dt, self.history)
        for a, b in facts.pairs(report['additional_ram_candidates']):
            self.assertEqual((a | b) & 4095, 0)
            self.assertTrue(0x40000000 <= a < b <= 0x340000000)
            for c, d in facts.pairs(report['exclusions']):
                self.assertFalse(a < d and c < b)
        # Linux labels the framebuffer as System RAM: independently guard it.
        self.assertTrue(any(r['source'] == 'active framebuffer' for r in report['exclusions']))

    def test_new_fdt_reservation_removes_candidate(self):
        initial = facts.extract(self.dt, self.history)
        a, b = max(facts.pairs(initial['additional_ram_candidates']), key=lambda r:r[1]-r[0])
        self.dt['memreserve'].append({'address': hex(a), 'size': '0x2000'})
        updated = facts.extract(self.dt, self.history)
        self.assertEqual(initial['candidate_bytes'] - updated['candidate_bytes'], 8192)

    def test_invalid_inputs_fail_closed(self):
        changes = [('/cpus/cpu@100', 'reg', '00000000'),
                   ('/interrupt-controller', 'reg', '1234'),
                   ('/psci', 'method', '68766300'),
                   ('/reserved-memory', 'ranges', '00000001'),
                   ('/timer', 'interrupts', '00')]
        for path, key, value in changes:
            with self.subTest(path=path, key=key):
                dt = copy.deepcopy(self.dt)
                dt['nodes'][path][key]['hex'] = value
                with self.assertRaises(ValueError):
                    facts.extract(dt, self.history)

    def test_gtdt_acpi_layout_and_checksum(self):
        data = facts.gtdt_draft(facts.extract(self.dt, self.history))
        self.assertEqual(len(data), 104)
        self.assertEqual(data[:4], b'GTDT')
        self.assertEqual(struct.unpack_from('<I', data, 4)[0], len(data))
        self.assertEqual(sum(data) & 255, 0)
        self.assertEqual(struct.unpack_from('<8I', data, 48), (29,0,30,0,27,0,26,0))
        self.assertEqual(struct.unpack_from('<Q', data, 80)[0], 0xffffffffffffffff)
        self.assertEqual(data[88:], bytes(16))


if __name__ == '__main__':
    unittest.main()
