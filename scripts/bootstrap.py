#!/usr/bin/env python3
"""Resume pinned dependency setup; never delete or replace an existing SDK."""
import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=ROOT):
    subprocess.run(args, cwd=cwd, check=True)


def _create_windows_junction(link, target):
    """Create one new directory junction without requiring symlink privileges."""
    # Expand paths through quoted environment variables instead of interpolating
    # them into cmd code. Delayed expansion and AutoRun are both disabled, so
    # spaces, Unicode, &, %, and ! in a checkout name remain literal arguments.
    env = os.environ.copy()
    for key, value in (("TETORICA_BOOTSTRAP_LINK", link),
                       ("TETORICA_BOOTSTRAP_TARGET", target)):
        text = str(value)
        if any(char in text for char in ('"', '\r', '\n', '\0')):
            raise ValueError('Unsupported Windows SDK link path')
        env[key] = text
    command_processor = env.get('COMSPEC', 'cmd.exe')
    if any(char in command_processor for char in ('"', '\r', '\n', '\0')):
        raise ValueError('Invalid Windows command processor path')
    command = (f'"{command_processor}" /d /v:off /s /c '
               '"mklink /J "%TETORICA_BOOTSTRAP_LINK%" '
               '"%TETORICA_BOOTSTRAP_TARGET%""')
    subprocess.run(command, check=True, env=env)


def _ensure_sdk_link(link, target, *, check=False, windows=False):
    """Accept only the exact pinned target; never replace existing SDK data."""
    if not target.is_dir():
        raise SystemExit(f'Missing pinned SDK directory: {target}')
    expected = target.resolve(strict=True)
    # lexists also detects a dangling symlink/junction: do not mistake an old
    # broken SDK link for permission to overwrite its path.
    if os.path.lexists(link):
        try:
            matches = link.resolve(strict=True) == expected
        except (OSError, RuntimeError):
            matches = False
        if not matches:
            raise SystemExit(f'Refusing to overwrite existing SDK: {link}')
        return
    if check:
        raise SystemExit(f'Missing/mismatched SDK link: {link}; run bootstrap.py')
    try:
        link.symlink_to(target, target_is_directory=True)
    except OSError as error:
        # Only ERROR_PRIVILEGE_NOT_HELD permits the junction fallback. Other
        # errors (including an existing-path race) must remain visible.
        if not windows or getattr(error, 'winerror', None) != 1314:
            raise
        _create_windows_junction(link, target)
    if not link.is_dir() or link.resolve(strict=True) != expected:
        raise SystemExit(f'Created SDK link does not match pinned target: {link}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true', help='Verify only; no network or writes')
    args = parser.parse_args()
    lock = json.loads((ROOT / 'dependencies.lock.json').read_text())
    if not args.check:
        run('git', 'submodule', 'update', '--init', 'vendor/iPlug2OOS',
            'vendor/tetorica-soundchip-native', 'vendor/vst3sdk')
        run('git', 'submodule', 'update', '--init', '--depth', '1', 'iPlug2',
            cwd=ROOT / 'vendor/iPlug2OOS')
        run('git', 'submodule', 'update', '--init', '--depth', '1',
            'base', 'pluginterfaces', 'public.sdk', 'cmake', cwd=ROOT / 'vendor/vst3sdk')
    for relative, expected in lock['repositories'].items():
        actual = subprocess.check_output(['git', 'rev-parse', 'HEAD'],
                                         cwd=ROOT / relative, text=True).strip()
        if actual != expected:
            raise SystemExit(f'Wrong revision: {relative}: {actual}, expected {expected}')
    # iPlug2 has a fixed SDK search path. Link just the required SDK directories
    # inside its ignored SDK folder, preserving the upstream tracked README.
    sdk_dir = ROOT / 'vendor/iPlug2OOS/iPlug2/Dependencies/IPlug/VST3_SDK'
    for name in ('base', 'pluginterfaces', 'public.sdk'):
        link = sdk_dir / name
        target = ROOT / 'vendor/vst3sdk' / name
        _ensure_sdk_link(link, target, check=args.check, windows=os.name == 'nt')
    print('PASS: fixed dependency revisions and VST3 SDK links')


if __name__ == '__main__':
    main()
