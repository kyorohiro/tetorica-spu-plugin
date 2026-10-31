#!/bin/sh
# Load a local VST3 directly and verify offline PCM without opening an editor/device.
set -eu
if [ "$#" -eq 1 ] && [ "$1" = --help ]; then
  printf 'Usage: sh scripts/check_vst3_audio.sh /path/to/TetoricaSPU.vst3\n'
  exit 0
fi
if [ "$#" -ne 1 ]; then
  printf 'Usage: sh scripts/check_vst3_audio.sh /path/to/TetoricaSPU.vst3\n' >&2
  exit 2
fi
if [ ! -e "$1" ]; then
  printf 'VST3 bundle not found: %s\n' "$1" >&2
  exit 2
fi
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
python3 "$root/scripts/bootstrap.py" --check
cmake -S "$root/tools/vst3-host" -B "$root/build/vst3-host" \
  -G 'Unix Makefiles' -DCMAKE_BUILD_TYPE=Release
cmake --build "$root/build/vst3-host" --config Release \
  --target tetorica_vst3_audio_check --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"
exec "$root/build/vst3-host/tetorica_vst3_audio_check" "$1"
