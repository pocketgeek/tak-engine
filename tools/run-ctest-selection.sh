#!/usr/bin/env bash
# Run an explicit list of CTest tests, failing if any of them is not registered.
#
#   tools/run-ctest-selection.sh BUILD_DIR NAME [NAME...]
#
# `ctest -R` passes silently for a pattern that matches nothing: a renamed test,
# or one CMake skipped (the Python-driven navigation tests are registered only
# when CMake finds an interpreter) would quietly drop out of CI. Each NAME is an
# exact test name or a prefix ending in `*` (e.g. cooperative_progress_*), which
# must match at least one registered test. Portable to bash 3.2 (macOS) and MSYS2.
set -euo pipefail
build=${1:?usage: run-ctest-selection.sh BUILD_DIR NAME [NAME...]}
shift
[[ $# -gt 0 ]] || { echo "run-ctest-selection: no tests named" >&2; exit 2; }
listed=$(ctest --test-dir "$build" -N | sed -n 's/^ *Test *#[0-9]*: *//p' | tr -d '\r')
pattern=""
missing=0
for name in "$@"; do
  if [[ "$name" == *'*' ]]; then
    prefix=${name%\*}
    count=$(printf '%s\n' "$listed" | grep -c "^${prefix}" || true)
    regex="${prefix}.*"
  else
    count=$(printf '%s\n' "$listed" | grep -cx "$name" || true)
    regex="$name"
  fi
  if [[ "$count" -eq 0 ]]; then
    echo "::error::ctest test '$name' is not registered in $build" >&2
    missing=1
  else
    echo "selected $name ($count test(s))"
  fi
  pattern="${pattern:+$pattern|}$regex"
done
[[ "$missing" -eq 0 ]] || exit 1
ctest --test-dir "$build" -R "^($pattern)\$" --no-tests=error --output-on-failure
