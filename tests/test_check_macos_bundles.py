"""Portable inspection-policy tests. Fake bundles/tools never launch or sign code."""
import importlib.util
from pathlib import Path
import plistlib
import tempfile
import unittest


SPEC = importlib.util.spec_from_file_location('check_macos_bundles',
    Path(__file__).parents[1] / 'scripts/check_macos_bundles.py')
checker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(checker)


class MacBundleInspectionTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='tetorica-mac-inspect-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.project = self.root / 'project'
        self.output = self.root / 'out'
        (self.project / 'plugin/resources/fonts').mkdir(parents=True)
        (self.project / 'plugin/config.h').write_text(
            '#define PLUG_VERSION_STR "0.1.0"\n#define PLUG_VERSION_HEX 0x00000100\n')
        (self.project / 'CMakeLists.txt').write_text('project(tetorica_spu_plugin VERSION 0.1.0 LANGUAGES C CXX)\n')
        (self.project / 'plugin/resources/fonts/Roboto-Regular.ttf').write_bytes(b'test-font')
        (self.project / 'plugin/resources/THIRD_PARTY_NOTICES.txt').write_bytes(b'test-notices')
        for format_name, name in checker.BUNDLES:
            bundle = self.output / name
            (bundle / 'Contents/MacOS').mkdir(parents=True)
            resources = bundle / 'Contents/Resources'
            resources.mkdir()
            executable = bundle / 'Contents/MacOS/TetoricaSPU'
            executable.write_bytes(bytes.fromhex('cafebabe') + b'fake universal Mach-O')
            executable.chmod(0o755)
            info = {'CFBundleExecutable': 'TetoricaSPU', 'CFBundleVersion': '0.1.0',
                    'CFBundleShortVersionString': '0.1.0', 'LSMinimumSystemVersion': '13.0'}
            if format_name == 'au':
                info['AudioComponents'] = [{'type': 'aumu', 'subtype': 'Tspu', 'manufacturer': 'Tetr', 'version': 256}]
            if format_name == 'app':
                info['NSMainNibFile'] = 'MainMenu'
                (resources / 'MainMenu.nib').write_bytes(b'compiled-menu')
            (bundle / 'Contents/Info.plist').write_bytes(plistlib.dumps(info))
            (resources / 'Roboto-Regular.ttf').write_bytes(b'test-font')
            (resources / 'THIRD_PARTY_NOTICES.txt').write_bytes(b'test-notices')
        self.thin = None
        self.bad_signature = None
        self.external = None
        self.bad_rpath = False
        self.newer_minos = False
        self.commands = []
        self.signature_details = None
        self.reject_requirement = False

    def developer_id(self):
        detail = ('Identifier=test\n'
                  'CodeDirectory v=20500 size=100 flags=0x10000(runtime) hashes=1+1 location=embedded\n'
                  'Authority=Developer ID Application: Example Developer (ABCDE12345)\n'
                  'Authority=Developer ID Certification Authority\n'
                  'Authority=Apple Root CA\n'
                  'TeamIdentifier=ABCDE12345\n'
                  'Timestamp=Nov 2, 2026 at 12:00:00\n'
                  'CDHash=test-hash\n')
        self.signature_details = {architecture: detail for architecture in checker.REQUIRED_ARCHITECTURES}

    def fake_run(self, arguments):
        self.commands.append(arguments)
        tool, path = Path(arguments[0]).name, Path(arguments[-1])
        if tool == 'lipo':
            return 'arm64\n' if path == self.thin else 'x86_64 arm64\n'
        if tool == 'otool' and '-L' in arguments:
            return f'{path} (architecture arm64):\n\t/usr/lib/libSystem.B.dylib (compatibility version 1.0.0, current version 1.0.0)\n' + (
                f'{path} (architecture x86_64):\n\t{self.external} (compatibility version 1.0.0, current version 1.0.0)\n'
                if self.external else '')
        if tool == 'otool' and '-l' in arguments:
            minimum = '14.0' if self.newer_minos else '13.0'
            return f'Load command 0\n      cmd LC_BUILD_VERSION\n  cmdsize 32\n platform 1\n    minos {minimum}\n      sdk 26.1\n' * 2 + (
                'Load command 1\n      cmd LC_RPATH\n  cmdsize 48\n     path /Users/builder/homebrew/lib (offset 12)\n' if self.bad_rpath else '')
        if tool == 'codesign' and '--verify' in arguments:
            if path == self.bad_signature:
                raise ValueError('injected invalid resource/code signature')
            if self.reject_requirement and '--test-requirement' in arguments:
                raise ValueError('injected Apple Developer ID requirement failure')
            return ''
        if tool == 'codesign' and '-d' in arguments:
            if self.signature_details is not None:
                self.assertIn('--arch', arguments)
                return self.signature_details[arguments[arguments.index('--arch') + 1]]
            return 'Identifier=test\nSignature=adhoc\nTeamIdentifier=not set\nCDHash=test-hash\n'
        self.fail(f'Unexpected tool invocation: {arguments}')

    def inspect(self, **kwargs):
        return checker.inspect_bundles(self.output, self.project, self.fake_run, **kwargs)

    def test_valid_universal_is_integrity_only_and_never_signs_or_launches(self):
        report = self.inspect()
        self.assertTrue(report['ok'], report['errors'])
        self.assertEqual(len(report['bundles']), 3)
        self.assertEqual(report['notarization'], 'not-checked')
        for bundle in report['bundles']:
            self.assertEqual(bundle['machO'][0]['architectures'], ['arm64', 'x86_64'])
            self.assertEqual(bundle['signature']['kind'], 'ad-hoc')
        self.assertTrue(all(Path(command[0]).name in ('lipo', 'otool', 'codesign') for command in self.commands))
        self.assertFalse(any('--sign' in command for command in self.commands))
        self.assertFalse(any('--arch' in command or '--test-requirement' in command for command in self.commands))

    def test_developer_id_checks_apple_requirement_and_both_slices_of_every_binary_and_bundle(self):
        self.developer_id()
        library = self.output / 'TetoricaSPU.app/Contents/Resources/nested/helper.dylib'
        library.parent.mkdir()
        library.write_bytes(bytes.fromhex('cafebabe') + b'fake universal helper')
        report = self.inspect(expected_team_id='ABCDE12345')
        self.assertTrue(report['ok'], report['errors'])
        self.assertEqual(report['signingPolicy'], 'developer-id-application-exact-team')
        self.assertEqual(report['expectedTeamIdentifier'], 'ABCDE12345')
        self.assertEqual(report['notarization'], 'not-checked')
        requirement = checker.developer_id_requirement('ABCDE12345')
        self.assertEqual(requirement, '=anchor apple generic and '
                         'certificate leaf[field.1.2.840.113635.100.6.1.13] exists and '
                         'certificate leaf[subject.OU] = "ABCDE12345"')
        expected_paths = {self.output / name for _, name in checker.BUNDLES}
        expected_paths |= {path / 'Contents/MacOS/TetoricaSPU' for path in list(expected_paths)}
        expected_paths.add(library)
        verifications = [command for command in self.commands if '--test-requirement' in command]
        self.assertEqual({Path(command[-1]) for command in verifications}, expected_paths)
        for command in verifications:
            self.assertIn('--all-architectures', command)
            self.assertEqual(command[command.index('--test-requirement') + 1], requirement)
        for path in expected_paths:
            displays = [command for command in self.commands if '-d' in command and Path(command[-1]) == path]
            self.assertEqual({command[command.index('--arch') + 1] for command in displays},
                             checker.REQUIRED_ARCHITECTURES)
        for bundle in report['bundles']:
            for signature in [bundle['signature']] + [binary['signature'] for binary in bundle['machO']]:
                self.assertEqual(signature['kind'], 'developer-id-application')
                self.assertEqual(set(signature['architectures']), checker.REQUIRED_ARCHITECTURES)
                for evidence in signature['architectures'].values():
                    self.assertEqual(evidence['teamIdentifier'], 'ABCDE12345')
                    self.assertEqual(evidence['codeDirectoryFlags'], '0x10000')
                    self.assertTrue(evidence['hardenedRuntime'])
                    self.assertTrue(evidence['timestamp'])
        self.assertFalse(any('--sign' in command for command in self.commands))

    def test_expected_team_id_is_validated_before_any_tool_runs(self):
        for team in ('', 'abcde12345', 'ABCDE1234', 'ABCDE123456', 'ABCDE1234" or true', 123):
            with self.subTest(team=team):
                report = self.inspect(expected_team_id=team)
                self.assertFalse(report['ok'])
                self.assertIn('Expected Team ID', report['errors'][0])
                self.assertEqual(self.commands, [])

    def test_developer_id_rejects_adhoc_development_and_wrong_team_on_either_slice(self):
        report = self.inspect(expected_team_id='ABCDE12345')
        self.assertFalse(report['ok'])
        self.assertTrue(any('Developer ID Application signature' in error for error in report['errors']))
        for architecture in sorted(checker.REQUIRED_ARCHITECTURES):
            for old, new, error in (
                    ('TeamIdentifier=ABCDE12345', 'TeamIdentifier=ZYXWV98765', 'TeamIdentifier'),
                    ('Developer ID Application: Example Developer (ABCDE12345)',
                     'Apple Development: Example Developer (ABCDE12345)', 'Developer ID Application authority'),
                    ('Example Developer (ABCDE12345)', 'Example Developer (ZYXWV98765)',
                     'Developer ID Application authority')):
                with self.subTest(architecture=architecture, replacement=new):
                    self.developer_id()
                    self.signature_details[architecture] = self.signature_details[architecture].replace(old, new)
                    report = self.inspect(expected_team_id='ABCDE12345')
                    self.assertFalse(report['ok'])
                    self.assertTrue(any(architecture in message and error in message for message in report['errors']))

    def test_developer_id_requires_runtime_flag_bit_not_a_runtime_label(self):
        for value in ('0x0(runtime)', '0x1000', 'unknown(runtime)'):
            with self.subTest(flags=value):
                self.developer_id()
                self.signature_details['x86_64'] = self.signature_details['x86_64'].replace('0x10000(runtime)', value)
                report = self.inspect(expected_team_id='ABCDE12345')
                self.assertFalse(report['ok'])
                self.assertTrue(any('x86_64' in error and 'hardened runtime' in error for error in report['errors']))
        self.developer_id()
        self.signature_details['x86_64'] = self.signature_details['x86_64'].replace('0x10000(runtime)', '0x11000(runtime,library)')
        self.assertTrue(self.inspect(expected_team_id='ABCDE12345')['ok'])

    def test_developer_id_requires_trusted_timestamp_on_both_slices(self):
        for replacement in ('Signed Time=Nov 2, 2026', 'Timestamp=', 'Timestamp=none', ''):
            with self.subTest(timestamp=replacement):
                self.developer_id()
                self.signature_details['x86_64'] = self.signature_details['x86_64'].replace(
                    'Timestamp=Nov 2, 2026 at 12:00:00', replacement)
                report = self.inspect(expected_team_id='ABCDE12345')
                self.assertFalse(report['ok'])
                self.assertTrue(any('x86_64' in error and 'trusted Timestamp' in error for error in report['errors']))

    def test_developer_id_requirement_failure_cannot_be_overridden_by_display_metadata(self):
        self.developer_id()
        self.reject_requirement = True
        report = self.inspect(expected_team_id='ABCDE12345')
        self.assertFalse(report['ok'])
        self.assertTrue(any('Developer ID requirement failure' in error for error in report['errors']))
        self.assertFalse(any('-d' in command for command in self.commands))

    def test_every_macho_is_checked_including_resource_subdirectories(self):
        library = self.output / 'TetoricaSPU.app/Contents/Resources/nested/helper.dylib'
        library.parent.mkdir()
        library.write_bytes(bytes.fromhex('cffaedfe') + b'thin-arm-library')
        self.thin = library
        report = self.inspect()
        self.assertFalse(report['ok'])
        self.assertTrue(any('helper.dylib is not exactly arm64' in error for error in report['errors']))

    def test_x86_dependency_and_runtime_path_cannot_depend_on_build_machine(self):
        for external in ('/opt/homebrew/lib/libbad.dylib', '/System/Library/../../Users/libbad.dylib', '@rpath/libbad.dylib'):
            self.external = external
            self.assertFalse(self.inspect()['ok'])
        self.external = None
        self.bad_rpath = True
        self.assertTrue(any('runtime search path' in error for error in self.inspect()['errors']))

    def test_invalid_bundle_signature_is_not_confused_with_adhoc_metadata(self):
        self.bad_signature = self.output / 'TetoricaSPU.component'
        report = self.inspect()
        self.assertFalse(report['ok'])
        self.assertTrue(any('invalid resource/code signature' in error for error in report['errors']))

    def test_external_and_dangling_symlinks_are_rejected_but_internal_alias_is_retained(self):
        resources = self.output / 'TetoricaSPU.app/Contents/Resources'
        link = resources / 'alias'
        link.symlink_to('Roboto-Regular.ttf')
        report = self.inspect()
        self.assertTrue(report['ok'], report['errors'])
        self.assertEqual(report['bundles'][0]['symlinks'], [{'path': 'Contents/Resources/alias', 'target': 'Roboto-Regular.ttf'}])
        link.unlink()
        link.symlink_to(self.project / 'plugin/config.h')
        self.assertFalse(self.inspect()['ok'])
        link.unlink()
        link.symlink_to('missing.ttf')
        self.assertFalse(self.inspect()['ok'])

    def test_permissions_resources_version_and_actual_minimum_are_validated(self):
        executable = self.output / 'TetoricaSPU.vst3/Contents/MacOS/TetoricaSPU'
        executable.chmod(0o644)
        self.assertFalse(self.inspect()['ok'])
        executable.chmod(0o755)
        notice = self.output / 'TetoricaSPU.app/Contents/Resources/THIRD_PARTY_NOTICES.txt'
        notice.write_bytes(b'stale notices')
        self.assertFalse(self.inspect()['ok'])
        notice.write_bytes(b'test-notices')
        info_path = self.output / 'TetoricaSPU.app/Contents/Info.plist'
        info = plistlib.loads(info_path.read_bytes())
        info['CFBundleVersion'] = '0.0.0'
        info_path.write_bytes(plistlib.dumps(info))
        self.assertFalse(self.inspect()['ok'])
        info['CFBundleVersion'] = '0.1.0'
        info_path.write_bytes(plistlib.dumps(info))
        self.newer_minos = True
        self.assertTrue(any('deployment target exceeds' in error for error in self.inspect()['errors']))


if __name__ == '__main__':
    unittest.main()
