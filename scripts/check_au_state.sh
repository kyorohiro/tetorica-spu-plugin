#!/bin/sh
# Load/register the local AU in-process and render/state-check without a device.
set -eu
if [ "$#" -eq 1 ] && [ "$1" = --help ]; then
  printf 'Usage: sh scripts/check_au_state.sh /path/to/TetoricaSPU.component\n'
  exit 0
fi
if [ "$#" -ne 1 ]; then
  printf 'Usage: sh scripts/check_au_state.sh /path/to/TetoricaSPU.component\n' >&2
  exit 2
fi
if [ ! -d "$1" ]; then
  printf 'AU bundle not found: %s\n' "$1" >&2
  exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake --preset macos -S "$root"
cmake --build "$root/build/macos" --config Release --target tetorica_au_state_check \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
exec "$root/build/macos/tetorica_au_state_check" "$1"
