import copy
import json
from pathlib import Path
import sys
import unittest
import struct

sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import generate_degas_acpi as acpi
from prepare_degas_windows_facts import extract,DT,HISTORY


class AcpiEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.facts=extract(json.loads(DT.read_text()),json.loads(HISTORY.read_text()))
        self.log=acpi.LOG.read_text()

    def test_verified_boot_and_affinity_flags(self):
        boot=acpi.boot_evidence(self.log,self.facts)
        self.assertEqual(boot['mpidr'],0x81000000)
        self.assertEqual(boot['affinity'],0)
        self.assertEqual(boot['gicr'],0xc440000)
        self.assertEqual(len(acpi.madt(self.facts,boot)),148)

    def test_reject_ambiguous_or_mismatched_runtime(self):
        for log in [self.log+self.log, self.log.replace('boot_match=1','boot_match=0'),
                    self.log.replace('cntfrq=13000000','cntfrq=19200000'),
                    self.log.replace('first=0c440000','first=0c460000')]:
            with self.subTest(log=log[-40:]):
                with self.assertRaises(ValueError): acpi.boot_evidence(log,self.facts)

    def test_tables_are_deterministic_and_checksummed(self):
        boot=acpi.boot_evidence(self.log,self.facts)
        for name,data in [('Fadt',acpi.fadt()),('Madt',acpi.madt(self.facts,boot)),
                          ('Gtdt',acpi.gtdt_draft(self.facts))]:
            self.assertEqual(sum(data)&255,0)
            self.assertEqual(data,(acpi.ROOT/'artifacts/degas-acpi-v1/tables'/f'{name}.bin').read_bytes())

    def test_eight_cpu_template(self):
        boot=acpi.boot_evidence(self.log,self.facts)
        data=acpi.madt(self.facts,boot,True)
        self.assertEqual(len(data),708);self.assertEqual(sum(data)&255,0)
        for uid in range(8):
            base=44+80*uid
            self.assertEqual(data[base:base+2],bytes([11,80]))
            self.assertEqual(struct.unpack_from('<III',data,base+4),(0,uid,1))
            self.assertEqual(struct.unpack_from('<Q',data,base+68)[0],int(self.facts['cpus'][uid]['mpidr'],0))
            self.assertEqual(struct.unpack_from('<Q',data,base+60)[0],boot['gicr'] if uid==0 else 0)
            self.assertEqual(data[base+76],0 if uid<4 else 1)
        source=acpi.dsdt_source(True,json.loads(DT.read_text()),True,True)
        for uid in range(8):self.assertIn('Device (C%03X)'%uid,source)
        self.assertIn('"DGS0001"',source);self.assertIn('"DGS0002"',source)


if __name__=='__main__': unittest.main()
