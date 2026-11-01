"""Portable AU discovery policy tests; never launch services or audio hosts."""
import contextlib
import importlib.util
import io
import os
from pathlib import Path
import plistlib
import signal
import subprocess
import tempfile
import unittest
from unittest import mock


SPEC = importlib.util.spec_from_file_location(
    'validate_au', Path(__file__).parents[1] / 'scripts/validate_au.py')
validator = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validator)


class AuDiscoveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='tetorica-au-discovery-')
        self.addCleanup(temporary.cleanup)
        self.home = Path(temporary.name)
        self.components = self.home / validator.COMPONENTS_RELATIVE
        self.bundle = self.components / validator.COMPONENT_NAME
        self.plist = self.bundle / 'Contents/Info.plist'
        self.plist.parent.mkdir(parents=True)
        self.plist.write_bytes(plistlib.dumps({'AudioComponents': [{
            'type': 'aumu', 'subtype': 'Tspu', 'manufacturer': 'Tetr'}]}))
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(validator.Path, 'home', return_value=self.home).start()
        mock.patch.object(validator.getpass, 'getuser', return_value='test-runner').start()
        mock.patch.object(validator.os, 'getuid', return_value=501).start()
        self.run = mock.patch.object(validator.subprocess, 'run').start()
        self.run.return_value = subprocess.CompletedProcess([], 0, '', '')
        self.sleep = mock.patch.object(validator.time, 'sleep').start()
        self.touch = mock.patch.object(validator.os, 'utime').start()
        self.kill = mock.patch.object(validator.os, 'kill').start()
        self.output = io.StringIO()
        stdout_context = contextlib.redirect_stdout(self.output)
        stdout_context.__enter__()
        self.addCleanup(stdout_context.__exit__, None, None, None)

    def test_local_refresh_is_rejected_before_any_service_or_file_mutation(self):
        for value in (None, 'false', '1'):
            with self.subTest(value=value), mock.patch.dict(os.environ, {}, clear=True):
                if value is not None:
                    os.environ['GITHUB_ACTIONS'] = value
                with self.assertRaisesRegex(SystemExit, 'restricted to GitHub Actions'):
                    validator.refresh_ci_registry()
        self.run.assert_not_called()
        self.kill.assert_not_called()
        self.sleep.assert_not_called()
        self.touch.assert_not_called()

    def test_missing_bundle_is_rejected_before_refresh(self):
        self.plist.unlink()
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            with self.assertRaisesRegex(SystemExit, 'without the installed AU'):
                validator.refresh_ci_registry()
        self.run.assert_not_called()
        self.touch.assert_not_called()

    def test_components_symlink_is_never_touched(self):
        alias_home = self.home / 'alias-home'
        alias_parent = alias_home / validator.COMPONENTS_RELATIVE.parent
        alias_parent.mkdir(parents=True)
        (alias_parent / 'Components').symlink_to(self.components, target_is_directory=True)
        with mock.patch.object(validator.Path, 'home', return_value=alias_home), \
                mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            with self.assertRaisesRegex(SystemExit, 'without the installed AU'):
                validator.refresh_ci_registry()
        self.run.assert_not_called()
        self.touch.assert_not_called()

    def test_ci_refresh_without_registrar_only_touches_known_directory(self):
        events = mock.Mock()
        events.attach_mock(self.run, 'run')
        events.attach_mock(self.kill, 'kill')
        events.attach_mock(self.sleep, 'sleep')
        events.attach_mock(self.touch, 'touch')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            validator.refresh_ci_registry()
        ps_call = mock.call.run(['/bin/ps', '-axo', 'pid=,uid=,comm='],
                                capture_output=True, text=True, timeout=5)
        self.assertEqual(events.mock_calls, [ps_call, ps_call,
                                           mock.call.sleep(1.1), mock.call.touch(self.components, None)])

    def test_registrar_failure_is_not_masked_by_touch_or_retry(self):
        self.run.return_value = subprocess.CompletedProcess([], 2, '', 'permission denied')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            with self.assertRaisesRegex(SystemExit, 'permission denied'):
                validator.refresh_ci_registry()
        self.sleep.assert_not_called()
        self.touch.assert_not_called()

    @staticmethod
    def process_list(text):
        return subprocess.CompletedProcess([], 0, text, '')

    def test_exact_current_uid_full_name_capture_and_recheck_excludes_every_other_process(self):
        own = '409 501 /System/Library/Frameworks/AudioToolbox.framework/AudioComponentRegistrar\n'
        unrelated = ('12 0 /System/Library/Frameworks/AudioToolbox.framework/AudioComponentRegistrar\n'
                     '22 502 /System/Library/Frameworks/AudioToolbox.framework/AudioComponentRegistrar\n'
                     '33 501 /System/Library/Frameworks/AudioToolbox.framework/AudioComponentRegistrarHelper\n'
                     '44 501 /Applications/AudioUnitHost\n'
                     '1 501 /System/Library/Frameworks/AudioToolbox.framework/AudioComponentRegistrar\n')
        replacement = '900 501 /System/Library/Frameworks/AudioToolbox.framework/AudioComponentRegistrar\n'
        self.run.side_effect = [self.process_list(own + unrelated), self.process_list(own + unrelated),
                                self.process_list(replacement + unrelated)]
        events = mock.Mock()
        events.attach_mock(self.run, 'run')
        events.attach_mock(self.kill, 'kill')
        events.attach_mock(self.sleep, 'sleep')
        events.attach_mock(self.touch, 'touch')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            validator.refresh_ci_registry()
        self.kill.assert_called_once_with(409, signal.SIGKILL)
        self.assertEqual(self.run.call_count, 3)
        self.assertEqual([call[0] for call in events.mock_calls],
                         ['run', 'run', 'kill', 'run', 'sleep', 'touch'])
        self.assertIn('original CI registrar PIDs gone=[409]', self.output.getvalue())
        self.assertIn('current same-user registrar PIDs=[900]', self.output.getvalue())

    def test_recheck_changed_uid_or_name_does_not_signal_reused_pid(self):
        own = '409 501 /system/AudioComponentRegistrar\n'
        for changed in ('409 0 /system/AudioComponentRegistrar\n', '409 501 /system/AudioHost\n', ''):
            with self.subTest(changed=changed):
                self.run.side_effect = [self.process_list(own), self.process_list(changed),
                                        self.process_list(changed)]
                with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
                    validator.refresh_ci_registry()
        self.kill.assert_not_called()

    def test_registrar_that_will_not_exit_fails_before_touch_or_auval(self):
        self.run.return_value = self.process_list('409 501 /system/AudioComponentRegistrar\n')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}), \
                mock.patch.object(validator.time, 'monotonic', side_effect=[0, 6]):
            with self.assertRaisesRegex(SystemExit, 'did not exit within 5 seconds: \\[409\\]'):
                validator.refresh_ci_registry()
        self.kill.assert_called_once_with(409, signal.SIGKILL)
        self.sleep.assert_not_called()
        self.touch.assert_not_called()
        self.assertTrue(all(call.args[0][0] == '/bin/ps' for call in self.run.call_args_list))

    def test_bounded_wait_confirms_original_pid_gone_and_never_kills_replacement(self):
        own = self.process_list('409 501 /system/AudioComponentRegistrar\n')
        replacement = self.process_list('900 501 /system/AudioComponentRegistrar\n')
        self.run.side_effect = [own, own, own, replacement]
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}), \
                mock.patch.object(validator.time, 'monotonic', side_effect=[0, 0.01]):
            validator.refresh_ci_registry()
        self.kill.assert_called_once_with(409, signal.SIGKILL)
        self.assertEqual(self.sleep.call_args_list, [mock.call(0.1), mock.call(1.1)])

    def test_vanished_pid_before_signal_is_harmless_but_permission_denied_is_not(self):
        own = self.process_list('409 501 /system/AudioComponentRegistrar\n')
        self.run.side_effect = [own, own, self.process_list('')]
        self.kill.side_effect = ProcessLookupError('already exited')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            validator.refresh_ci_registry()
        self.touch.assert_called_once_with(self.components, None)
        self.touch.reset_mock()
        self.sleep.reset_mock()
        self.run.side_effect = [own, own]
        self.kill.side_effect = PermissionError('permission denied')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            with self.assertRaisesRegex(SystemExit, 'Cannot stop captured CI registrar PID409'):
                validator.refresh_ci_registry()
        self.touch.assert_not_called()
        self.sleep.assert_not_called()

    def test_root_is_rejected_without_enumeration_or_signals(self):
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}), \
                mock.patch.object(validator.os, 'getuid', return_value=0):
            with self.assertRaisesRegex(SystemExit, 'root Audio Component service'):
                validator.refresh_ci_registry()
        self.run.assert_not_called()
        self.kill.assert_not_called()
        self.touch.assert_not_called()

    def test_unparseable_process_list_fails_closed_without_signals(self):
        self.run.return_value = self.process_list('unexpected process identity format\n')
        with mock.patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
            with self.assertRaisesRegex(SystemExit, 'Cannot parse CI process identity'):
                validator.refresh_ci_registry()
        self.kill.assert_not_called()
        self.touch.assert_not_called()

    def test_discovery_failure_keeps_final_listing_and_does_not_validate(self):
        self.run.return_value = subprocess.CompletedProcess([], 0, 'Only system components\n', 'listing warning\n')
        with mock.patch.object(validator.time, 'monotonic', side_effect=[0, 0, 31]), \
                mock.patch.object(validator, 'discovery_diagnostics') as diagnostics:
            with self.assertRaisesRegex(SystemExit, 'not discovered within 30 seconds'):
                validator.main([])
        diagnostics.assert_called_once_with(self.run.return_value)
        self.assertEqual(self.run.call_args.args[0], ['/usr/bin/auval', '-a'])
        self.assertEqual(self.run.call_count, 1)
        self.touch.assert_not_called()

    def test_real_validation_failure_and_name_warning_are_never_retried(self):
        for result, expected in ((subprocess.CompletedProcess([], 7, 'bad API\n', ''), 7),
                                 (subprocess.CompletedProcess([], 0,
                                     'Preset name is not retained in retrieved class data', ''),
                                  'AU preset-name validation failed despite auval exit 0'),
                                 (subprocess.CompletedProcess([], 0, 'AU VALIDATION SUCCEEDED\n', ''), 0)):
            with self.subTest(expected=expected):
                self.run.reset_mock()
                self.run.side_effect = [subprocess.CompletedProcess([], 0, ' aumu Tspu Tetr - TetoricaSPU\n', ''), result]
                with mock.patch.object(validator.time, 'monotonic', side_effect=[0, 0]):
                    with self.assertRaises(SystemExit) as outcome:
                        validator.main([])
                self.assertEqual(outcome.exception.code, expected)
                self.assertEqual(self.run.call_count, 2)
                self.assertEqual(self.run.call_args.args[0], ['/usr/bin/auval', '-v', 'aumu', 'Tspu', 'Tetr'])
        self.sleep.assert_not_called()
        self.touch.assert_not_called()

    def test_failure_diagnostics_are_read_only_bounded_and_show_full_final_listing(self):
        stderr = io.StringIO()
        listing = subprocess.CompletedProcess([], 0, 'final component listing\n', 'final listing stderr\n')
        with contextlib.redirect_stderr(stderr):
            validator.discovery_diagnostics(listing)
        self.assertIn('final component listing', self.output.getvalue())
        self.assertIn('final listing stderr', stderr.getvalue())
        self.assertIn('AudioComponents', self.output.getvalue())
        commands = [call.args[0] for call in self.run.call_args_list]
        self.assertEqual([Path(command[0]).name for command in commands],
                         ['uname', 'sw_vers', 'pgrep', 'xattr', 'codesign', 'codesign', 'log'])
        self.assertTrue(all(call.kwargs['timeout'] == 5 for call in self.run.call_args_list))
        self.assertEqual(commands[-3], ['/usr/bin/codesign', '--verify', '--strict',
                                       '--all-architectures', str(self.bundle)])
        self.assertEqual(commands[-2], ['/usr/bin/codesign', '-dv', '--verbose=2', str(self.bundle)])
        self.assertIn('process == "AudioComponentRegistrar"', commands[-1])
        self.touch.assert_not_called()

    def test_unavailable_diagnostic_does_not_replace_original_failure(self):
        self.run.side_effect = subprocess.TimeoutExpired('diagnostic', 5)
        validator._diagnostic_command('timed out', ['/usr/bin/log'])
        self.assertIn('Diagnostic unavailable', self.output.getvalue())


if __name__ == '__main__':
    unittest.main()
