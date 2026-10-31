#!/bin/sh
# Build Steinberg's pinned, command-line validator and load a bundle directly.
# No plug-in installation, audio device, GUI, downloads, or signing identity needed.
set -eu

usage() {
  cat <<'USAGE'
Usage: sh scripts/validate_vst3.sh PLUGIN.vst3 [VALIDATOR_OPTIONS...]
       sh scripts/validate_vst3.sh --build-only

Builds the official validator in build/vst3-validator (Release, native architecture).
The bundle may be outside the system plug-in directories; quote paths with spaces.
The validator's exit status is returned unchanged. For example:
  sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3
  sh scripts/validate_vst3.sh build/macos/out/TetoricaSPU.vst3 -e

Set CMAKE_BUILD_PARALLEL_LEVEL to change the default of two compiler jobs.
USAGE
}

if [ "$#" -eq 0 ]; then
  usage >&2
  exit 2
fi
case "$1" in
  -h|--help) usage; exit 0 ;;
  --build-only)
    if [ "$#" -ne 1 ]; then
      usage >&2
      exit 2
    fi
    bundle=
    ;;
  *)
    bundle=$1
    shift
    if [ ! -e "$bundle" ]; then
      printf 'VST3 bundle not found: %s\n' "$bundle" >&2
      exit 2
    fi
    ;;
esac

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
sdk="$root/vendor/vst3sdk"
build="$root/build/vst3-validator"

# Check recorded revisions without fetching or modifying dependencies.
python3 "$root/scripts/bootstrap.py" --check

cmake -S "$sdk" -B "$build" -G 'Unix Makefiles' \
  -DCMAKE_BUILD_TYPE=Release \
  -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF \
  -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
  -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF \
  -DSMTG_CREATE_PLUGIN_LINK=OFF \
  -DSMTG_RUN_VST_VALIDATOR=OFF
cmake --build "$build" --config Release --target validator \
  --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-2}"

validator="$build/bin/Release/validator"
if [ ! -x "$validator" ]; then
  printf 'Validator executable not found: %s\n' "$validator" >&2
  exit 1
fi

if [ -z "$bundle" ]; then
  printf 'Built validator: %s\n' "$validator"
  exec "$validator" -selftest
fi

exec "$validator" "$bundle" "$@"
