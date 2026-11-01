"""Credential lifecycle tests use fake values and mocked Apple commands only."""
import base64
import contextlib
import importlib.util
import io
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    'ci_macos_signing', Path(__file__).parents[1] / 'scripts/ci_macos_signing.py')
signing = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(signing)
TEAM = '5H7KW7PC7C'
FINGERPRINT = '1234567890ABCDEF1234567890ABCDEF12345678'
IDENTITY = f'Developer ID Application: Example Test Organization ({TEAM})'


class FakeApple:
    """Only emulate keychain files/search lists; never invoke a native tool."""
    def __init__(self, root):
        self.calls = []
        self.login = root / 'existing-login.keychain-db'
        self.login.write_bytes(b'leave existing user keychain alone')
        self.search = [str(self.login)]
        self.auto_add_keychain = False
        self.identities = f'  1) {FINGERPRINT} "{IDENTITY}"\n     1 valid identities found\n'
        self.fail_command = None
        self.failure_kind = 'exit'
        self.failed = False
        self.import_permissions = None
        self.leak = 'fake-sensitive-tool-output-NEVER-PRINT'

    def __call__(self, arguments, **kwargs):
        self.calls.append((arguments, kwargs))
        self_command = arguments[1] if arguments[0] == '/usr/bin/security' else arguments[2]
        if self_command == 'create-keychain':
            path = Path(arguments[-1])
            path.write_bytes(b'fake private keychain bytes')
            path.chmod(0o600)
            lock = path.parent / '.flD3051040'
            lock.write_bytes(b'fake Apple AtomicFile lock')
            lock.chmod(0o600)
            if self.auto_add_keychain:
                self.search.append(str(path))
        if self.fail_command == self_command and not self.failed:
            self.failed = True
            if self.failure_kind == 'timeout':
                raise subprocess.TimeoutExpired(arguments, 180, output=self.leak, stderr=self.leak)
            if self.failure_kind == 'oserror':
                raise OSError(self.leak)
            return subprocess.CompletedProcess(arguments, 99, self.leak, self.leak)
        output = ''
        if self_command == 'list-keychains':
            if '-s' in arguments:
                self.search = arguments[arguments.index('-s') + 1:]
            else:
                output = '\n'.join(json.dumps(item) for item in self.search)
        elif self_command == 'import':
            p12 = Path(arguments[2])
            self.import_permissions = (stat.S_IMODE(p12.stat().st_mode),
                                       stat.S_IMODE(p12.parent.stat().st_mode))
            assert p12.read_bytes() == b'fake PKCS12 test bytes'
        elif self_command == 'find-identity':
            output = self.identities
        elif self_command == 'store-credentials':
            output = self.leak  # Successful tool output is also never logged.
        elif self_command == 'delete-keychain':
            Path(arguments[-1]).unlink(missing_ok=True)
        return subprocess.CompletedProcess(arguments, 0, output, '')


class SigningTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ci-signing-test-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.report = self.root / 'macos-signing.json'
        self.environ = {
            'GITHUB_ACTIONS': 'true', 'RUNNER_TEMP': str(self.root),
            'GITHUB_RUN_ID': '123456', 'GITHUB_RUN_ATTEMPT': '1', 'GITHUB_JOB': 'sign',
            'EXPECTED_TEAM_ID': TEAM,
            'BUILD_CERTIFICATE_BASE64': base64.b64encode(b'fake PKCS12 test bytes').decode(),
            'P12_PASSWORD': 'fake-p12-password-NEVER-PRINT',
            'APPLE_ID': 'fake-account@example.invalid', 'APPLE_TEAM_ID': TEAM,
            'APPLE_APP_SPECIFIC_PASSWORD': 'fake-app-password-NEVER-PRINT',
        }
        self.apple = FakeApple(self.root)
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(signing.sys, 'platform', 'darwin').start()
        mock.patch.object(signing.subprocess, 'run', side_effect=self.apple).start()

    def prepare(self):
        return signing.prepare(self.report, environ=self.environ)

    def cleanup(self, environ=None):
        return signing.cleanup(self.report, environ=self.environ if environ is None else environ)

    def owned_directories(self):
        return list(self.root.glob(signing.DIRECTORY_PREFIX + '*'))

    def test_prepare_uses_isolated_keychain_exact_identity_and_public_report(self):
        report = self.prepare()
        keychain = Path(report['keychain_path'])
        self.assertEqual(report['status'], 'prepared')
        self.assertEqual(report['identity_sha1'], FINGERPRINT)
        self.assertEqual(report['signing_identity'], IDENTITY)
        self.assertEqual(report['team_id'], TEAM)
        self.assertEqual(json.loads(self.report.read_text()), report)
        self.assertEqual(self.apple.import_permissions, (0o600, 0o700))
        self.assertFalse((keychain.parent / signing.P12_NAME).exists())
        self.assertEqual(self.apple.search, [str(self.apple.login), str(keychain)])
        self.assertEqual(report['keychain_companions'], ['.flD3051040'])
        journal = Path(report['state'])
        self.assertEqual(stat.S_IMODE(journal.stat().st_mode), 0o600)
        self.assertEqual(stat.S_IMODE(self.report.stat().st_mode), 0o600)
        public = self.report.read_text() + journal.read_text()
        for name in signing.SECRET_NAMES:
            if name != 'APPLE_TEAM_ID':
                self.assertNotIn(self.environ[name], public)
        commands = [call[0] for call in self.apple.calls]
        self.assertTrue(all('-A' not in command for command in commands))
        self.assertTrue(all('default-keychain' not in command for command in commands))
        command = next(command for command in commands if 'store-credentials' in command)
        self.assertEqual(command[3], report['notary_profile'])
        self.assertEqual(command[command.index('--keychain') + 1], str(keychain))
        self.assertEqual(command[command.index('--team-id') + 1], TEAM)
        self.assertEqual(command[command.index('--password') + 1],
                         self.environ['APPLE_APP_SPECIFIC_PASSWORD'])
        for command, options in self.apple.calls:
            self.assertLessEqual(options['timeout'], 180)
            self.assertEqual(options['stdin'], subprocess.DEVNULL)
            self.assertTrue(options['capture_output'])
            self.assertFalse(options['check'])
            self.assertFalse(set(options['env']) & set(signing.SECRET_NAMES))
        keychain_password = commands[0][3]
        self.assertGreaterEqual(len(keychain_password), 48)
        self.assertNotIn(keychain_password, public)

    def test_ascii_whitespace_wrapped_base64_secret_is_accepted_without_relaxing_validation(self):
        encoded = self.environ['BUILD_CERTIFICATE_BASE64']
        self.environ['BUILD_CERTIFICATE_BASE64'] = ' \t\n' + '\r\n'.join(
            encoded[index:index + 8] for index in range(0, len(encoded), 8)) + '\n'
        report = self.prepare()
        self.assertEqual(json.loads(self.report.read_text())['status'], 'prepared')
        self.assertEqual(self.apple.import_permissions, (0o600, 0o700))
        self.cleanup()
        self.report.unlink()
        self.environ['BUILD_CERTIFICATE_BASE64'] = encoded + '\u00a0'
        with self.assertRaisesRegex(signing.SigningError, 'not valid base64'):
            self.prepare()
        self.assertFalse(self.owned_directories())

    def test_prepare_preserves_search_list_and_does_not_duplicate_an_auto_added_keychain(self):
        self.apple.auto_add_keychain = True
        report = self.prepare()
        self.assertEqual(self.apple.search,
                         [str(self.apple.login), report['keychain_path']])
        self.assertFalse(any('-s' in command and 'list-keychains' in command
                             for command, _ in self.apple.calls))
        self.cleanup()

    def test_cleanup_accepts_only_owned_apple_lock_and_atomic_write_companions(self):
        self.assertEqual(signing.KEYCHAIN_LOCK_NAMES, {'.flD3051040', '.fl25C0AF36'})
        report = self.prepare()
        directory = Path(report['state']).parent
        names = ('.fl25C0AF36', 'signing.keychain-db.sb-ABCDEFGH',
                 'signing.keychain.sb-abcdef12', 'signing.keychain-db-wal',
                 'signing.keychain-db-shm')
        for name in names:
            (directory / name).write_bytes(b'fake Apple companion bytes')
        cleaned = self.cleanup()
        self.assertEqual(set(cleaned['removed_keychain_companions']), set(names) | {'.flD3051040'})
        self.assertFalse(directory.exists())
        self.assertTrue(self.apple.login.is_file())

    def test_cleanup_refuses_unknown_lock_names_and_linked_known_companions(self):
        report = self.prepare()
        directory = Path(report['state']).parent
        unknown = directory / '.flFFFFFFFF'
        unknown.write_bytes(b'unrecognized lock')
        before = len(self.apple.calls)
        with self.assertRaisesRegex(signing.SigningError, 'unexpected file'):
            self.cleanup()
        self.assertEqual(len(self.apple.calls), before)
        unknown.unlink()
        lock = directory / '.fl25C0AF36'
        lock.symlink_to(self.apple.login)
        with self.assertRaisesRegex(signing.SigningError, 'linked credential'):
            self.cleanup()
        self.assertEqual(len(self.apple.calls), before)
        lock.unlink()
        os.link(self.apple.login, lock)
        with self.assertRaisesRegex(signing.SigningError, 'linked credential'):
            self.cleanup()
        self.assertEqual(len(self.apple.calls), before)
        lock.unlink()
        self.cleanup()

    def test_cleanup_is_idempotent_without_secrets_and_preserves_other_keychains(self):
        report = self.prepare()
        other = self.root / 'concurrently-added.keychain-db'
        other.write_bytes(b'other job')
        self.apple.search.append(str(other))
        public_environ = {key: value for key, value in self.environ.items()
                          if key not in signing.SECRET_NAMES and key != 'EXPECTED_TEAM_ID'}
        cleaned = self.cleanup(public_environ)
        self.assertEqual(cleaned['status'], 'cleaned')
        self.assertFalse(Path(report['keychain_path']).parent.exists())
        self.assertEqual(self.apple.search, [str(self.apple.login), str(other)])
        self.assertEqual(self.apple.login.read_bytes(), b'leave existing user keychain alone')
        self.assertEqual(other.read_bytes(), b'other job')
        before = len(self.apple.calls)
        self.assertEqual(self.cleanup(public_environ)['status'], 'cleaned')
        self.assertEqual(len(self.apple.calls), before)

    def test_every_prepare_command_failure_cleans_partial_credentials(self):
        for operation in ('create-keychain', 'set-keychain-settings', 'unlock-keychain',
                          'import', 'set-key-partition-list', 'find-identity',
                          'store-credentials', 'list-keychains'):
            with self.subTest(operation=operation):
                self.report.unlink(missing_ok=True)
                self.apple.fail_command, self.apple.failed = operation, False
                with self.assertRaises(signing.SigningError) as caught:
                    self.prepare()
                self.assertNotIn(self.apple.leak, str(caught.exception))
                self.assertFalse(self.owned_directories())
                self.assertEqual(self.apple.search, [str(self.apple.login)])
                self.assertEqual(json.loads(self.report.read_text())['status'], 'cleaned')

    def test_timeout_and_exception_output_are_sanitized_and_cleanup_runs(self):
        for kind in ('timeout', 'oserror'):
            with self.subTest(kind=kind):
                self.report.unlink(missing_ok=True)
                self.apple.fail_command, self.apple.failed = 'store-credentials', False
                self.apple.failure_kind = kind
                with self.assertRaises(signing.SigningError) as caught:
                    self.prepare()
                self.assertNotIn(self.apple.leak, str(caught.exception))
                self.assertNotIn(self.environ['APPLE_APP_SPECIFIC_PASSWORD'], str(caught.exception))
                self.assertFalse(self.owned_directories())

    def test_identity_must_be_single_valid_developer_id_application_for_exact_team(self):
        invalid = (
            '  0 valid identities found\n',
            f'  1) {FINGERPRINT} "Apple Development: Example ({TEAM})"\n',
            f'  1) {FINGERPRINT} "Developer ID Installer: Example ({TEAM})"\n',
            f'  1) {FINGERPRINT} "Developer ID Application: Example (WRONGTEAM1)"\n',
            self.apple.identities + f'  2) {"A" * 40} "{IDENTITY}"\n',
        )
        for identities in invalid:
            with self.subTest(identities=identities):
                self.report.unlink(missing_ok=True)
                self.apple.identities = identities
                with self.assertRaisesRegex(signing.SigningError, 'exactly one valid'):
                    self.prepare()
                self.assertFalse(self.owned_directories())
        # Valid SHA-1s are canonicalized; other non-Developer-ID identities do not
        # create an ambiguous Developer ID Application match.
        selected = signing.select_identity(
            f'  1) {FINGERPRINT.lower()} "{IDENTITY}"\n'
            f'  2) {"A" * 40} "Apple Development: Example ({TEAM})"\n', TEAM)
        self.assertEqual(selected, (FINGERPRINT, IDENTITY))

    def test_ci_team_and_input_guards_precede_all_native_operations(self):
        cases = ({'GITHUB_ACTIONS': 'false'}, {'GITHUB_RUN_ID': ''}, {'RUNNER_TEMP': ''},
                 {'APPLE_TEAM_ID': 'WRONGTEAM1'}, {'EXPECTED_TEAM_ID': ''},
                 {'APPLE_APP_SPECIFIC_PASSWORD': ''}, {'BUILD_CERTIFICATE_BASE64': '!!invalid!!'},
                 {'BUILD_CERTIFICATE_BASE64': ''})
        for changes in cases:
            with self.subTest(changes=changes):
                environ = dict(self.environ, **changes)
                with self.assertRaises(signing.SigningError):
                    signing.prepare(self.report, environ=environ)
                self.assertFalse(self.apple.calls)
                self.assertFalse(self.owned_directories())
        with mock.patch.object(signing.sys, 'platform', 'linux'):
            with self.assertRaisesRegex(signing.SigningError, 'macOS GitHub Actions'):
                self.prepare()
        self.assertFalse(self.apple.calls)

    def test_existing_report_and_outside_report_are_never_replaced(self):
        self.report.write_text('keep existing report')
        with self.assertRaisesRegex(signing.SigningError, 'existing credential report'):
            self.prepare()
        self.assertEqual(self.report.read_text(), 'keep existing report')
        with self.assertRaisesRegex(signing.SigningError, 'directly inside RUNNER_TEMP'):
            signing.prepare(self.root.parent / 'outside.json', environ=self.environ)
        self.assertFalse(self.apple.calls)

    def test_report_symlink_and_keychain_symlink_are_refused(self):
        outside = self.root / 'outside.json'
        outside.write_text('do not read or delete')
        self.report.symlink_to(outside)
        with self.assertRaises(signing.SigningError):
            self.cleanup()
        self.report.unlink()
        report = self.prepare()
        keychain = Path(report['keychain_path'])
        keychain.unlink()
        keychain.symlink_to(self.apple.login)
        before = len(self.apple.calls)
        with self.assertRaises(signing.SigningError):
            self.cleanup()
        self.assertEqual(len(self.apple.calls), before)
        self.assertEqual(self.apple.login.read_bytes(), b'leave existing user keychain alone')

    def test_cross_run_or_tampered_journal_cannot_delete_a_keychain(self):
        report = self.prepare()
        before = len(self.apple.calls)
        with self.assertRaisesRegex(signing.SigningError, 'ownership journal'):
            self.cleanup(dict(self.environ, GITHUB_RUN_ID='999999'))
        state_path = Path(report['state'])
        state = json.loads(state_path.read_text())
        state['directory_identity'][1] += 1
        state_path.write_text(json.dumps(state))
        with self.assertRaisesRegex(signing.SigningError, 'ownership journal'):
            self.cleanup()
        self.assertTrue(Path(report['keychain_path']).is_file())
        self.assertEqual(len(self.apple.calls), before)

    def test_report_cannot_redirect_cleanup_to_an_existing_user_keychain(self):
        report = self.prepare()
        report['keychain_path'] = str(self.apple.login)
        self.report.write_text(json.dumps(report))
        before = len(self.apple.calls)
        with self.assertRaisesRegex(signing.SigningError, 'outside the owned'):
            self.cleanup()
        self.assertEqual(len(self.apple.calls), before)
        self.assertTrue(self.apple.login.is_file())

    def test_cleanup_removes_owned_private_files_when_delete_tool_fails(self):
        report = self.prepare()
        self.apple.fail_command = 'delete-keychain'
        self.assertEqual(self.cleanup()['status'], 'cleaned')
        self.assertFalse(Path(report['keychain_path']).parent.exists())

    def test_failed_search_list_cleanup_removes_secrets_and_retains_retry_journal(self):
        report = self.prepare()
        self.apple.fail_command = 'list-keychains'
        with self.assertRaisesRegex(signing.SigningError, 'cleanup needs retry'):
            self.cleanup()
        self.assertFalse(Path(report['keychain_path']).exists())
        self.assertTrue(Path(report['state']).exists())
        self.assertEqual(self.cleanup()['status'], 'cleaned')
        self.assertFalse(Path(report['state']).parent.exists())

    def test_interrupted_public_report_update_keeps_cleanup_state_readable(self):
        replace = signing.os.replace
        interrupted = False

        def interrupt_once(source, destination):
            nonlocal interrupted
            if Path(destination) == self.report and not interrupted:
                interrupted = True
                raise KeyboardInterrupt(self.apple.leak)
            return replace(source, destination)

        with mock.patch.object(signing.os, 'replace', side_effect=interrupt_once):
            with self.assertRaises(signing.SigningError) as caught:
                self.prepare()
        self.assertNotIn(self.apple.leak, str(caught.exception))
        self.assertFalse(self.owned_directories())
        self.assertEqual(json.loads(self.report.read_text())['status'], 'cleaned')

    def test_interrupted_journal_update_leaves_no_p12_and_no_native_creation(self):
        dump = signing.json.dump

        def partial_journal(value, target, **kwargs):
            if value.get('keychain_creation_attempted'):
                target.write('{partial')
                raise OSError(self.apple.leak)
            return dump(value, target, **kwargs)

        with mock.patch.object(signing.json, 'dump', side_effect=partial_journal):
            with self.assertRaises(signing.SigningError) as caught:
                self.prepare()
        self.assertNotIn(self.apple.leak, str(caught.exception))
        self.assertFalse(self.owned_directories())
        commands = [call[0] for call in self.apple.calls]
        self.assertFalse(any('create-keychain' in command for command in commands))
        self.assertEqual(json.loads(self.report.read_text())['status'], 'cleaned')

    def test_main_success_and_failure_never_log_tool_output_or_secrets(self):
        for failure in (None, 'store-credentials'):
            with self.subTest(failure=failure):
                self.report.unlink(missing_ok=True)
                self.apple.fail_command, self.apple.failed = failure, False
                output, error = io.StringIO(), io.StringIO()
                with mock.patch.dict(signing.os.environ, self.environ, clear=True), \
                        mock.patch.object(signing.sys, 'argv',
                                          ['ci_macos_signing.py', 'prepare', '--report', str(self.report)]), \
                        contextlib.redirect_stdout(output), contextlib.redirect_stderr(error):
                    result = signing.main()
                self.assertEqual(result, 1 if failure else 0)
                text = output.getvalue() + error.getvalue()
                self.assertNotIn(self.apple.leak, text)
                for name in signing.SECRET_NAMES:
                    if name != 'APPLE_TEAM_ID':
                        self.assertNotIn(self.environ[name], text)
                if not failure:
                    self.cleanup()


if __name__ == '__main__':
    unittest.main()
