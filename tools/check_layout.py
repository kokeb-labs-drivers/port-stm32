#!/usr/bin/env python3
"""Checks the source layout of the port: one source per public header, one boundary file per SDK component.

  tools/check_layout.py [--root DIR]

Rules (the allow-lists are in tools/layout.yml, every entry with its reason):
  1. every inc/kb/port/stm32/<stem>.hpp has src/<stem>.cpp, or <stem> is listed under header_only;
  2. every file of src/ (.cpp or .hpp) has the stem of a public header, or is listed under internal;
  3. every file of src/boundary/ has the stem of a public header (the SDK component it wraps), or is listed
     under boundary_extra;
  4. every allow-list entry names a file that exists, and a header_only entry has no source (so it is removed
     when the source is added);
  5. deviations/boundary-files.yml lists exactly the files of src/boundary/.
A file named after two components (adc_can.cpp, i2c_spi.cpp) matches no rule, so the combined-file
pattern cannot come back.
SPDX-License-Identifier: MIT
Copyright (c) 2026 The port-stm32 authors
"""
import argparse
import pathlib
import sys

import yaml

HEADERS = "inc/kb/port/stm32"


def check(root: pathlib.Path) -> list:
    cfg = yaml.safe_load((root / "tools" / "layout.yml").read_text()) or {}
    header_only = cfg.get("header_only") or {}
    internal = cfg.get("internal") or {}
    boundary_extra = cfg.get("boundary_extra") or {}
    errors = []
    for table, name in ((header_only, "header_only"), (internal, "internal"), (boundary_extra, "boundary_extra")):
        for stem, reason in table.items():
            if not str(reason or "").strip():
                errors.append(f"tools/layout.yml: {name}.{stem} has no reason")

    headers = {p.stem for p in (root / HEADERS).glob("*.hpp")}
    src = root / "src"
    logic = {p.stem for p in src.glob("*.cpp")}
    logic_all = logic | {p.stem for p in src.glob("*.hpp")}
    boundary = {p.stem for p in (src / "boundary").glob("*") if p.suffix in (".cpp", ".hpp")}

    for stem in sorted(headers):
        if stem not in logic and stem not in header_only:
            errors.append(f"{HEADERS}/{stem}.hpp has no src/{stem}.cpp (add it, or list {stem} under header_only "
                          f"in tools/layout.yml with a reason)")
    for stem in sorted(logic_all):
        if stem not in headers and stem not in internal:
            errors.append(f"src/{stem}.* matches no public header and is not listed under internal in "
                          f"tools/layout.yml (one source per header: split it by header)")
    for stem in sorted(boundary):
        if stem not in headers and stem not in boundary_extra:
            errors.append(f"src/boundary/{stem}.* matches no public header and is not listed under "
                          f"boundary_extra in tools/layout.yml (one file per SDK component, named after it)")
    for stem in sorted(header_only):
        if stem not in headers:
            errors.append(f"tools/layout.yml: header_only.{stem} names no public header")
        elif stem in logic:
            errors.append(f"tools/layout.yml: header_only.{stem} has src/{stem}.cpp now; remove the entry")
    for stem in sorted(internal):
        if stem not in logic_all:
            errors.append(f"tools/layout.yml: internal.{stem} names no file of src/")
    for stem in sorted(boundary_extra):
        if stem not in boundary:
            errors.append(f"tools/layout.yml: boundary_extra.{stem} names no file of src/boundary/")
    entries = (yaml.safe_load((root / "deviations" / "boundary-files.yml").read_text()) or {}).get("boundary_files", [])
    listed = {e["path"] for e in entries if str(e["path"]).startswith("src/boundary/")}
    on_disk = {p.relative_to(root).as_posix() for p in (src / "boundary").glob("*") if p.suffix in (".cpp", ".hpp")}
    if listed != on_disk:
        errors.append("deviations/boundary-files.yml and the files of src/boundary/ differ: "
                      f"only listed {sorted(listed - on_disk)}, only on disk {sorted(on_disk - listed)}")
    return errors


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--root", default=str(pathlib.Path(__file__).resolve().parent.parent))
    args = parser.parse_args(argv)
    errors = check(pathlib.Path(args.root))
    for e in errors:
        print(f"layout: {e}")
    if errors:
        return 1
    print("layout: every header has its source, every boundary file its SDK component")
    return 0


if __name__ == "__main__":
    sys.exit(main())
