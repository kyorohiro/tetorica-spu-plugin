"""Candidate archive integrity, permissions and safe extraction regression tests."""
import importlib.util
import json
from pathlib import Path
import stat
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('preview_package',
    Path(__file__).resolve().parents[1] / 'scripts/package_preview.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class PreviewTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.slug = 'tetorica-spu-plugin-0.1.0-preview-0123456789ab-windows-x64-vst3'
        self.files = {'INSTALL.txt': b'usage', 'LICENSE.txt': b'license',
            'THIRD_PARTY_NOTICES.txt': b'notice', 'WINDOWS_THIRD_PARTY_NOTICES.txt': b'windows',
            'TetoricaSPU.vst3/Contents/x86_64-win/TetoricaSPU.vst3': b'candidate'}

    def tearDown(self):
        self.temp.cleanup()

    def make(self, mutate=None, extra=None, corrupt=None):
        metadata = {'schema': 1, 'product': package.PRODUCT, 'version': '0.1.0',
            'source_commit': '0123456789ab' + '0' * 28, 'platform': 'windows',
            'bundles': ['TetoricaSPU.vst3'],
            'files': {name: {'kind': 'file', 'mode': 0o755 if name.endswith('.vst3') else 0o644,
                'bytes': len(data), 'sha256': package.digest(data)} for name, data in self.files.items()}}
        if mutate:
            mutate(metadata)
        archive = self.root / 'candidate.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            for name, data in self.files.items():
                info = zipfile.ZipInfo(self.slug + '/' + name)
                info.create_system = 3
                info.external_attr = (stat.S_IFREG | metadata['files'][name]['mode']) << 16
                z.writestr(info, data if corrupt != name else data + b'x')
            z.writestr(self.slug + '/manifest.json', json.dumps(metadata))
            if extra:
                z.writestr(extra, b'unrecorded')
        return archive

    def test_roundtrip_retains_bytes_and_executable_permissions(self):
        archive = self.make()
        result = package.verify(archive, self.root / 'extract')
        extracted = Path(result['staging_directory'])
        self.assertEqual(result['platform'], 'windows')
        for name, data in self.files.items():
            self.assertEqual((extracted / name).read_bytes(), data)
        if package.sys.platform != 'win32':
            self.assertEqual(stat.S_IMODE((extracted / next(n for n in self.files if n.endswith('.vst3'))).stat().st_mode), 0o755)
        with self.assertRaisesRegex(ValueError, 'existing'):
            package.verify(archive, self.root / 'extract')

    def test_corrupt_payload_is_rejected_before_extraction(self):
        archive = self.make(corrupt='INSTALL.txt')
        with self.assertRaisesRegex(ValueError, 'mismatch'):
            package.verify(archive, self.root / 'extract')
        self.assertFalse((self.root / 'extract').exists())

    def test_extra_files_and_path_traversal_are_rejected(self):
        for extra in [self.slug + '/unexpected', self.slug + '/../escape', '/absolute', self.slug + '/C:/outside', self.slug + '/C:outside', self.slug + '/file:stream']:
            with self.subTest(extra=extra):
                archive = self.make(extra=extra)
                with self.assertRaises(ValueError):
                    package.verify(archive)

    def test_platform_and_commit_identity_are_checked(self):
        for mutate in [lambda m: m.update(source_commit='f'*40),
                       lambda m: m.update(bundles=['TetoricaSPU.exe']),
                       lambda m: m.update(platform='linux')]:
            with self.subTest(mutate=mutate):
                with self.assertRaises((ValueError, KeyError)):
                    package.verify(self.make(mutate=mutate))

    def test_windows_reserved_and_case_aliases_are_rejected(self):
        for name in ['NUL.txt', 'trailing.', 'trailing ', 'install.txt']:
            with self.subTest(name=name):
                self.files[name] = b'alias'
                with self.assertRaisesRegex(ValueError, 'Windows path'):
                    package.verify(self.make())
                del self.files[name]

    def test_external_zip_checksum_is_checked(self):
        archive = self.make()
        archive.with_suffix('.zip.sha256').write_text('0'*64 + '  candidate.zip\n')
        with self.assertRaisesRegex(ValueError, 'checksum'):
            package.verify(archive)

    def test_inventory_rejects_external_symlink(self):
        if package.sys.platform == 'win32':
            self.skipTest('Windows symlink privileges are not required for VST3 packages')
        stage = self.root / 'stage'
        stage.mkdir()
        (stage / 'escape').symlink_to(self.root / 'outside')
        with self.assertRaisesRegex(ValueError, 'Symlink leaves'):
            package.inventory(stage)


if __name__ == '__main__':
    unittest.main()
