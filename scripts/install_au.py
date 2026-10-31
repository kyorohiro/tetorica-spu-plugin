#!/usr/bin/env python3
"""Inspect/install this AU, or explicitly update a hash-identified prior installation."""
import argparse
import ctypes
import datetime
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'build/macos/out/TetoricaSPU.component'
DESTINATION = Path.home() / 'Library/Audio/Plug-Ins/Components/TetoricaSPU.component'


def fingerprint(bundle):
    bundle = Path(bundle)
    if bundle.is_symlink() or not bundle.is_dir():
        raise SystemExit(f'Refusing missing/non-directory/symlink bundle: {bundle}')
    files = {}
    for path in sorted(bundle.rglob('*')):
        relative = str(path.relative_to(bundle))
        if path.is_symlink():
            raise SystemExit(f'Refusing unexpected bundle symlink: {path}')
        if path.is_file():
            files[relative] = hashlib.sha256(path.read_bytes()).hexdigest()
        elif not path.is_dir():
            raise SystemExit(f'Refusing special bundle entry: {path}')
    if not files:
        raise SystemExit(f'Empty bundle: {bundle}')
    return files


def source_identity(bundle):
    fingerprint(bundle)  # Reject all symlinks before opening the Info.plist.
    info = plistlib.loads((Path(bundle) / 'Contents/Info.plist').read_bytes())
    component = info['AudioComponents'][0]
    if (component['type'], component['subtype'], component['manufacturer']) != ('aumu', 'Tspu', 'Tetr'):
        raise SystemExit('Unexpected source AU identity')
    return {key: component[key] for key in ('type', 'subtype', 'manufacturer', 'version')}


def expected_manifest(record):
    data = json.loads(Path(record).read_text())
    manifest = data.get('au', data).get('file_sha256')
    if not isinstance(manifest, dict) or not manifest:
        raise SystemExit('Expected record has no AU file_sha256 manifest')
    for name, digest in manifest.items():
        if not isinstance(name, str):
            raise SystemExit('Invalid expected AU manifest path')
        path = Path(name)
        if (path.is_absolute() or '..' in path.parts or
                not isinstance(digest, str) or not re.fullmatch(r'[0-9a-f]{64}', digest)):
            raise SystemExit('Invalid expected AU manifest')
    return manifest


def ensure_no_hosts():
    if sys.platform != 'darwin':
        return
    result = subprocess.run(['ps', '-axo', 'comm='], check=True, capture_output=True, text=True)
    known = {'GarageBand', 'Logic Pro', 'MainStage', 'AU Lab', 'REAPER', 'Ableton Live'}
    found = sorted({Path(line.strip()).name for line in result.stdout.splitlines()} & known)
    if found:
        raise SystemExit('Quit AU hosts before changing the installed bundle: ' + ', '.join(found))


def _rename_exclusive(source, destination):
    """Rename without POSIX rename's otherwise allowed empty-directory replacement."""
    source, destination = Path(source), Path(destination)
    if os.name == 'nt':
        os.rename(source, destination)  # Windows refuses an existing destination.
        return
    library = ctypes.CDLL(None, use_errno=True)
    if sys.platform == 'darwin':
        function = library.renamex_np
        function.argtypes = [ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint]
        arguments = (os.fsencode(source), os.fsencode(destination), 4)  # RENAME_EXCL, macOS SDK sys/stdio.h.
    else:
        function = library.renameat2
        function.argtypes = [ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint]
        arguments = (-100, os.fsencode(source), -100, os.fsencode(destination), 1)  # AT_FDCWD, RENAME_NOREPLACE.
    function.restype = ctypes.c_int
    if function(*arguments):
        error = ctypes.get_errno()
        raise OSError(error, os.strerror(error), str(destination))


def _write_report(path, report):
    with tempfile.NamedTemporaryFile('w', prefix='.report-', dir=path.parent, delete=False) as file:
        json.dump(report, file, indent=2)
        file.write('\n')
        file.flush()
        os.fsync(file.fileno())
        temporary = Path(file.name)
    os.replace(temporary, path)


def _copy_verified(source, destination, expected):
    shutil.copytree(source, destination, symlinks=True)
    if fingerprint(destination) != expected:
        raise SystemExit(f'Copy verification failed: {destination}')


def _same_filesystem(destination, backup_root):
    if destination.parent.is_symlink() or backup_root.is_symlink():
        raise SystemExit('Refusing a symlink Components/backup directory')
    backup_root, components = backup_root.resolve(), destination.parent.resolve()
    if backup_root == components or components in backup_root.parents:
        raise SystemExit('Backup root must be outside Components')
    backup_root.mkdir(parents=True, exist_ok=True)
    if not backup_root.is_dir() or components.stat().st_dev != backup_root.stat().st_dev:
        raise SystemExit('Backup root and installed AU must use the same filesystem')


def _recover(destination, report, report_path):
    """Restore only an absent destination or our exact new bundle; never unknown data."""
    try:
        if destination.exists() or destination.is_symlink():
            if fingerprint(destination) != report['new_sha256']:
                report['state'] = 'failed_needs_review'
                report['recovery_error'] = 'Destination changed; unknown data was not moved or replaced'
                _write_report(report_path, report)
                return
            _rename_exclusive(destination, Path(report['backup_directory']) / 'failed-new.component')
        original = Path(report['old_archive'])
        if fingerprint(original) != report['old_sha256']:
            raise SystemExit('Archived original no longer matches expected hashes')
        # Rename uses no full-bundle disk allocation. The verified backup copy
        # remains permanently in backup_component, even when the original moves.
        _rename_exclusive(original, destination)
        if fingerprint(destination) != report['old_sha256']:
            raise SystemExit('Recovery hash verification failed')
        report['state'] = 'failed_recovered'
        _write_report(report_path, report)
    except BaseException as error:
        report['state'] = 'failed_needs_review'
        report['recovery_error'] = str(error)
        try:
            _write_report(report_path, report)
        except BaseException:
            pass  # The already durable prepared journal still records both copies.


def update_bundle(source, destination, expected_record, backup_root, *, host_check=None):
    source, destination, backup_root = map(Path, (source, destination, backup_root))
    if any(path.is_symlink() for path in (source, destination, destination.parent, backup_root)):
        raise SystemExit('Refusing a source/destination/Components/backup symlink')
    source, destination, backup_root = source.resolve(), destination.resolve(), backup_root.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        raise SystemExit('Source and installed AU must not overlap')
    if backup_root == source or source in backup_root.parents:
        raise SystemExit('Backup root must be outside the source bundle')
    check = host_check or ensure_no_hosts
    check()
    old = expected_manifest(expected_record)
    component = source_identity(source)
    new = fingerprint(source)
    if fingerprint(destination) != old:
        raise SystemExit('Installed AU differs from the expected record; refusing update')
    _same_filesystem(destination, backup_root)
    directory = Path(tempfile.mkdtemp(prefix='au-update-', dir=backup_root)).resolve()
    report_path = directory / 'manifest.json'
    backup, staged, original = (directory / name for name in ('previous.component', 'staged.component', 'original.component'))
    report = {'schema': 1, 'operation': 'au-update', 'state': 'preparing',
              'created_utc': datetime.datetime.now(datetime.timezone.utc).isoformat(),
              'source': str(source.resolve()), 'destination': str(destination.absolute()),
              'backup_directory': str(directory), 'backup_component': str(backup),
              'staged_component': str(staged), 'old_archive': str(original),
              'component': component, 'old_sha256': old, 'new_sha256': new}
    _write_report(report_path, report)
    moved = False
    try:
        _copy_verified(destination, backup, old)
        _copy_verified(source, staged, new)
        report['state'] = 'prepared'
        _write_report(report_path, report)
        check()
        if fingerprint(destination) != old:
            raise SystemExit('Installed AU changed during staging; refusing replacement')
        _rename_exclusive(destination, original)
        moved = True
        # Recheck after the rename too: a concurrent change must be recovered
        # rather than silently replaced by the new source.
        if fingerprint(original) != old:
            raise SystemExit('Original AU changed at replacement boundary')
        report['state'] = 'old_archived'
        _write_report(report_path, report)
        _rename_exclusive(staged, destination)
        if fingerprint(destination) != new:
            raise SystemExit('Installed new AU failed full hash verification')
        report['state'] = 'installed'
        _write_report(report_path, report)
        return report_path
    except BaseException as error:
        report['error'] = str(error)
        if moved:
            _recover(destination, report, report_path)
        else:
            report['state'] = 'failed_before_replace'
            _write_report(report_path, report)
        raise SystemExit(f'AU update failed: {error}\nRecovery record: {report_path}') from error


def rollback_bundle(report_path, *, expected_destination=None, host_check=None):
    report_path = Path(report_path)
    report = json.loads(report_path.read_text())
    if report.get('schema') != 1 or report.get('operation') != 'au-update' or report.get('state') != 'installed':
        raise SystemExit('Rollback requires an installed AU update report')
    destination = Path(report['destination'])
    if destination.is_symlink() or destination.parent.is_symlink():
        raise SystemExit('Refusing a destination/Components symlink')
    destination = destination.resolve()
    if expected_destination is not None and destination != Path(expected_destination).resolve():
        raise SystemExit('Rollback record names a different destination')
    check = host_check or ensure_no_hosts
    check()
    if fingerprint(destination) != report['new_sha256']:
        raise SystemExit('Installed AU changed after update; refusing rollback')
    backup = Path(report['backup_component'])
    if fingerprint(backup) != report['old_sha256']:
        raise SystemExit('Backup changed; refusing rollback')
    directory = Path(report['backup_directory'])
    if directory.is_symlink() or backup.is_symlink():
        raise SystemExit('Refusing backup symlinks')
    directory, backup = directory.resolve(), backup.resolve()
    if backup.parent != directory or report_path.resolve().parent != directory:
        raise SystemExit('Rollback report/backup must belong to the recorded backup directory')
    _same_filesystem(destination, directory)
    stage = directory / 'rollback-staged.component'
    _copy_verified(backup, stage, report['old_sha256'])
    check()
    if fingerprint(destination) != report['new_sha256']:
        raise SystemExit('Installed AU changed during rollback staging')
    new_archive = directory / 'rolled-back-new.component'
    _rename_exclusive(destination, new_archive)
    try:
        if fingerprint(new_archive) != report['new_sha256']:
            raise SystemExit('Installed AU changed at rollback boundary')
        _rename_exclusive(stage, destination)
        if fingerprint(destination) != report['old_sha256']:
            raise SystemExit('Restored AU failed hash verification')
        report['state'] = 'rolled_back'
        report['rolled_back_new'] = str(new_archive)
        _write_report(report_path, report)
        return report_path
    except BaseException:
        if not destination.exists() and not destination.is_symlink():
            _rename_exclusive(new_archive, destination)
        raise


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument('--install', action='store_true', help='Copy only if destination does not exist')
    actions.add_argument('--update', action='store_true', help='Replace only a hash-identified old AU, retaining a verified backup')
    actions.add_argument('--rollback', type=Path, help='Restore a backup only if the installed new AU still matches its update report')
    parser.add_argument('--expected', type=Path, help='Prior verification JSON with AU file_sha256')
    parser.add_argument('--backup-root', type=Path, help='Workspace backup directory outside Components')
    args = parser.parse_args()
    if args.rollback:
        if args.expected or args.backup_root:
            parser.error('--rollback cannot be combined with --expected/--backup-root')
        path = rollback_bundle(args.rollback, expected_destination=DESTINATION)
        print(f'PASS: restored verified prior AU; backup retained. Record: {path}')
        return
    if args.update:
        if not args.expected or not args.backup_root:
            parser.error('--update requires --expected and --backup-root')
        if ROOT not in args.backup_root.resolve().parents:
            parser.error('--backup-root must be inside this workspace')
        path = update_bundle(SOURCE, DESTINATION, args.expected, args.backup_root)
        print(f'PASS: updated hash-identified AU; verified backup retained. Record: {path}')
        return
    if args.expected or args.backup_root:
        parser.error('--expected/--backup-root require --update')
    component = source_identity(SOURCE)
    expected = fingerprint(SOURCE)
    print(f'Source: {SOURCE}\nDestination: {DESTINATION}')
    if DESTINATION.is_symlink():
        raise SystemExit('Refusing an existing destination symlink')
    if DESTINATION.exists():
        if fingerprint(DESTINATION) != expected:
            raise SystemExit('Destination differs; refusing to overwrite. Review the existing AU first.')
        print('PASS: existing installed AU matches the development build')
    elif args.install:
        if DESTINATION.parent.is_symlink():
            raise SystemExit('Refusing a Components directory symlink')
        DESTINATION.parent.mkdir(parents=True, exist_ok=True)
        stage = Path(tempfile.mkdtemp(prefix='.tetorica-spu-stage-', dir=DESTINATION.parent))
        staged_bundle = stage / DESTINATION.name
        shutil.copytree(SOURCE, staged_bundle)
        if fingerprint(staged_bundle) != expected:
            raise SystemExit(f'Copy verification failed; staged files retained at {stage}')
        if DESTINATION.exists() or DESTINATION.is_symlink():
            raise SystemExit(f'Destination appeared during copying; stage retained at {stage}')
        _rename_exclusive(staged_bundle, DESTINATION)
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
