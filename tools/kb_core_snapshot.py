#!/usr/bin/env python3
"""The pinned copy of kb-core in third_party/kb-core: refresh it from a kb-core checkout, or check it.

kb-core is a private repository of another organisation, so this repository's CI cannot fetch it. The port
therefore carries an unchanged copy of the kb-core files it builds against, at one recorded commit. Nothing in
third_party/kb-core is ever edited by hand: CMake re-hashes every file at configure time against
third_party/kb-core.lock (any difference stops the build), and this tool is the only writer.

    python3 tools/kb_core_snapshot.py --check                     # the copy equals the lock (CI and pre-flight)
    python3 tools/kb_core_snapshot.py --update /path/to/kb-core   # copy from a checkout at its HEAD commit

--update refuses a commit that is not on kb-core's origin/main. This copy is the interim method while kb-core is
private and untagged; docs/kb-core-dependency.md is the plan that replaces it (a release tag, find_package or
FetchContent, no copy).

The lock records the source repository, the commit and the SHA-256 of every file.

SPDX-License-Identifier: MIT
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEST = ROOT / "third_party" / "kb-core"
LOCK = ROOT / "third_party" / "kb-core.lock"
REPO = "https://github.com/kokeb-labs/kb-core"
# The branch the copied commit must be on: the port never builds against an unmerged kb-core state.
BRANCH = "main"
# What the port builds against: the CMake entry and modules, the three parts it uses, the RTOS-free and FreeRTOS
# backends, the host test port and the test harness. Documentation, drivers and the other parts are not copied.
PATHS = (
    "LICENSE",
    "CMakeLists.txt",
    # The whole cmake/ folder: the root CMakeLists.txt includes modules from it by name, and a hand-kept list of
    # them broke when kb-core added one (cmake/kb_tool_tests.cmake).
    "cmake",
    "lib/utils/CMakeLists.txt",
    "lib/utils/inc",
    "lib/rtos/CMakeLists.txt",
    "lib/rtos/inc",
    "lib/rtos/backends/none",
    "lib/rtos/backends/freertos",
    "lib/interfaces/CMakeLists.txt",
    "lib/interfaces/inc",
    "lib/interfaces/host",
    "host/rtos",
    "tests/harness",
)
SKIP_NAMES = {"README.md"}


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def files_under(base: Path) -> list[str]:
    out: list[str] = []
    for rel in PATHS:
        p = base / rel
        if p.is_file():
            out.append(rel)
        elif p.is_dir():
            out.extend(str(f.relative_to(base)) for f in sorted(p.rglob("*")) if f.is_file()
                       and f.name not in SKIP_NAMES)
        else:
            raise SystemExit(f"kb_core_snapshot: {rel} is missing in {base}")
    return sorted(out)


def read_lock() -> tuple[str, dict[str, str]]:
    commit = ""
    entries: dict[str, str] = {}
    for line in LOCK.read_text(encoding="utf-8").splitlines():
        if line.startswith("commit "):
            commit = line.split()[1]
        elif line and not line.startswith(("#", "repo ", "branch ")):
            digest, rel = line.split(None, 1)
            entries[rel] = digest
    return commit, entries


def check() -> int:
    commit, entries = read_lock()
    present = sorted(str(f.relative_to(DEST)) for f in DEST.rglob("*") if f.is_file())
    bad = [rel for rel, digest in entries.items() if not (DEST / rel).is_file() or sha256(DEST / rel) != digest]
    extra = [rel for rel in present if rel not in entries]
    for rel in bad:
        print(f"third_party/kb-core/{rel}: differs from the lock (or is missing)")
    for rel in extra:
        print(f"third_party/kb-core/{rel}: not in the lock")
    if bad or extra:
        return 1
    print(f"kb-core copy: {len(entries)} files equal the lock at {commit[:12]}")
    return 0


def update(src: Path) -> int:
    commit = subprocess.run(["git", "-C", str(src), "rev-parse", "HEAD"], check=True, capture_output=True,
                            text=True).stdout.strip()
    on_main = subprocess.run(["git", "-C", str(src), "merge-base", "--is-ancestor", commit, f"origin/{BRANCH}"],
                             capture_output=True, text=True)
    if on_main.returncode != 0:
        raise SystemExit(f"kb_core_snapshot: {commit[:12]} is not on kb-core origin/{BRANCH} (fetch, then check out a "
                         f"commit of {BRANCH})")
    dirty = subprocess.run(["git", "-C", str(src), "status", "--porcelain", "--", *PATHS], check=True,
                           capture_output=True, text=True).stdout.strip()
    if dirty:
        raise SystemExit("kb_core_snapshot: the checkout has local changes in the copied paths")
    rels = files_under(src)
    if DEST.exists():
        shutil.rmtree(DEST)
    lines = ["# The kb-core files in third_party/kb-core, written by tools/kb_core_snapshot.py --update. Never edit.",
             f"repo {REPO}", f"branch {BRANCH}", f"commit {commit}"]
    for rel in rels:
        (DEST / rel).parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src / rel, DEST / rel)
        lines.append(f"{sha256(DEST / rel)}  {rel}")
    LOCK.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"kb-core copy: {len(rels)} files at {commit[:12]}")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--check", action="store_true")
    ap.add_argument("--update", metavar="KB_CORE_CHECKOUT")
    a = ap.parse_args()
    if a.update:
        return update(Path(a.update).resolve())
    return check()


if __name__ == "__main__":
    sys.exit(main())
