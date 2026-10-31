#!/usr/bin/env python3
"""Install this development AU for the current user without replacing another bundle."""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'build/macos/out/TetoricaSPU.component'
DESTINATION = Path.home() / 'Library/Audio/Plug-Ins/Components/TetoricaSPU.component'


def fingerprint(bundle):
    files = {}
    for path in sorted(bundle.rglob('*')):
        relative = str(path.relative_to(bundle))
        if path.is_symlink():
            raise SystemExit(f'Refusing unexpected bundle symlink: {path}')
        if path.is_file():
            files[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
    if not files:
        raise SystemExit(f'Empty bundle: {bundle}')
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true', help='Copy only if destination does not exist')
    args = parser.parse_args()
    info = plistlib.loads((SOURCE / 'Contents/Info.plist').read_bytes())
    component = info['AudioComponents'][0]
    if (component['type'], component['subtype'], component['manufacturer']) != ('aumu', 'Tspu', 'Tetr'):
        raise SystemExit('Unexpected source AU identity')
    expected = fingerprint(SOURCE)
    print(f'Source: {SOURCE}\nDestination: {DESTINATION}')
    if DESTINATION.is_symlink():
        raise SystemExit('Refusing an existing destination symlink')
    if DESTINATION.exists():
        if fingerprint(DESTINATION) != expected:
            raise SystemExit('Destination differs; refusing to overwrite. Review the existing AU first.')
        print('PASS: existing installed AU matches the development build')
    elif args.install:
        DESTINATION.parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='.tetorica-spu-stage-', dir=DESTINATION.parent))
        staged_bundle = stage / DESTINATION.name
        shutil.copytree(SOURCE, staged_bundle)
        if fingerprint(staged_bundle) != expected:
            raise SystemExit(f'Copy verification failed; staged files retained at {stage}')
        if DESTINATION.exists() or DESTINATION.is_symlink():
            raise SystemExit(f'Destination appeared during copying; stage retained at {stage}')
        staged_bundle.rename(DESTINATION)
        stage.rmdir()  # Only our new, now empty staging directory.
        print('PASS: installed a new AU bundle; no existing plugin was replaced')
    else:
        print('Not installed. Re-run with --install to copy this AU for the current user.')
        return
    report = {'source': str(SOURCE), 'destination': str(DESTINATION),
              'component': {k: component[k] for k in ('type', 'subtype', 'manufacturer', 'version')},
              'file_sha256': expected}
    (ROOT / 'build/au-install.json').write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    main()
