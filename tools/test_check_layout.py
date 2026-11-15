#!/usr/bin/env python3
"""Tests of tools/check_layout.py on small trees: the good layout passes, each way back to a combined file fails.
SPDX-License-Identifier: MIT
Copyright (c) 2026 The port-stm32 authors
"""
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import check_layout  # noqa: E402

LAYOUT = """\
header_only:
  version: constants only, checked in port.cpp
internal:
  sdk: the seam
boundary_extra:
  no_can: the stand-in for can.cpp
"""
BOUNDARY = "boundary_files:\n" + "".join(f"  - path: src/boundary/{s}\n" for s in ("adc.cpp", "can.cpp", "no_can.cpp"))
FILES = {
    "inc/kb/port/stm32/adc.hpp": "", "inc/kb/port/stm32/can.hpp": "", "inc/kb/port/stm32/version.hpp": "",
    "src/adc.cpp": "", "src/can.cpp": "", "src/sdk.hpp": "",
    "src/boundary/adc.cpp": "", "src/boundary/can.cpp": "", "src/boundary/no_can.cpp": "",
    "tools/layout.yml": LAYOUT, "deviations/boundary-files.yml": BOUNDARY,
}


class CheckLayout(unittest.TestCase):
    def run_tree(self, add=None, remove=(), layout=None, boundary=None):
        files = dict(FILES)
        files.update(add or {})
        for name in remove:
            files.pop(name)
        if layout is not None:
            files["tools/layout.yml"] = layout
        if boundary is not None:
            files["deviations/boundary-files.yml"] = boundary
        with tempfile.TemporaryDirectory() as tmp:
            for name, text in files.items():
                path = pathlib.Path(tmp) / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(text)
            return check_layout.check(pathlib.Path(tmp))

    def test_good_tree_passes(self):
        self.assertEqual(self.run_tree(), [])

    def test_header_without_source_fails(self):
        errors = self.run_tree(remove=["src/adc.cpp"])
        self.assertTrue(any("adc.hpp has no src/adc.cpp" in e for e in errors), errors)

    def test_combined_source_fails(self):
        errors = self.run_tree(remove=["src/adc.cpp", "src/can.cpp"], add={"src/adc_can.cpp": ""})
        self.assertTrue(any("adc_can" in e for e in errors), errors)

    def test_combined_boundary_file_fails(self):
        errors = self.run_tree(add={"src/boundary/adc_can.cpp": ""})
        self.assertTrue(any("adc_can" in e for e in errors), errors)

    def test_unlisted_internal_file_fails(self):
        errors = self.run_tree(add={"src/helpers.cpp": ""})
        self.assertTrue(any("helpers" in e for e in errors), errors)

    def test_header_only_entry_with_a_source_fails(self):
        errors = self.run_tree(add={"src/version.cpp": ""})
        self.assertTrue(any("header_only.version" in e for e in errors), errors)

    def test_stale_entry_fails(self):
        errors = self.run_tree(layout=LAYOUT + "  gone: no longer there\n")
        self.assertTrue(any("boundary_extra.gone" in e for e in errors), errors)

    def test_entry_without_reason_fails(self):
        errors = self.run_tree(layout=LAYOUT.replace("constants only, checked in port.cpp", '""'))
        self.assertTrue(any("no reason" in e for e in errors), errors)

    def test_boundary_list_must_match_disk(self):
        errors = self.run_tree(boundary=BOUNDARY + "  - path: src/boundary/gpio.cpp\n")
        self.assertTrue(any("boundary-files.yml" in e for e in errors), errors)

    def test_this_repository_passes(self):
        root = pathlib.Path(__file__).resolve().parent.parent
        self.assertEqual(check_layout.check(root), [])


if __name__ == "__main__":
    unittest.main()
