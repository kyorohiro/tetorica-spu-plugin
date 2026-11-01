"""AU update ownership/recovery tests; all paths live in one temporary directory."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import plistlib
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location('install_au', Path(__file__).parents[1] / 'scripts/install_au.py')
installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(installer)


class InstallerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='tetorica-au-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        (self.root / 'build').mkdir()
        self.source = self.root / 'source/TetoricaSPU.component'
        self.destination = self.root / 'Components/TetoricaSPU.component'
        self.backups = self.root / 'backups'
        self.bundle(self.source, b'new native binary')
        self.bundle(self.destination, b'previous native binary')
        self.old = installer.fingerprint(self.destination)
        self.new = installer.fingerprint(self.source)
        self.expected = self.root / 'expected.json'
        self.expected.write_text(json.dumps({'au': {'file_sha256': self.old}}))

    @staticmethod
    def bundle(path, binary):
        files = {'Contents/Info.plist': plistlib.dumps({'AudioComponents': [{
            'type': 'aumu', 'subtype': 'Tspu', 'manufacturer': 'Tetr', 'version': 256}]}),
                 'Contents/MacOS/TetoricaSPU': binary, 'Contents/PkgInfo': b'BNDL????',
                 'Contents/Resources/Roboto-Regular.ttf': b'original test font',
                 'Contents/Resources/THIRD_PARTY_NOTICES.txt': b'original test notices'}
        for name, content in files.items():
            target = path / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(content)

    def update(self, **kwargs):
        return installer.update_bundle(self.source, self.destination, self.expected, self.backups,
                                       host_check=kwargs.pop('host_check', lambda: None), **kwargs)

    def reports(self):
        return list(self.backups.glob('au-update-*/manifest.json'))

    def test_success_full_backup_and_rollback(self):
        record = self.update()
        data = json.loads(record.read_text())
        self.assertEqual(data['state'], 'installed')
        self.assertEqual(installer.fingerprint(self.destination), self.new)
        self.assertEqual(installer.fingerprint(Path(data['backup_component'])), self.old)
        self.assertEqual(installer.fingerprint(Path(data['old_archive'])), self.old)
        self.assertEqual(len(data['old_sha256']), 5)
        installer.rollback_bundle(record, expected_destination=self.destination, host_check=lambda: None)
        rolled = json.loads(record.read_text())
        self.assertEqual(rolled['state'], 'rolled_back')
        self.assertEqual(installer.fingerprint(self.destination), self.old)
        self.assertEqual(installer.fingerprint(Path(rolled['backup_component'])), self.old)
        self.assertEqual(installer.fingerprint(Path(rolled['old_archive'])), self.old)
        self.assertEqual(installer.fingerprint(Path(rolled['rolled_back_new'])), self.new)
        with self.assertRaises(SystemExit):
            installer.rollback_bundle(record, host_check=lambda: None)

    def test_unknown_existing_bundle_is_never_overwritten(self):
        (self.destination / 'unknown').write_bytes(b'user data')
        before = installer.fingerprint(self.destination)
        with self.assertRaises(SystemExit):
            self.update()
        self.assertEqual(installer.fingerprint(self.destination), before)
        self.assertFalse(self.backups.exists())

    def test_copy_failure_preserves_installed_and_backup(self):
        real = installer._copy_verified

        def copying(source, destination, expected):
            if destination.name == 'staged.component':
                raise OSError('injected stage-copy failure')
            real(source, destination, expected)

        with mock.patch.object(installer, '_copy_verified', side_effect=copying):
            with self.assertRaisesRegex(SystemExit, 'stage-copy failure'):
                self.update()
        self.assertEqual(installer.fingerprint(self.destination), self.old)
        data = json.loads(self.reports()[0].read_text())
        self.assertEqual(data['state'], 'failed_before_replace')
        self.assertEqual(installer.fingerprint(Path(data['backup_component'])), self.old)

    def test_install_rename_failure_recovers_old(self):
        real = installer._rename_exclusive

        def renaming(source, destination):
            if source.name == 'staged.component':
                raise OSError('injected install rename failure')
            real(source, destination)

        with mock.patch.object(installer, '_rename_exclusive', side_effect=renaming):
            with self.assertRaisesRegex(SystemExit, 'rename failure'):
                self.update()
        self.assertEqual(installer.fingerprint(self.destination), self.old)
        data = json.loads(self.reports()[0].read_text())
        self.assertEqual(data['state'], 'failed_recovered')
        self.assertEqual(installer.fingerprint(Path(data['backup_component'])), self.old)

    def test_change_during_staging_keeps_user_change(self):
        calls = 0

        def hosts():
            nonlocal calls
            calls += 1
            if calls == 2:
                (self.destination / 'user-change').write_bytes(b'changed while staging')

        with self.assertRaisesRegex(SystemExit, 'changed during staging'):
            self.update(host_check=hosts)
        self.assertEqual((self.destination / 'user-change').read_bytes(), b'changed while staging')
        self.assertEqual(json.loads(self.reports()[0].read_text())['state'], 'failed_before_replace')

    def test_exclusive_rename_never_replaces_a_racing_empty_directory(self):
        real = installer._rename_exclusive

        def racing(source, destination):
            if source.name == 'staged.component':
                destination.mkdir()
            real(source, destination)

        with mock.patch.object(installer, '_rename_exclusive', side_effect=racing):
            with self.assertRaises(SystemExit):
                self.update()
        self.assertEqual(list(self.destination.iterdir()), [])
        data = json.loads(self.reports()[0].read_text())
        self.assertEqual(data['state'], 'failed_needs_review')
        self.assertEqual(installer.fingerprint(Path(data['backup_component'])), self.old)
        self.assertEqual(installer.fingerprint(Path(data['old_archive'])), self.old)

    def test_rollback_refuses_modified_new_or_backup(self):
        record = self.update()
        (self.destination / 'unknown').write_bytes(b'new user data')
        before = installer.fingerprint(self.destination)
        with self.assertRaisesRegex(SystemExit, 'changed after update'):
            installer.rollback_bundle(record, host_check=lambda: None)
        self.assertEqual(installer.fingerprint(self.destination), before)
        (self.destination / 'unknown').unlink()
        backup = Path(json.loads(record.read_text())['backup_component'])
        (backup / 'unknown').write_bytes(b'backup tampering')
        with self.assertRaisesRegex(SystemExit, 'Backup changed'):
            installer.rollback_bundle(record, host_check=lambda: None)
        self.assertEqual(installer.fingerprint(self.destination), self.new)

    def test_bundle_and_backup_symlinks_are_refused(self):
        old_file = self.destination / 'Contents/PkgInfo'
        old_file.unlink()
        old_file.symlink_to(self.source / 'Contents/PkgInfo')
        with self.assertRaisesRegex(SystemExit, 'symlink'):
            self.update()
        old_file.unlink()
        old_file.write_bytes(b'BNDL????')
        outside = self.root / 'outside'
        outside.mkdir()
        self.backups.symlink_to(outside, target_is_directory=True)
        with self.assertRaisesRegex(SystemExit, 'symlink'):
            self.update()

    def test_resolved_backup_placement_and_overlap(self):
        alias = self.root / 'components-alias'
        alias.symlink_to(self.destination.parent, target_is_directory=True)
        with self.assertRaisesRegex(SystemExit, 'outside Components'):
            installer.update_bundle(self.source, self.destination, self.expected, alias / 'nested', host_check=lambda: None)
        self.assertFalse((self.destination.parent / 'nested').exists())
        with self.assertRaisesRegex(SystemExit, 'outside the source'):
            installer.update_bundle(self.source, self.destination, self.expected, self.source / 'nested', host_check=lambda: None)
        with self.assertRaisesRegex(SystemExit, 'overlap'):
            installer.update_bundle(self.destination, self.destination, self.expected, self.backups, host_check=lambda: None)

    def test_running_host_refusal_and_invalid_manifest(self):
        def running():
            raise SystemExit('Quit GarageBand')
        with self.assertRaisesRegex(SystemExit, 'GarageBand'):
            self.update(host_check=running)
        self.assertFalse(self.backups.exists())
        self.expected.write_text(json.dumps({'au': {'file_sha256': {'../wrong': '0' * 64}}}))
        with self.assertRaisesRegex(SystemExit, 'Invalid expected'):
            self.update()
        self.assertEqual(installer.fingerprint(self.destination), self.old)

    def test_default_and_install_behavior(self):
        fresh = self.root / 'Components/Fresh.component'
        with mock.patch.multiple(installer, ROOT=self.root, SOURCE=self.source, DESTINATION=fresh):
            with mock.patch('sys.argv', ['install_au.py']), contextlib.redirect_stdout(io.StringIO()):
                installer.main()
            self.assertFalse(fresh.exists())
            with mock.patch('sys.argv', ['install_au.py', '--install']), contextlib.redirect_stdout(io.StringIO()):
                installer.main()
            self.assertEqual(installer.fingerprint(fresh), self.new)
            with mock.patch('sys.argv', ['install_au.py']), contextlib.redirect_stdout(io.StringIO()):
                installer.main()
            (fresh / 'unknown').write_bytes(b'do not replace')
            with mock.patch('sys.argv', ['install_au.py', '--install']), contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'refusing to overwrite'):
                    installer.main()

    def test_explicit_source_installs_exact_package_without_default_build_source(self):
        packaged = self.root / 'packaged/TetoricaSPU.component'
        self.bundle(packaged, b'exact packaged candidate')
        expected = installer.fingerprint(packaged)
        fresh = self.root / 'Components/Fresh.component'
        missing_default = self.root / 'build/macos/out/Missing.component'
        with mock.patch.multiple(installer, ROOT=self.root, SOURCE=missing_default, DESTINATION=fresh):
            arguments = ['install_au.py', '--source', str(packaged)]
            with mock.patch('sys.argv', arguments), contextlib.redirect_stdout(io.StringIO()):
                installer.main()
            self.assertFalse(fresh.exists())  # Inspection remains read-only.
            with mock.patch('sys.argv', arguments + ['--install']), contextlib.redirect_stdout(io.StringIO()):
                installer.main()
        self.assertEqual(installer.fingerprint(fresh), expected)
        self.assertEqual(installer.fingerprint(packaged), expected)
        self.assertFalse(missing_default.exists())
        report = json.loads((self.root / 'build/au-install.json').read_text())
        self.assertEqual(report['source'], str(packaged))
        self.assertEqual(report['file_sha256'], expected)

    def test_explicit_source_update_uses_selected_package_and_retains_old_backup(self):
        packaged = self.root / 'packaged/TetoricaSPU.component'
        self.bundle(packaged, b'distinct packaged update')
        expected = installer.fingerprint(packaged)
        arguments = ['install_au.py', '--source', str(packaged), '--update',
                     '--expected', str(self.expected), '--backup-root', str(self.backups)]
        with mock.patch.multiple(installer, ROOT=self.root.resolve(), SOURCE=self.source,
                                 DESTINATION=self.destination, ensure_no_hosts=lambda: None):
            with mock.patch('sys.argv', arguments), contextlib.redirect_stdout(io.StringIO()):
                installer.main()
        self.assertEqual(installer.fingerprint(self.destination), expected)
        self.assertEqual(installer.fingerprint(self.source), self.new)
        record = json.loads(self.reports()[0].read_text())
        self.assertEqual(record['source'], str(packaged.resolve()))
        self.assertEqual(record['new_sha256'], expected)
        self.assertEqual(installer.fingerprint(Path(record['backup_component'])), self.old)

    def test_explicit_source_is_rejected_with_rollback_before_any_operation(self):
        arguments = ['install_au.py', '--source', str(self.source), '--rollback', 'unused.json']
        with mock.patch.object(installer, 'rollback_bundle') as rollback:
            with mock.patch('sys.argv', arguments), contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as error:
                    installer.main()
            self.assertEqual(error.exception.code, 2)
            rollback.assert_not_called()
        self.assertEqual(installer.fingerprint(self.destination), self.old)

    def test_explicit_install_source_ancestor_cannot_recursively_copy_itself(self):
        nested = self.source / 'nested.component'
        with mock.patch.multiple(installer, ROOT=self.root, DESTINATION=nested):
            with mock.patch('sys.argv', ['install_au.py', '--source', str(self.source), '--install']), \
                    contextlib.redirect_stdout(io.StringIO()):
                with self.assertRaisesRegex(SystemExit, 'must not overlap'):
                    installer.main()
        self.assertFalse(nested.exists())
        self.assertEqual(installer.fingerprint(self.source), self.new)


if __name__ == '__main__':
    unittest.main()
