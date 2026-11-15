#!/usr/bin/env bash
# Local pre-flight of the checks CI runs, so a push spends no CI minutes on a known failure.
#
#   tools/preflight.sh [--quick] [--no-tidy]
#
# Needs: cmake, ninja, a Clang 20 (CLANG, default clang++), a GCC with gcov (GCC, default g++; GCOV),
# clang-format and clang-tidy 20 (pip clang-format==20.* clang-tidy==20.*), gcovr 8.7 and lizard 1.24.1 (pip),
# python3 with PyYAML, and a ci-tools checkout (CI_TOOLS_DIR, for the handbook, layering and misra_lint include
# lists). Steps: kb-core copy, format, host tests (Clang strict and GCC -O2), ASan and UBSan, coverage at the
# high level's limits (lines 85 %, branches 75 %), complexity (CCN 15, 60 lines, 6 parameters), clang-tidy,
# misra_lint --strict, the handbook check, the layering check and the source layout check (tools/check_layout.py:
# one src/<stem>.cpp per public header, one src/boundary/<component>.cpp per HAL component). Builds go to
# $PREFLIGHT_DIR (default build-preflight) and are removed at the end unless KEEP=1. The firmware builds are docs/examples.md.
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The port-stm32 authors
set -uo pipefail
cd "$(dirname "$0")/.."
root="$(pwd)"
quick=0; tidy=1
for a in "$@"; do
  case "$a" in --quick) quick=1 ;; --no-tidy) tidy=0 ;; *) echo "unknown option $a"; exit 2 ;; esac
done
CLANG="${CLANG:-clang++}"; GCC="${GCC:-g++}"; GCOV="${GCOV:-gcov}"
out="${PREFLIGHT_DIR:-$root/build-preflight}"; mkdir -p "$out"
fails=()
step() { local name="$1"; shift; echo "== $name"; if "$@" >"$out/$name.log" 2>&1; then echo "   PASS"; else
  echo "   FAIL ($out/$name.log)"; tail -25 "$out/$name.log" | sed 's/^/   | /'; fails+=("$name"); fi; }

sources() { find inc src tests -name '*.cpp' -o -name '*.hpp' -o -name '*.h' | sort; }
library() { find inc src -name '*.cpp' -o -name '*.hpp' | sort; }
layout() { python3 tools/check_layout.py && python3 tools/test_check_layout.py; }

snapshot() { python3 tools/kb_core_snapshot.py --check; }
format() { sources | xargs clang-format --dry-run --Werror --style=file:_config/.clang-format; }
host() { # $1 name, $2 compiler, $3.. extra cmake args
  local b="$out/$1" cxx="$2"; shift 2
  cmake -S . -B "$b" -G Ninja -DCMAKE_CXX_COMPILER="$cxx" -DKB_PORT_STM32_BUILD_TESTS=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON "$@" && cmake --build "$b" && ctest --test-dir "$b" --output-on-failure
}
sanitizers() {
  host asan "$CLANG" -DCMAKE_BUILD_TYPE=Debug \
    "-DCMAKE_CXX_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer" \
    "-DCMAKE_EXE_LINKER_FLAGS=-fsanitize=address,undefined"
}
coverage() {
  host coverage "$GCC" -DCMAKE_BUILD_TYPE=Debug "-DCMAKE_CXX_FLAGS=--coverage -O0" "-DCMAKE_EXE_LINKER_FLAGS=--coverage" &&
    gcovr --root . --gcov-executable "$GCOV" --filter '^inc/' --filter '^src/' --exclude-unreachable-branches \
      --exclude-throw-branches --merge-lines --object-directory "$out/coverage" --txt-summary \
      --fail-under-line 85 --fail-under-branch 75 --print-summary
}
complexity() { library | xargs lizard -l cpp -m -w -C 15 -L 60 -a 6; }
tidy_run() {
  local sdk=(); [ "$(uname)" = Darwin ] && sdk=(--extra-arg=-isysroot "--extra-arg=$(xcrun --show-sdk-path)")
  local f bad=0 tidy18
  # The configuration must be valid for clang-tidy 20 (lint job) and 18 (host job, when installed): a key a
  # version does not know makes it report nothing, which once passed CI silently.
  clang-tidy --verify-config --config-file=_config/.clang-tidy || bad=1
  tidy18="$(command -v clang-tidy-18 || true)"
  # clang-tidy 18 only warns about the newer checks it does not know (it skips them); an unknown key is an error.
  if [ -n "$tidy18" ] && "$tidy18" --verify-config --config-file=_config/.clang-tidy 2>&1 | grep -E 'error:'; then
    bad=1
  fi
  for f in src/*.cpp src/boundary/*.cpp; do
    # stderr kept: a configuration error is an error, not silence. Compiler warnings echoed by clang-tidy are
    # not findings; "N warnings generated" lines are dropped.
    clang-tidy -p "$out/clang" --config-file=_config/.clang-tidy "${sdk[@]}" --quiet "$f" 2>&1 |
      grep -v 'warnings\? generated' | grep -E '(warning|error):' && bad=1
  done
  [ "$bad" = 0 ]
}
misra_lint() {
  local lint="${MISRA_LINT:-${CI_TOOLS_DIR:-../ci-tools}/../driver-template/tools/misra_lint.py}"
  [ -f "$lint" ] || { echo "misra_lint.py not found (MISRA_LINT)"; return 1; }
  python3 "$lint" --root . --strict ${CI_TOOLS_DIR:+--ci-tools "$CI_TOOLS_DIR"}
}
handbook() { python3 "${CI_TOOLS_DIR:?set CI_TOOLS_DIR}/tools/handbook_check/handbook_check.py" --repo .; }
layering() {
  python3 "${CI_TOOLS_DIR:?set CI_TOOLS_DIR}/tools/layering/layering.py" check --rules tools/layering/rules.yml \
    --root . --compile-db "$out/clang/compile_commands.json" --exclude "build*/** tests/fakes/** examples/**"
}

step snapshot snapshot
step layout layout
step format format
step clang host clang "$CLANG" -DCMAKE_BUILD_TYPE=Debug
if [ "$quick" = 0 ]; then
  step gcc-o2 host gcc-o2 "$GCC" -DCMAKE_BUILD_TYPE=Release
  step sanitizers sanitizers
  step coverage coverage
  step complexity complexity
  [ "$tidy" = 1 ] && step clang-tidy tidy_run
fi
step misra-lint misra_lint
step handbook handbook
step layering layering
step platform-free cmake --build "$out/clang" --target kb_port_stm32_platform_free
[ "${KEEP:-0}" = 1 ] || rm -rf "$out"
if [ "${#fails[@]}" -gt 0 ]; then echo "FAILED: ${fails[*]}"; exit 1; fi
echo "preflight: every step passed"
