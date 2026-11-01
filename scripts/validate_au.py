#!/usr/bin/env python3
"""Wait for AU discovery, then run auval once without hiding validation failures."""
import argparse
import getpass
import json
import os
from pathlib import Path
import plistlib
import re
import signal
import subprocess
import sys
import time


COMPONENTS_RELATIVE = Path('Library/Audio/Plug-Ins/Components')
COMPONENT_NAME = 'TetoricaSPU.component'


def _registrar_pids(uid):
    """Use full executable names, not platform-dependent pgrep name limits."""
    result = subprocess.run(['/bin/ps', '-axo', 'pid=,uid=,comm='],
                            capture_output=True, text=True, timeout=5)
    if result.returncode:
        raise SystemExit(f'Cannot enumerate CI registrar processes: {result.stderr.strip()}')
    pids = set()
    for line in result.stdout.splitlines():
        fields = line.split(maxsplit=2)
        if len(fields) != 3:
            raise SystemExit(f'Cannot parse CI process identity: {line!r}')
        try:
            pid, owner = int(fields[0]), int(fields[1])
        except ValueError:
            raise SystemExit(f'Cannot parse CI process identity: {line!r}') from None
        if pid > 1 and owner == uid and Path(fields[2]).name == 'AudioComponentRegistrar':
            pids.add(pid)
    return pids


def _restart_ci_registrar():
    uid = os.getuid()
    if uid == 0:
        raise SystemExit('Refusing to restart the root Audio Component service')
    captured = _registrar_pids(uid)
    print(f'CI registrar captured uid={uid} pids={sorted(captured)}', flush=True)
    for pid in sorted(captured):
        # Recheck ownership and the full executable name immediately before
        # signaling. Never signal a root daemon, another host, or a newly-spawned
        # replacement registrar. EnablePressuredExit agents ignore SIGTERM by
        # default (launchd.plist(5)); SIGTERM success did not restart PID409 in CI.
        if pid not in _registrar_pids(uid):
            continue
        try:
            os.kill(pid, signal.SIGKILL)
        except ProcessLookupError:
            continue
        except OSError as error:
            raise SystemExit(f'Cannot stop captured CI registrar PID{pid}: {error}') from error
        print(f'CI registrar SIGKILL sent to captured PID{pid}', flush=True)
    deadline = time.monotonic() + 5
    while True:
        current = _registrar_pids(uid)
        remaining = captured & current
        if not remaining:
            print(f'PASS: original CI registrar PIDs gone={sorted(captured)}; '
                  f'current same-user registrar PIDs={sorted(current)}', flush=True)
            return
        if time.monotonic() >= deadline:
            raise SystemExit(f'Captured CI registrar PIDs did not exit within 5 seconds: {sorted(remaining)}')
        time.sleep(0.1)


def refresh_ci_registry():
    """Refresh only the disposable runner's installed AUv2 search directory."""
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise SystemExit('Registrar refresh is restricted to GitHub Actions; no local service was changed')
    components = Path.home() / COMPONENTS_RELATIVE
    bundle = components / COMPONENT_NAME
    if (components.is_symlink() or not components.is_dir() or bundle.is_symlink()
            or not bundle.is_dir() or not (bundle / 'Contents/Info.plist').is_file()):
        raise SystemExit(f'Cannot refresh CI registry without the installed AU: {bundle}')
    # The installer has already published the complete, verified bundle. Restart
    # only captured processes owned by this disposable runner user, and verify
    # their exit before auval can connect to a stale registrar again.
    _restart_ci_registrar()
    before = components.stat().st_mtime_ns
    # Apple's AU installation contract refreshes the registry when a Components
    # directory's modification timestamp changes. Cross a whole-second boundary
    # before touching that directory, including on a fresh CI image. This is a
    # targeted refresh, not proof of the previous discovery failure's cause.
    # https://developer.apple.com/library/archive/documentation/MusicAudio/Conceptual/AudioUnitProgrammingGuide/AudioUnitDevelopmentFundamentals/AudioUnitDevelopmentFundamentals.html
    time.sleep(1.1)
    os.utime(components, None)
    print(f'CI Components mtime refresh: {components} {before} -> '
          f'{components.stat().st_mtime_ns}', flush=True)


def _diagnostic_command(label, argv, timeout=5):
    """Read-only, bounded diagnostics must not replace the discovery failure."""
    print(f'--- AU discovery diagnostic: {label} ---', flush=True)
    try:
        result = subprocess.run(argv, capture_output=True, text=True, timeout=timeout)
        print(f'exit={result.returncode}', flush=True)
        print(result.stdout[-16384:], end='', flush=True)
        print(result.stderr[-16384:], end='', file=sys.stderr, flush=True)
    except (OSError, subprocess.TimeoutExpired) as error:
        print(f'Diagnostic unavailable: {error}', flush=True)


def discovery_diagnostics(listing):
    print('--- Final auval -a stdout ---', flush=True)
    if listing:
        print(listing.stdout, end='', flush=True)
        print('--- Final auval -a stderr ---', flush=True)
        print(listing.stderr, end='', file=sys.stderr, flush=True)
    else:
        print('The final listing attempt timed out.', flush=True)
    components = Path.home() / COMPONENTS_RELATIVE
    bundle = components / COMPONENT_NAME
    print(f'--- AU discovery context: user={getpass.getuser()} '
          f'HOME={os.environ.get("HOME")!r} home={Path.home()} ---', flush=True)
    for path in (components, bundle, bundle / 'Contents/Info.plist', bundle / 'Contents/MacOS/TetoricaSPU'):
        try:
            stat = path.lstat()
            print(json.dumps({'path': str(path), 'mode': oct(stat.st_mode), 'uid': stat.st_uid,
                              'gid': stat.st_gid, 'mtime_ns': stat.st_mtime_ns,
                              'symlink': path.is_symlink()}), flush=True)
        except OSError as error:
            print(f'{path}: {error}', flush=True)
    try:
        with (bundle / 'Contents/Info.plist').open('rb') as handle:
            info = plistlib.load(handle)
        print(json.dumps({key: info.get(key) for key in (
            'CFBundleIdentifier', 'CFBundleExecutable', 'CFBundleVersion',
            'LSMinimumSystemVersion', 'AudioComponents')}, default=str), flush=True)
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        print(f'Installed plist unavailable: {error}', flush=True)
    _diagnostic_command('architecture', ['/usr/bin/uname', '-m'])
    _diagnostic_command('system version', ['/usr/bin/sw_vers'])
    _diagnostic_command('registrar processes', ['/usr/bin/pgrep', '-alf', 'AudioComponentRegistrar'])
    _diagnostic_command('bundle extended attributes', ['/usr/bin/xattr', '-l', str(bundle)])
    _diagnostic_command('installed bundle signature validity', ['/usr/bin/codesign', '--verify',
                        '--strict', '--all-architectures', str(bundle)])
    _diagnostic_command('installed bundle signature metadata', ['/usr/bin/codesign', '-dv',
                        '--verbose=2', str(bundle)])
    _diagnostic_command('recent registrar messages', ['/usr/bin/log', 'show', '--last', '2m',
                        '--style', 'compact', '--predicate', 'process == "AudioComponentRegistrar"'])


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh-ci-registry', action='store_true',
                        help='Restart this ephemeral GitHub runner user\'s registrar, not audio hosts')
    args = parser.parse_args(argv)
    if args.refresh_ci_registry:
        refresh_ci_registry()
    deadline = time.monotonic() + 30
    listing = None
    while time.monotonic() < deadline:
        try:
            listing = subprocess.run(['/usr/bin/auval', '-a'], capture_output=True,
                                     text=True, timeout=5)
        except subprocess.TimeoutExpired:
            listing = None
        if listing and listing.returncode == 0 and re.search(
                r'^\s*aumu\s+Tspu\s+Tetr\b', listing.stdout, re.MULTILINE):
            print('PASS: TetoricaSPU was discovered by the Audio Component service', flush=True)
            # Discovery retries do not turn a real API/render/MIDI failure green.
            validation = subprocess.run(['/usr/bin/auval', '-v', 'aumu', 'Tspu', 'Tetr'],
                                        capture_output=True, text=True)
            print(validation.stdout, end='', flush=True)
            print(validation.stderr, end='', file=sys.stderr, flush=True)
            if validation.returncode:
                raise SystemExit(validation.returncode)
            # auval returns success for this warning. Treat it as a regression
            # so CI verifies the host's preset-name update path too.
            if 'Preset name is not retained in retrieved class data' in validation.stdout + validation.stderr:
                raise SystemExit('AU preset-name validation failed despite auval exit 0')
            raise SystemExit(0)
        time.sleep(1)
    detail = 'listing timed out'
    if listing:
        detail = f'listing exit={listing.returncode}, stderr={listing.stderr.strip()[:2048]!r}'
    discovery_diagnostics(listing)
    raise SystemExit(f'TetoricaSPU was not discovered within 30 seconds; auval validation was not run ({detail})')


if __name__ == '__main__':
    main()
