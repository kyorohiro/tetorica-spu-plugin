"""Signed distribution policy tests; Apple commands and signatures are mocked."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock


SCRIPTS = Path(__file__).parents[1] / 'scripts'
SPEC = importlib.util.spec_from_file_location('package_macos_signed', SCRIPTS / 'package_macos_signed.py')
package = importlib.util.module_from_spec(SPEC)
with mock.patch.object(sys, 'path', [str(SCRIPTS), *sys.path]):
    SPEC.loader.exec_module(package)

TEAM = 'ABCDE12345'
SOURCE = '0123456789abcdef0123456789abcdef01234567'
VERSION = '0.0.0'
PRODUCT = 'tetorica-spu-plugin'
BUNDLES = ['TetoricaSPU.app', 'TetoricaSPU.component', 'TetoricaSPU.vst3']
SLUG = f'{PRODUCT}-{VERSION}-preview-{SOURCE[:12]}-macos-universal-signed'
FINGERPRINT = '1234567890ABCDEF1234567890ABCDEF12345678'


class FakeApple:
    """Represent a DMG by a copied payload, without signing or mounting it."""
    def __init__(self, root, payload):
        self.root, self.payload = root, payload
        self.calls = []
        self.mounted = set()
        self.fail = None
        self.notary_status = 'Accepted'
        self.notary_log_status = None
        self.notary_log_id = '11111111-2222-3333-4444-555555555555'
        self.root_metadata = False
        self.payload_kind = 'directory'
        self.leak = 'fake-private-command-output-NEVER-LOG'

    def __call__(self, arguments, *, timeout=120):
        arguments = [str(value) for value in arguments]
        self.calls.append(arguments)
        tool = Path(arguments[0]).name
        if self.fail and self.fail in arguments:
            raise ValueError(self.leak)
        if tool == 'git':
            return '' if 'status' in arguments else SOURCE + '\n'
        if tool == 'codesign':
            if '--sign' in arguments:
                path = Path(arguments[-1])
                if path.is_file():
                    path.write_bytes(path.read_bytes() + b'fake-certificate-signature')
                return ''
            if '-d' in arguments or '--display' in arguments:
                return (f'Identifier=com.Tetorica.TetoricaSPU.distribution.{SOURCE[:12]}\n'
                        f'Authority=Developer ID Application: Example ({TEAM})\n'
                        f'TeamIdentifier={TEAM}\nTimestamp=Nov 2, 2026\n')
            return ''
        if tool == 'hdiutil':
            if 'verify' in arguments:
                return ''
            if 'create' in arguments:
                source = Path(arguments[arguments.index('-srcfolder') + 1])
                assert [path.name for path in source.iterdir()] == [SLUG]
                assert not Path(arguments[-1]).is_relative_to(source)
                if self.payload.exists():
                    shutil.rmtree(self.payload)
                shutil.copytree(source / SLUG, self.payload)
                Path(arguments[-1]).write_bytes(b'fake-read-only-image')
                return ''
            if 'attach' in arguments:
                mount = Path(arguments[arguments.index('-mountpoint') + 1])
                mount.mkdir(parents=True, exist_ok=True)
                if self.payload_kind == 'directory':
                    shutil.copytree(self.payload, mount / SLUG, symlinks=True)
                elif self.payload_kind == 'symlink':
                    (mount / SLUG).symlink_to(self.payload, target_is_directory=True)
                if self.root_metadata:
                    (mount / '.fseventsd').mkdir()
                    (mount / '.fseventsd/journal').write_bytes(b'filesystem-generated metadata')
                    (mount / '.Trashes').mkdir()
                self.mounted.add(mount)
                return plistlib.dumps({'system-entities': [
                    {'mount-point': str(mount), 'dev-entry': '/dev/fake-test-only'}]}).decode()
            if 'detach' in arguments:
                for mount in list(self.mounted):
                    for path in mount.iterdir():
                        if path.is_dir() and not path.is_symlink():
                            shutil.rmtree(path)
                        else:
                            path.unlink()
                    self.mounted.remove(mount)
                return ''
        if tool == 'ditto':
            source, target = map(Path, arguments[-2:])
            if source.is_dir():
                shutil.copytree(source, target, symlinks=True)
            else:
                shutil.copy2(source, target)
            return ''
        if tool == 'xcrun':
            if 'notarytool' in arguments:
                if 'log' in arguments:
                    Path(arguments[-1]).write_text(json.dumps({
                        'jobId': self.notary_log_id,
                        'status': self.notary_log_status or self.notary_status, 'issues': []}))
                    return ''
                return json.dumps({'id': '11111111-2222-3333-4444-555555555555',
                                   'status': self.notary_status})
            if 'staple' in arguments:
                path = Path(arguments[-1])
                path.write_bytes(path.read_bytes() + b'fake-stapled-ticket')
            return ''
        if tool == 'spctl':
            return ''
        raise AssertionError(f'Unexpected mocked Apple command: {arguments}')


class SignedMacPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(prefix='signed-mac-package-test-')
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name).resolve()
        self.project = self.root / 'project'
        (self.project / 'plugin').mkdir(parents=True)
        (self.project / 'plugin/config.h').write_text(
            f'#define PLUG_VERSION_STR "{VERSION}"\n#define PLUG_VERSION_HEX 0x00000000\n')
        (self.project / 'CMakeLists.txt').write_text(
            f'project(tetorica_spu_plugin VERSION {VERSION} LANGUAGES C CXX)\n')
        self.dependencies = {'schema': 1, 'repositories': {'vendor/example': 'a' * 40}}
        (self.project / 'dependencies.lock.json').write_text(json.dumps(self.dependencies))
        (self.project / 'LICENSE').write_text('Example license\n')
        (self.project / 'plugin/resources').mkdir()
        (self.project / 'plugin/resources/THIRD_PARTY_NOTICES.txt').write_text('Example notices\n')
        self.payload = self.root / 'payload'
        self.payload.mkdir()
        for name in BUNDLES:
            executable = self.payload / name / 'Contents/MacOS/TetoricaSPU'
            executable.parent.mkdir(parents=True)
            executable.write_bytes(bytes.fromhex('cafebabe') + b'fake universal binary')
            executable.chmod(0o755)
        for name in ('INSTALL.txt', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt'):
            (self.payload / name).write_text(name + '\n')
        self.manifest = {'schema': 1, 'product': PRODUCT, 'version': VERSION,
            'channel': 'signed-preview', 'source_commit': SOURCE, 'platform': 'macos',
            'label': 'macos-universal', 'bundles': BUNDLES, 'team_id': TEAM,
            'signing': {'kind': 'developer-id-application', 'team_id': TEAM,
                'hardened_runtime': True, 'secure_timestamp': True,
                'notarization': 'container-submission-pending'},
            'dependencies': self.dependencies, 'files': self.inventory(self.payload)}
        self.write_manifest()
        self.image = self.root / (SLUG + '.dmg')
        self.image.write_bytes(b'fake signed and stapled distribution image')
        self.write_checksum()
        self.apple = FakeApple(self.root, self.payload)
        self.inspections = []
        self.inspection_error = False
        self.addCleanup(mock.patch.stopall)
        mock.patch.object(package.sys, 'platform', 'darwin').start()
        mock.patch.object(package, 'inspect_bundles', side_effect=self.inspect).start()

    def inventory(self, directory):
        files = {}
        for path in sorted(directory.rglob('*')):
            if path.is_file() and path.name != 'manifest.json':
                data = path.read_bytes()
                files[path.relative_to(directory).as_posix()] = {
                    'kind': 'file', 'mode': path.stat().st_mode & 0o7777,
                    'bytes': len(data), 'sha256': package.digest(data)}
        return files

    def inspect(self, directory, *args, expected_team_id=None, **kwargs):
        self.assertEqual(expected_team_id, TEAM)
        self.inspections.append(Path(directory))
        if self.inspection_error:
            raise ValueError('Injected signed-bundle policy failure')
        return {'ok': True, 'bundles': [], 'notarization': 'not-checked'}

    def write_manifest(self):
        (self.payload / 'manifest.json').write_text(json.dumps(self.manifest))

    def write_checksum(self):
        self.image.with_suffix('.dmg.sha256').write_text(
            package.digest(self.image.read_bytes()) + '  ' + self.image.name + '\n')

    def validate(self, metadata=None, **kwargs):
        return package.validate_manifest(self.manifest if metadata is None else metadata,
            expected_source=SOURCE, expected_team_id=TEAM, project_root=self.project, **kwargs)

    def verify(self, **kwargs):
        return package.verify(self.image, expected_source=SOURCE, expected_team_id=TEAM,
            extract_to=self.root / 'extracted', run=self.apple, project_root=self.project, **kwargs)

    def build(self):
        input_dir = self.root / 'input'
        if not input_dir.exists():
            shutil.copytree(self.payload, input_dir)
        keychain = self.root / 'fake-ci.keychain-db'
        keychain.write_bytes(b'fake keychain bytes, never use a real identity')
        credentials = self.root / 'credentials.json'
        credentials.write_text(json.dumps({'status': 'prepared', 'team_id': TEAM,
            'identity_sha1': FINGERPRINT, 'keychain_path': str(keychain),
            'notary_profile': 'tetorica-notary-abcdefgh'}))
        args = SimpleNamespace(input=input_dir, output=self.root / 'release',
            credentials=credentials, expected_team_id=TEAM, report=self.root / 'build-report.json')
        return package.build(args, run=self.apple, environ={'GITHUB_ACTIONS': 'true',
            'GITHUB_REF': 'refs/heads/main', 'GITHUB_SHA': SOURCE}, project_root=self.project)

    def test_manifest_requires_exact_source_version_channel_team_and_dependency_pins(self):
        self.validate()
        for key, value in (
                ('schema', 2), ('product', 'another-product'), ('version', '0.0.1'),
                ('channel', 'preview'), ('source_commit', SOURCE[:12]),
                ('source_commit', SOURCE[:12] + 'f' * 28), ('platform', 'windows'),
                ('label', 'macos-arm64'), ('bundles', BUNDLES[:1]),
                ('team_id', 'ZYXWV98765'), ('dependencies', {'schema': 1, 'repositories': {}})):
            with self.subTest(key=key, value=value):
                metadata = copy.deepcopy(self.manifest)
                metadata[key] = value
                with self.assertRaises(ValueError):
                    self.validate(metadata)

    def test_manifest_signing_claims_are_exact_and_cannot_replace_inspection(self):
        for key, value in (('kind', 'ad-hoc'), ('team_id', 'ZYXWV98765'),
                           ('hardened_runtime', False), ('secure_timestamp', False),
                           ('notarization', 'Accepted')):
            with self.subTest(key=key):
                metadata = copy.deepcopy(self.manifest)
                metadata['signing'][key] = value
                with self.assertRaises(ValueError):
                    self.validate(metadata)

    def test_manifest_rejects_unsafe_paths_types_permissions_and_missing_required_files(self):
        malformed = []
        for name in ('../outside', '/outside', 'TetoricaSPU.app/../outside', 'INSTALL.txt:stream', '.', 12):
            metadata = copy.deepcopy(self.manifest)
            metadata['files'][name] = copy.deepcopy(metadata['files']['INSTALL.txt'])
            malformed.append(metadata)
        for key, value in (('mode', 0o4755), ('bytes', True), ('bytes', package.MAX_BYTES + 1),
                           ('sha256', 'f' * 63), ('kind', 'device')):
            metadata = copy.deepcopy(self.manifest)
            metadata['files']['INSTALL.txt'][key] = value
            malformed.append(metadata)
        metadata = copy.deepcopy(self.manifest)
        metadata['files']['install.txt'] = copy.deepcopy(metadata['files']['INSTALL.txt'])
        malformed.append(metadata)
        metadata = copy.deepcopy(self.manifest)
        del metadata['files'][BUNDLES[0] + '/Contents/MacOS/TetoricaSPU']
        malformed.append(metadata)
        metadata = copy.deepcopy(self.manifest)
        metadata['signing']['hardened_runtime'] = 1
        malformed.append(metadata)
        for metadata in malformed:
            with self.subTest(metadata=metadata):
                with self.assertRaises(ValueError):
                    self.validate(metadata)

    def test_json_inputs_reject_duplicate_fields_and_symlinks(self):
        duplicate = self.root / 'duplicate.json'
        duplicate.write_text('{"schema": 1, "schema": 2}')
        with self.assertRaises(ValueError):
            package.read_json(duplicate)
        link = self.root / 'linked.json'
        link.symlink_to(duplicate)
        with self.assertRaises(ValueError):
            package.read_json(link)

    def test_payload_bytes_modes_and_unrecorded_files_are_rejected(self):
        package.verify_payload(self.payload, self.manifest)
        executable = self.payload / BUNDLES[0] / 'Contents/MacOS/TetoricaSPU'
        executable.chmod(0o644)
        with self.assertRaises(ValueError):
            package.verify_payload(self.payload, self.manifest)
        executable.chmod(0o755)
        executable.write_bytes(executable.read_bytes() + b'tamper')
        with self.assertRaises(ValueError):
            package.verify_payload(self.payload, self.manifest)
        executable.write_bytes(bytes.fromhex('cafebabe') + b'fake universal binary')
        (self.payload / 'unrecorded-private-file').write_text('unrecorded')
        with self.assertRaises(ValueError):
            package.verify_payload(self.payload, self.manifest)

    def test_checksum_is_required_and_checked_before_mounting(self):
        checksum = self.image.with_suffix('.dmg.sha256')
        for content in (None, '0' * 64 + '  ' + self.image.name + '\n'):
            with self.subTest(checksum=content):
                if content is None:
                    checksum.unlink(missing_ok=True)
                else:
                    checksum.write_text(content)
                with self.assertRaises((ValueError, OSError)):
                    self.verify()
                self.assertFalse(any('attach' in command for command in self.apple.calls))

    def test_verify_mounts_readonly_inspects_a_fresh_copy_and_detaches(self):
        report_path = self.root / 'verification-report.json'
        report = self.verify(report_path=report_path)
        self.assertTrue(report['ok'])
        self.assertEqual(report['source_commit'], SOURCE)
        self.assertEqual(report['sha256'], package.digest(self.image.read_bytes()))
        stage = Path(report['staging_directory'])
        self.assertEqual(stage, self.root / 'extracted')
        self.assertEqual(self.inventory(stage), self.manifest['files'])
        self.assertEqual(json.loads(report_path.read_text()), report)
        attach = next(command for command in self.apple.calls if 'attach' in command)
        for option in ('-readonly', '-nobrowse', '-noautoopen'):
            self.assertIn(option, attach)
        self.assertEqual(len(self.inspections), 1)
        self.assertNotEqual(self.inspections[0], self.payload)
        self.assertNotEqual(self.inspections[0], Path(attach[attach.index('-mountpoint') + 1]))
        self.assertTrue(any('detach' in command for command in self.apple.calls))
        self.assertEqual(self.apple.mounted, set())
        self.assertFalse(list(self.root.glob('.signed-mount-*')))
        self.assertFalse(any('--sign' in command or 'notarytool' in command for command in self.apple.calls))

    def test_tampered_manifest_or_failed_bundle_policy_cannot_publish_an_extraction(self):
        for failure in ('manifest-source', 'bundle-policy'):
            with self.subTest(failure=failure):
                self.manifest['source_commit'] = 'f' * 40 if failure == 'manifest-source' else SOURCE
                self.write_manifest()
                self.inspection_error = failure == 'bundle-policy'
                with self.assertRaises(ValueError):
                    self.verify()
                self.assertFalse((self.root / 'extracted').exists())
                self.assertEqual(self.apple.mounted, set())
                self.assertFalse(list(self.root.glob('.signed-mount-*')))
                self.assertFalse(list(self.root.glob('.signed-extract-*')))

    def test_detach_failure_retains_mount_instead_of_recursively_deleting_it(self):
        self.apple.fail = 'detach'
        report_path = self.root / 'failed-verification.json'
        with self.assertRaises(ValueError):
            self.verify(report_path=report_path)
        self.assertEqual(len(self.apple.mounted), 1)
        mount = next(iter(self.apple.mounted))
        self.assertTrue((mount / SLUG / 'manifest.json').is_file())
        self.assertTrue((mount / SLUG / BUNDLES[0]).is_dir())
        self.assertFalse((self.root / 'extracted').exists())
        report = json.loads(report_path.read_text())
        self.assertFalse(report['ok'])
        self.assertTrue(any('mount directory retained' in error for error in report['errors']))
        # This is only the fake mount's ordinary test directory.
        self.apple.fail = None
        self.apple(['/usr/bin/hdiutil', 'detach', str(mount)])

    def test_filesystem_metadata_stays_outside_the_strict_versioned_payload(self):
        self.apple.root_metadata = True
        report = self.verify()
        self.assertTrue(report['ok'])
        stage = Path(report['staging_directory'])
        self.assertEqual({path.name for path in stage.iterdir()}, set(BUNDLES) |
                         {'INSTALL.txt', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt', 'manifest.json'})
        self.assertFalse((stage / '.fseventsd').exists())
        self.assertFalse((stage / '.Trashes').exists())

    def test_versioned_payload_must_exist_as_a_real_directory(self):
        for kind in ('missing', 'symlink'):
            with self.subTest(kind=kind):
                self.apple.payload_kind = kind
                with self.assertRaises(ValueError):
                    self.verify()
                self.assertFalse((self.root / 'extracted').exists())
                self.assertEqual(self.apple.mounted, set())
                self.assertFalse(self.inspections)

    def test_existing_extraction_is_never_merged_or_replaced(self):
        destination = self.root / 'extracted'
        destination.mkdir()
        (destination / 'keep').write_text('existing data')
        with self.assertRaises(ValueError):
            self.verify()
        self.assertEqual((destination / 'keep').read_text(), 'existing data')
        self.assertFalse(any('attach' in command for command in self.apple.calls))

    def test_build_signs_staging_copies_and_hashes_only_after_stapling(self):
        original = self.inventory(self.payload)
        report = self.build()
        self.assertTrue(report['ok'])
        self.assertEqual(report['notary']['status'], 'Accepted')
        self.assertEqual(report['source_commit'], SOURCE)
        image = Path(report['image'])
        self.assertTrue(image.read_bytes().endswith(b'fake-stapled-ticket'))
        expected_sha = package.digest(image.read_bytes())
        self.assertEqual(report['sha256'], expected_sha)
        self.assertEqual(image.with_suffix('.dmg.sha256').read_text(),
                         expected_sha + '  ' + image.name + '\n')
        self.assertEqual(self.inventory(self.root / 'input'), original)
        signed = [command for command in self.apple.calls if '--sign' in command]
        self.assertTrue(signed)
        for command in signed:
            self.assertEqual(command[command.index('--sign') + 1], FINGERPRINT)
            self.assertIn('--timestamp', command)
            self.assertFalse(Path(command[-1]).is_relative_to(self.root / 'input'))
            if '--options' in command:
                self.assertLess(command.index('--sign'), command.index('--options'))
                self.assertEqual(command[command.index('--options') + 1], 'runtime')
        self.assertTrue(Path(report['notary']['log']).is_file())

    def test_only_accepted_notarization_can_be_stapled_or_published(self):
        for status in ('Invalid', 'In Progress', 'Unknown'):
            with self.subTest(status=status):
                self.apple.notary_status = status
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(list((self.root / 'release').glob('*.dmg')))
                self.assertFalse(any('staple' in command for command in self.apple.calls))
                # Clear these fake test outputs between attempts; production
                # packaging refuses to overwrite the retained public log.
                shutil.rmtree(self.root / 'release')
                self.apple.calls.clear()

    def test_accepted_wait_requires_matching_accepted_notary_log(self):
        for job_id, status in (('ffffffff-ffff-ffff-ffff-ffffffffffff', 'Accepted'),
                               (None, 'Accepted'),
                               ('11111111-2222-3333-4444-555555555555', 'Invalid')):
            with self.subTest(job_id=job_id, status=status):
                self.apple.notary_log_id, self.apple.notary_log_status = job_id, status
                with self.assertRaises(ValueError):
                    self.build()
                self.assertFalse(list((self.root / 'release').glob('*.dmg')))
                self.assertFalse(any('staple' in command for command in self.apple.calls))
                report = json.loads((self.root / 'build-report.json').read_text())
                self.assertFalse(report['ok'])
                self.assertEqual(report['notary']['submission_id'],
                                 '11111111-2222-3333-4444-555555555555')
                shutil.rmtree(self.root / 'release')
                self.apple.calls.clear()

    def test_successful_candidate_cannot_be_overwritten(self):
        report = self.build()
        image = Path(report['image'])
        original = image.read_bytes()
        self.apple.calls.clear()
        with self.assertRaises(ValueError):
            self.build()
        self.assertEqual(image.read_bytes(), original)
        self.assertFalse(any('--sign' in command or 'notarytool' in command for command in self.apple.calls))

    def test_publication_helpers_refuse_files_and_empty_directories_created_concurrently(self):
        source = self.root / 'new-image'
        source.write_bytes(b'new candidate')
        existing = self.root / 'existing-image'
        existing.write_bytes(b'concurrent candidate')
        with self.assertRaises(FileExistsError):
            package.publish_file(source, existing)
        self.assertEqual(source.read_bytes(), b'new candidate')
        self.assertEqual(existing.read_bytes(), b'concurrent candidate')
        directory = self.root / 'verified-extraction'
        directory.mkdir()
        (directory / 'new-file').write_text('verified data')
        existing_directory = self.root / 'concurrent-extraction'
        existing_directory.mkdir()
        with self.assertRaises(FileExistsError):
            package.publish_directory(directory, existing_directory)
        self.assertEqual(list(existing_directory.iterdir()), [])
        self.assertEqual((directory / 'new-file').read_text(), 'verified data')

    def test_command_failures_and_child_environments_do_not_expose_credentials(self):
        secret_values = {name: 'fake-sensitive-' + name for name in package.SECRET_NAMES}
        failure = subprocess.CompletedProcess(['tool'], 1, self.apple.leak, self.apple.leak)
        with mock.patch.dict(os.environ, secret_values), \
                mock.patch.object(package.subprocess, 'run', return_value=failure) as process:
            with self.assertRaises(ValueError) as error:
                package.run_command(['/usr/bin/false'])
            self.assertNotIn(self.apple.leak, str(error.exception))
            self.assertFalse(set(package.SECRET_NAMES) & set(process.call_args.kwargs['env']))
            self.assertEqual(process.call_args.kwargs['stdin'], subprocess.DEVNULL)
            self.assertEqual(process.call_args.kwargs['timeout'], 120)

    def test_codesign_diagnostics_classify_known_errors_without_retaining_tool_text(self):
        secret = 'fake-private-key-NEVER-PUBLISH'
        identity = 'Developer ID Application: fake-private-subject@example.invalid (ABCDE12345)'
        path = '/private/fake-private-directory-NEVER-PUBLISH/TetoricaSPU.app'
        arguments = ['/usr/bin/codesign', '--sign', identity, path]
        cases = (
            ('The specified item could not be found in the keychain.', 'keychain-item-not-found'),
            ('User interaction is not allowed.', 'key-access-interaction-required'),
            ('unable to build chain to self-signed root', 'certificate-chain-incomplete'),
            ('CSSMERR_TP_CERT_EXPIRED', 'certificate-expired-or-invalid'),
            ('CFBundleVersion must be a valid version', 'invalid-bundle-version'),
            ('bundle format unrecognized, invalid, or unsuitable', 'invalid-bundle-layout'),
            ('resource fork, Finder information, or similar detritus not allowed', 'resource-metadata-forbidden'),
            ('The timestamp service is not available.', 'timestamp-service-unavailable'),
            ('errSecInternalComponent', 'security-internal-component'),
            ('arbitrary unrecognized failure', 'unclassified'))
        for message, category in cases:
            with self.subTest(category=category):
                diagnostic = package.codesign_diagnostic(arguments, 1,
                    stdout=f'-----BEGIN PRIVATE KEY-----{secret}\n{identity}\n',
                    stderr=f'{path}: {message}\nAPPLE_APP_SPECIFIC_PASSWORD={secret}\n\x1b[31m{secret}')
                self.assertEqual(set(diagnostic), {'tool', 'operation', 'target_kind', 'exit_code',
                                                  'categories', 'advice', 'raw_output'})
                self.assertEqual(diagnostic['tool'], 'codesign')
                self.assertEqual(diagnostic['operation'], 'sign')
                self.assertEqual(diagnostic['target_kind'], 'app')
                self.assertEqual(diagnostic['exit_code'], 1)
                self.assertEqual(diagnostic['categories'], [category])
                self.assertEqual(diagnostic['raw_output'], 'withheld')
                self.assertTrue(diagnostic['advice'])
                public = json.dumps(diagnostic) + str(package.CommandFailure(diagnostic))
                for value in (secret, identity, path, message, 'BEGIN PRIVATE KEY', 'APPLE_APP_SPECIFIC_PASSWORD'):
                    self.assertNotIn(value, public)

    def test_codesign_diagnostics_are_bounded_and_report_only_fixed_operation_and_target_kinds(self):
        message = 'errSecInternalComponent'
        output = 'x' * 17000 + message + 'y' * 17000
        diagnostic = package.codesign_diagnostic(['/usr/bin/codesign', '--sign', 'secret-identity',
                                                 '/secret/path/TetoricaSPU'], 2, stderr=output)
        self.assertEqual(diagnostic['categories'], ['unclassified'])
        mixed = package.codesign_diagnostic(['/usr/bin/codesign', '--verify', '/secret/image.dmg'],
            3, stdout='unable to build chain to self-signed root', stderr=message.upper())
        self.assertEqual(mixed['categories'], ['certificate-chain-incomplete', 'security-internal-component'])
        for option, target, operation, kind in (
                ('--verify', '/secret/a.component', 'verify', 'au'),
                ('-d', '/secret/a.vst3', 'display', 'vst3'),
                ('--display', '/secret/a.dmg', 'display', 'disk-image'),
                ('--sign', '/secret/TetoricaSPU', 'sign', 'mach-o'),
                ('--unknown-secret-option', '/secret/fake-private-path', 'other', 'other')):
            with self.subTest(option=option, target=target):
                diagnostic = package.codesign_diagnostic(['/usr/bin/codesign', option, target], 1)
                self.assertEqual(diagnostic['operation'], operation)
                self.assertEqual(diagnostic['target_kind'], kind)
                self.assertNotIn('secret', json.dumps(diagnostic))

    def test_codesign_failure_becomes_a_structured_public_report_without_argv_or_output(self):
        secret = 'fake-credential-NEVER-PUBLISH'
        identity = 'fake-secret-certificate-subject@example.invalid'
        target = '/private/fake-secret-target/TetoricaSPU.component'
        arguments = ['/usr/bin/codesign', '--sign', identity, target]
        failure = subprocess.CompletedProcess(arguments, 1, secret,
                                              f'{target}: errSecInternalComponent; {secret}')
        with mock.patch.object(package.subprocess, 'run', return_value=failure):
            with self.assertRaises(package.CommandFailure) as error:
                package.run_command(arguments)
        report = {'schema': 1, 'ok': True, 'errors': []}
        report_path = self.root / 'public-command-failure.json'
        package.public_failure(report, report_path, error.exception)
        public = report_path.read_text()
        self.assertEqual(json.loads(public), report)
        self.assertFalse(report['ok'])
        self.assertEqual(report['tool_failure'], error.exception.diagnostic)
        self.assertEqual(report['tool_failure']['categories'], ['security-internal-component'])
        self.assertEqual(len(report['errors']), 1)
        for value in (secret, identity, target, 'fake-secret-target'):
            self.assertNotIn(value, public)

    def test_codesign_timeout_and_oserror_keep_arbitrary_details_out_of_reports(self):
        secret = 'fake-exception-detail-NEVER-PUBLISH'
        arguments = ['/usr/bin/codesign', '--sign', secret, '/private/' + secret + '/TetoricaSPU']
        for failure in (OSError(secret), subprocess.TimeoutExpired(arguments, 120, output=secret, stderr=secret)):
            with self.subTest(failure=type(failure).__name__), \
                    mock.patch.object(package.subprocess, 'run', side_effect=failure):
                with self.assertRaises(ValueError) as error:
                    package.run_command(arguments)
                self.assertNotIsInstance(error.exception, package.CommandFailure)
                report = {'ok': True, 'errors': []}
                package.public_failure(report, None, error.exception)
                self.assertNotIn(secret, json.dumps(report))
                self.assertNotIn('tool_failure', report)

    def test_inspector_cannot_swallow_a_structured_codesign_failure(self):
        diagnostic = package.codesign_diagnostic(['/usr/bin/codesign', '--verify', '/private/secret/TetoricaSPU'],
                                                 3, stderr='errSecInternalComponent')
        failure = package.CommandFailure(diagnostic)

        def inspector(directory, *, run, **kwargs):
            try:
                run(['/usr/bin/codesign', '--verify', str(directory)])
            except ValueError:
                return {'ok': False, 'errors': ['inspection captured a signature failure']}
            self.fail('Injected signature error was not raised')

        with mock.patch.object(package, 'inspect_bundles', side_effect=inspector):
            with self.assertRaises(package.CommandFailure) as error:
                package.inspect_signed(self.payload, TEAM, self.project, mock.Mock(side_effect=failure))
        self.assertIs(error.exception, failure)


if __name__ == '__main__':
    unittest.main()
