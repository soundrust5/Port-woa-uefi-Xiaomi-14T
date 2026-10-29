"""Meaningful fail-closed offline cases; no hardware interaction."""
from dataclasses import replace
import unittest
from model import Window, active_hz, plan, recovery_after_enable

BASE = Window(0x41, 1 << 31, 0, 1, 0, 0, 100, 13_000_100,
              1000, 131000, 13_000_000)
LUT = [2200000, 2100000, 2000000, 1900000, 1800000, 1700000,
       1600000, 1500000, 1400000, 1300000, 1200000, 1100000,
       1000000, 900000, 800000, 700000, 600000, 480000]


class GateTests(unittest.TestCase):
    def inputs(self):
        return dict(mode=1, enable=0x101, request=9, ready=3, lut_khz=LUT,
                    windows=[BASE]*3, pmu_reads_completed=True,
                    domain0_pe_confirmed=True, thermal_gate=True,
                    thermal_evidence='host simulation only', ceiling_khz=1400000)

    def test_plan_and_restore(self):
        commands = plan(**self.inputs())
        self.assertEqual(commands[-2], ('domain0_request_khz', 1300000))
        self.assertEqual(recovery_after_enable(), [('domain0_request_khz', 1300000)])
        self.assertEqual(max(v for k, v in commands if k == 'domain0_request_khz'), 1400000)

    def test_lc_ignores_divider(self):
        self.assertEqual(active_hz(replace(BASE, pmcr=0x49)), 1300000000)

    def test_disabled_filtered_frozen_migrated(self):
        changes = [dict(pmcr=0x40), dict(pmcntenset=0), dict(pmcr=1),
                   dict(pmcr=0x61), dict(pmcr=0x241), dict(pmccfiltr=1<<31),
                   dict(current_el=2), dict(mpidr_after=1),
                   dict(counter_after=100), dict(counter_after=50)]
        for change in changes:
            with self.subTest(change=change), self.assertRaises(ValueError):
                active_hz(replace(BASE, **change))

    def test_missing_gate_cannot_plan_write(self):
        changes = [dict(pmu_reads_completed=False), dict(domain0_pe_confirmed=False),
                   dict(thermal_gate=False), dict(thermal_evidence=''),
                   dict(ceiling_khz=1300000), dict(request=10), dict(mode=0),
                   dict(enable=1), dict(ready=1), dict(windows=[BASE]),
                   dict(windows=[BASE, BASE, replace(BASE, pmcr=0x49)])]
        for change in changes:
            inp = self.inputs(); inp.update(change)
            with self.subTest(change=change), self.assertRaises(ValueError):
                plan(**inp)

    def test_counter_wrap(self):
        start = (1 << 64) - 100
        self.assertEqual(active_hz(replace(BASE, counter_before=start,
                                          counter_after=12999900)), 1300000000)


if __name__ == '__main__':
    unittest.main()
