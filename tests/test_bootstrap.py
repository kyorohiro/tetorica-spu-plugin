"""Exercise SDK linking only in fresh temporary directories, without fetching."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest import mock

SCRIPT = Path(__file__).resolve().parents[1] / 'scripts' / 'bootstrap.py'
SPEC = importlib.util.spec_from_file_location('tetorica_bootstrap', SCRIPT)
bootstrap = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bootstrap)


class BootstrapLinks(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.target = self.root / 'pinned SDK'
        self.target.mkdir()
        (self.target / 'sentinel').write_bytes(b'owned SDK bytes')
        self.link = self.root / 'SDK link'

    def test_create_and_check_exact_directory_link(self):
        bootstrap._ensure_sdk_link(self.link, self.target, windows=os.name == 'nt')
        self.assertEqual(self.link.resolve(strict=True), self.target.resolve())
        bootstrap._ensure_sdk_link(self.link, self.target, check=True)
        bootstrap._ensure_sdk_link(self.link, self.target)
        self.assertEqual((self.target / 'sentinel').read_bytes(), b'owned SDK bytes')

    def test_check_never_creates_missing_link(self):
        with self.assertRaises(SystemExit):
            bootstrap._ensure_sdk_link(self.link, self.target, check=True)
        self.assertFalse(os.path.lexists(self.link))

    def test_foreign_directory_and_file_are_never_overwritten(self):
        for kind in ('file', 'directory'):
            with self.subTest(kind=kind):
                path = self.root / kind
                if kind == 'file':
                    path.write_bytes(b'foreign data')
                else:
                    path.mkdir()
                    (path / 'sentinel').write_bytes(b'foreign data')
                with self.assertRaises(SystemExit):
                    bootstrap._ensure_sdk_link(path, self.target, windows=True)
                self.assertEqual((path if kind == 'file' else path / 'sentinel').read_bytes(), b'foreign data')

    def test_foreign_or_dangling_link_is_never_replaced(self):
        other = self.root / 'other'
        other.mkdir()
        for target in (other, self.root / 'missing target'):
            with self.subTest(target=target):
                link = self.root / ('foreign' if target == other else 'dangling')
                try:
                    link.symlink_to(target, target_is_directory=True)
                except OSError as error:
                    if os.name == 'nt' and getattr(error, 'winerror', None) == 1314:
                        self.skipTest('Symlink privilege unavailable; real junction covered separately')
                    raise
                original = os.readlink(link)
                with self.assertRaises(SystemExit):
                    bootstrap._ensure_sdk_link(link, self.target, windows=True)
                self.assertEqual(os.readlink(link), original)

    def test_only_windows_symlink_privilege_failure_uses_junction(self):
        denied = OSError('symlink privilege unavailable')
        denied.winerror = 1314
        original_symlink = Path.symlink_to
        native_junction = bootstrap._create_windows_junction
        def junction(link, target):
            if os.name == 'nt':
                native_junction(link, target)
            else:
                original_symlink(link, target, target_is_directory=True)
        with mock.patch.object(Path, 'symlink_to', side_effect=denied), \
             mock.patch.object(bootstrap, '_create_windows_junction', side_effect=junction) as fallback:
            bootstrap._ensure_sdk_link(self.link, self.target, windows=True)
        fallback.assert_called_once_with(self.link, self.target)
        self.assertEqual(self.link.resolve(), self.target.resolve())
        for windows, code in ((False, 1314), (True, 5), (True, 183)):
            error = OSError('other linking failure')
            error.winerror = code
            with self.subTest(windows=windows, code=code), \
                 mock.patch.object(Path, 'symlink_to', side_effect=error), \
                 mock.patch.object(bootstrap, '_create_windows_junction') as fallback:
                with self.assertRaises(OSError):
                    bootstrap._ensure_sdk_link(self.root / f'new-{windows}-{code}', self.target, windows=windows)
                fallback.assert_not_called()

    def test_failed_junction_postcondition_is_rejected(self):
        denied = OSError('no privilege')
        denied.winerror = 1314
        with mock.patch.object(Path, 'symlink_to', side_effect=denied), \
             mock.patch.object(bootstrap, '_create_windows_junction'):
            with self.assertRaises(SystemExit):
                bootstrap._ensure_sdk_link(self.link, self.target, windows=True)

    def test_missing_pinned_target_does_not_create_anything(self):
        with self.assertRaises(SystemExit):
            bootstrap._ensure_sdk_link(self.link, self.root / 'missing', windows=True)
        self.assertFalse(os.path.lexists(self.link))

    def test_junction_command_keeps_paths_out_of_cmd_code(self):
        link = self.root / 'link & %PATH% ! ^ Unicode 日本語'
        target = self.root / 'target & %COMSPEC% ! ^ SDK'
        with mock.patch.object(subprocess, 'run') as run:
            bootstrap._create_windows_junction(link, target)
        command = run.call_args.args[0]
        env = run.call_args.kwargs['env']
        self.assertNotIn(str(link), command)
        self.assertNotIn(str(target), command)
        self.assertIn('/d /v:off /s /c', command)
        self.assertIn('"%TETORICA_BOOTSTRAP_LINK%"', command)
        self.assertEqual(env['TETORICA_BOOTSTRAP_LINK'], str(link))
        self.assertEqual(env['TETORICA_BOOTSTRAP_TARGET'], str(target))
        self.assertTrue(run.call_args.kwargs['check'])
        for name in ('bad"name', 'bad\nname'):
            with self.assertRaises(ValueError):
                bootstrap._create_windows_junction(self.root / name, target)


if __name__ == '__main__':
    unittest.main()
