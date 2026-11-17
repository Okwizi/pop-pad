#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Runs what CI runs, in the same order: formatting, linting, the build, then the tests.
#
# Needs: cmake, ninja, g++, g++-mingw-w64-i686, shellcheck and the packages in
# requirements-dev.txt (pip install -r requirements-dev.txt, preferably in a virtualenv).
# Pass --fix to let clang-format and ruff rewrite files instead of only reporting.
set -euo pipefail
cd "$(dirname "$0")/.."

fix=false
[ "${1:-}" = "--fix" ] && fix=true

step() { printf '\n== %s\n' "$*"; }

cpp_files=(src/*.cpp src/*.h tests/native/*.cpp tests/native/shim/*.h)

step "clang-format"
if $fix; then clang-format -i "${cpp_files[@]}"; else clang-format --dry-run -Werror "${cpp_files[@]}"; fi

step "ruff"
if $fix; then ruff check --fix tools tests && ruff format tools tests; else ruff check tools tests && ruff format --check tools tests; fi

step "shellcheck"
shellcheck -x tools/*.sh tools/dev/*.sh

step "build the DLL (MinGW, warnings are errors)"
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-i686.cmake -DCMAKE_BUILD_TYPE=Release -DPOPPAD_WERROR=ON
cmake --build build

step "unit tests, C++ (host compiler, sanitizers on)"
cmake -S tests/native -B build/native -G Ninja -DPOPPAD_WERROR=ON
cmake --build build/native
ctest --test-dir build/native --output-on-failure

step "unit tests, Python"
python3 -m pytest -q

printf '\nAll checks passed.\n'
