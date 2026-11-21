#!/bin/sh
# Check what the freertos_port option does to a bare-metal cross build
# (doc/design/esp32-port.md, "PRISM's build for this target"): at its default the build lists
# the QEMU firmwares of tests/embedded (posix_tests.elf); with -Dfreertos_port=disabled it
# lists no target from ports/ or tests/embedded (no name containing "freertos" or "posix")
# and still lists libprism (the "prism" target of src/meson.build).
#
# Two meson setups in temporary directories, with the builddir-arm cross file of AGENTS.md.
# Each setup's output goes to setup.log inside its temp dir; when a setup fails, the log's
# last 20 lines are printed before the script exits 1, so the failure says why.
# Needs meson, python3 and the ARM GCC on PATH:
#   PATH=$HOME/opt/arm-gcc-threads/bin:$PATH sh tests/cross/check_freertos_port.sh
set -eu

srcdir=$(CDPATH= cd "$(dirname -- "$0")/../.." && pwd)
cross_file="$srcdir/tests/cross/none-cortex-m7.ini"

build_default=$(mktemp -d)
build_disabled=$(mktemp -d)
trap 'rm -rf "$build_default" "$build_disabled"' EXIT

# One meson setup with the cross file; extra arguments (e.g. -Dfreertos_port=disabled) come
# after the build dir. The output is kept in $dir/setup.log, and a failed setup prints the
# log's last 20 lines before exiting 1.
setup() {
  dir=$1
  shift
  log="$dir/setup.log"
  if meson setup "$dir" "$srcdir" --cross-file "$cross_file" -Dsdl=disabled "$@" >"$log" 2>&1; then
    return 0
  fi
  echo "meson setup $dir $* failed; last 20 lines of $log:" >&2
  tail -n 20 "$log" >&2
  exit 1
}

setup "$build_default"
setup "$build_disabled" -Dfreertos_port=disabled

# The target names of a build dir, one per line.
target_names() {
  meson introspect --targets "$1" | python3 -c '
import json, sys
for target in json.load(sys.stdin):
    print(target["name"])
'
}

names_default=$(target_names "$build_default")
names_disabled=$(target_names "$build_disabled")

failures=0

if printf '%s\n' "$names_default" | grep -qx 'posix_tests.elf'; then
  echo "default build lists posix_tests.elf (tests/embedded is built): PASS"
else
  echo "default build lists posix_tests.elf (tests/embedded is built): FAIL"
  failures=$((failures + 1))
fi

if printf '%s\n' "$names_disabled" | grep -q 'freertos'; then
  echo "disabled build lists no target with freertos in its name (ports/ is out): FAIL"
  failures=$((failures + 1))
else
  echo "disabled build lists no target with freertos in its name (ports/ is out): PASS"
fi

if printf '%s\n' "$names_disabled" | grep -q 'posix'; then
  echo "disabled build lists no target with posix in its name (ports/ is out): FAIL"
  failures=$((failures + 1))
else
  echo "disabled build lists no target with posix in its name (ports/ is out): PASS"
fi

if printf '%s\n' "$names_disabled" | grep -qx 'prism'; then
  echo "disabled build still lists prism (libprism.a): PASS"
else
  echo "disabled build still lists prism (libprism.a): FAIL"
  failures=$((failures + 1))
fi

if [ "$failures" -gt 0 ]; then
  exit 1
fi
