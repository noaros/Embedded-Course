#!/usr/bin/env bash
# Builds every firmware image and runs the Lab 11 host tests: the same checks
# as the course CI. Usage: tools/check.sh [build-dir]
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
build="${1:-$root/build}"
gen=()
command -v ninja >/dev/null && gen=(-G Ninja)

echo "== firmware"
cmake -S "$root" -B "$build" "${gen[@]}" >/dev/null
cmake --build "$build" 2>&1 | tee "$build/build.log" | grep -E "warning|error" && {
    echo "warnings or errors in the firmware build (see $build/build.log)"; exit 1; } || true

for variant in solution starter; do
    dir="$root/modules/11-testing-ci/lab/$variant"
    [ -d "$dir" ] || continue
    echo "== host tests ($variant)"
    cmake -S "$dir" -B "$build-host-$variant" "${gen[@]}" >/dev/null
    cmake --build "$build-host-$variant" >/dev/null
    if [ "$variant" = solution ]; then
        ctest --test-dir "$build-host-$variant" --output-on-failure
    else
        # The starter's parser is a stub: its test is expected to fail.
        ctest --test-dir "$build-host-$variant" -E cmd_parser --output-on-failure
    fi
done

echo "== python tools"
python3 -m py_compile "$root"/tools/*.py
echo "all checks passed"
