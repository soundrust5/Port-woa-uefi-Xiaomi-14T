#!/usr/bin/env python3
"""Exact semantic-diff test for patch_bcd_system_marker.py."""

from __future__ import annotations

from pathlib import Path
import tempfile
import unittest

from patch_bcd_system_marker import patch_copy, semantic_snapshot, SYSTEM_VALUE


REPO = Path(__file__).resolve().parents[1]
FIXTURE = REPO / "evidence/oneplus12/setup-bcd-20260925-003256/BCD"
FIXTURE_SHA256 = "0a96dc80de317efd226e03370fb62df127df1718c979e3b233f629045b68aab7"


class PatchBcdSystemMarkerTest(unittest.TestCase):
    def test_exactly_one_semantic_value_is_added(self) -> None:
        before = semantic_snapshot(FIXTURE)
        with tempfile.TemporaryDirectory() as temp_dir:
            output = Path(temp_dir) / "BCD.patched-copy"
            source_hash, _ = patch_copy(FIXTURE, output, FIXTURE_SHA256)
            after = semantic_snapshot(output)

        self.assertEqual(source_hash, FIXTURE_SHA256)
        expected = {key: values.copy() for key, values in before.items()}
        expected["System\\Description"]["System"] = SYSTEM_VALUE
        self.assertEqual(after, expected)


if __name__ == "__main__":
    unittest.main()
