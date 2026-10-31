#!/usr/bin/env python3
"""Resume pinned dependency setup; never delete or replace an existing SDK."""
import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def run(*args, cwd=ROOT):
    subprocess.run(args, cwd=cwd, check=True)


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
        if args.check:
            if not link.exists() or link.resolve() != target.resolve():
                raise SystemExit(f'Missing/mismatched SDK link: {link}; run bootstrap.py')
        elif link.is_symlink() or link.exists():
            if link.resolve() != target.resolve():
                raise SystemExit(f'Refusing to overwrite existing SDK: {link}')
        else:
            # Windows symlinks need developer mode or elevation; no copying fallback
            # that silently changes ownership or destroys another installed SDK.
            link.symlink_to(target, target_is_directory=True)
    print('PASS: fixed dependency revisions and VST3 SDK links')


if __name__ == '__main__':
    main()
