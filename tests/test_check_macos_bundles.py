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
            return ''
        if tool == 'codesign' and '-d' in arguments:
            return 'Identifier=test\nSignature=adhoc\nTeamIdentifier=not set\nCDHash=test-hash\n'
        self.fail(f'Unexpected tool invocation: {arguments}')

    def inspect(self):
        return checker.inspect_bundles(self.output, self.project, self.fake_run)

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
