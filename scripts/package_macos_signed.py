#!/usr/bin/env python3
"""Build a CI-only signed/notarized DMG, or verify and extract its exact payload.

Only ``build`` performs signing or contacts Apple's submission service. It is
restricted to a clean macOS GitHub Actions main-branch checkout. ``verify``
mounts an existing image read-only and never installs or launches its contents.
"""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import tempfile

from check_macos_bundles import (MACHO_MAGICS, bundle_files, developer_id_requirement,
                                 inspect_bundles, project_version, signature_fields)
from package_preview import CONTENT, LABEL, MAX_BYTES, PRODUCT, digest, inventory, safe_name


ROOT = Path(__file__).resolve().parents[1]
DOCS = ('LICENSE.txt', 'THIRD_PARTY_NOTICES.txt', 'INSTALL.txt')
PAYLOAD_ROOTS = (*CONTENT['macos'], *DOCS, 'manifest.json')
MAX_ENTRIES = 2000
MAX_MANIFEST_BYTES = 1024 * 1024
SECRET_NAMES = ('BUILD_CERTIFICATE_BASE64', 'P12_PASSWORD', 'APPLE_ID',
                'APPLE_TEAM_ID', 'APPLE_APP_SPECIFIC_PASSWORD')


def run_command(arguments, *, timeout=120):
    """Keep raw failing tool output and inherited credential values out of logs."""
    environment = {key: value for key, value in os.environ.items() if key not in SECRET_NAMES}
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=timeout,
                                stdin=subprocess.DEVNULL, env=environment, check=False)
    except (OSError, subprocess.TimeoutExpired):
        raise ValueError(f'{Path(arguments[0]).name} could not complete; raw output withheld') from None
    if result.returncode:
        raise ValueError(f'{Path(arguments[0]).name} failed; raw output withheld')
    return result.stdout + result.stderr


def write_report(path, report):
    if path is None:
        return
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.is_symlink():
        raise ValueError('Report destination must not be a symlink')
    temporary = path.with_name(path.name + '.tmp')
    with temporary.open('x', encoding='utf-8') as target:
        json.dump(report, target, indent=2, sort_keys=True)
        target.write('\n')
    temporary.replace(path)


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError('Duplicate JSON object key')
        result[key] = value
    return result


def read_json(path, maximum=MAX_MANIFEST_BYTES):
    path = Path(path)
    info = path.lstat()
    if not stat.S_ISREG(info.st_mode) or info.st_size > maximum:
        raise ValueError('JSON input must be a bounded regular file')
    try:
        value = json.loads(path.read_text(encoding='utf-8'), object_pairs_hook=unique_object)
    except (UnicodeError, json.JSONDecodeError):
        raise ValueError('Invalid JSON input') from None
    if not isinstance(value, dict):
        raise ValueError('JSON input must be an object')
    return value


def source_commit(value):
    if not isinstance(value, str) or not re.fullmatch(r'[0-9a-f]{40}', value):
        raise ValueError('Expected source must be a full lowercase 40-character commit')
    return value


def slug(version, commit):
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Unsafe version')
    return f'{PRODUCT}-{version}-preview-{source_commit(commit)[:12]}-{LABEL["macos"]}-signed'


def dmg_identifier(commit):
    return 'com.Tetorica.TetoricaSPU.distribution.' + source_commit(commit)[:12]


def signing_metadata(team_id):
    developer_id_requirement(team_id)
    # The immutable inner manifest describes signing. The outer container's
    # subsequent notarization is proved by its ticket and public build report.
    return {'kind': 'developer-id-application', 'team_id': team_id,
            'hardened_runtime': True, 'secure_timestamp': True,
            'notarization': 'container-submission-pending'}


def validate_manifest(metadata, *, expected_source, expected_team_id, project_root=ROOT):
    source_commit(expected_source)
    developer_id_requirement(expected_team_id)
    if not isinstance(metadata, dict):
        raise ValueError('Manifest must be an object')
    version, _ = project_version(Path(project_root))
    expected = {'schema': 1, 'product': PRODUCT, 'version': version,
                'channel': 'signed-preview', 'source_commit': expected_source,
                'platform': 'macos', 'label': LABEL['macos'],
                'bundles': list(CONTENT['macos']), 'team_id': expected_team_id,
                'signing': signing_metadata(expected_team_id),
                'dependencies': read_json(Path(project_root) / 'dependencies.lock.json')}
    if set(metadata) != set(expected) | {'files'}:
        raise ValueError('Unknown or missing signed manifest fields')
    for key, value in expected.items():
        if (metadata.get(key) != value or type(metadata.get(key)) is not type(value) or
                json.dumps(metadata[key], sort_keys=True) != json.dumps(value, sort_keys=True)):
            raise ValueError('Signed manifest identity mismatch: ' + key)
    files = metadata['files']
    if not isinstance(files, dict) or not files or len(files) > MAX_ENTRIES:
        raise ValueError('Manifest file count exceeds payload bounds')
    total, folded = 0, set()
    for name, spec in files.items():
        if not isinstance(name, str):
            raise ValueError('Manifest payload paths must be strings')
        parts = safe_name(name).parts
        if not parts or parts[0] not in (*CONTENT['macos'], *DOCS) or name == 'manifest.json':
            raise ValueError('Unrecognized manifest payload root')
        if name.casefold() in folded:
            raise ValueError('Case-insensitive payload path collision')
        folded.add(name.casefold())
        if (not isinstance(spec, dict) or set(spec) != {'kind', 'mode', 'bytes', 'sha256'} or
                spec.get('kind') not in ('file', 'symlink') or
                type(spec.get('bytes')) is not int or spec['bytes'] < 0 or
                type(spec.get('mode')) is not int or not 0 <= spec['mode'] <= 0o777 or
                not isinstance(spec.get('sha256'), str) or
                not re.fullmatch(r'[0-9a-f]{64}', spec['sha256'])):
            raise ValueError('Invalid manifest file record')
        if parts[0] in DOCS and (len(parts) != 1 or spec['kind'] != 'file'):
            raise ValueError('Documentation must be regular top-level files')
        total += spec['bytes']
    if total > MAX_BYTES:
        raise ValueError('Manifest exceeds payload byte bounds')
    required = set(DOCS) | {name + '/Contents/MacOS/TetoricaSPU' for name in CONTENT['macos']}
    if not required.issubset(files) or any(files[name]['kind'] != 'file' for name in required):
        raise ValueError('Missing required regular binaries or documentation')
    return metadata


def verify_payload(directory, metadata):
    directory = Path(directory)
    if directory.is_symlink() or not directory.is_dir():
        raise ValueError('Payload root must be a real directory')
    if {path.name for path in directory.iterdir()} != set(PAYLOAD_ROOTS):
        raise ValueError('Missing or unexpected payload roots')
    for name in CONTENT['macos']:
        bundle = directory / name
        if bundle.is_symlink() or not bundle.is_dir():
            raise ValueError('Bundle roots must be real directories')
    for name in (*DOCS, 'manifest.json'):
        if not stat.S_ISREG((directory / name).lstat().st_mode):
            raise ValueError('Documentation and manifest must be regular files')
    count, total = 0, 0
    for current, directories, names in os.walk(directory, followlinks=False):
        for name in directories + names:
            path = Path(current) / name
            relative = path.relative_to(directory).as_posix()
            safe_name(relative)
            info = path.lstat()
            count += 1
            if count > MAX_ENTRIES:
                raise ValueError('Actual payload exceeds entry bounds')
            if stat.S_ISREG(info.st_mode):
                total += info.st_size
            elif stat.S_ISLNK(info.st_mode):
                target = os.readlink(path)
                if (':' in target or '\\' in target or any(ord(char) < 32 for char in target)):
                    raise ValueError('Unsafe payload symlink target')
                total += len(target.encode('utf-8'))
            elif not stat.S_ISDIR(info.st_mode):
                raise ValueError('Special payload entry')
            if total > MAX_BYTES + MAX_MANIFEST_BYTES:
                raise ValueError('Actual payload exceeds byte bounds')
    for name in CONTENT['macos']:
        bundle_files((directory / name).resolve())
    actual = inventory(directory)
    actual.pop('manifest.json', None)
    if actual != metadata['files']:
        raise ValueError('Payload inventory does not match signed manifest')
    return actual


def verify_dmg(image, team_id, commit, run):
    requirement = developer_id_requirement(team_id)
    run(['/usr/bin/codesign', '--verify', '--strict', '--test-requirement', requirement, str(image)])
    detail = run(['/usr/bin/codesign', '-d', '--verbose=4', str(image)])
    fields, authorities = signature_fields(detail)
    if (fields.get('Signature') == 'adhoc' or not authorities or
            not authorities[0].startswith('Developer ID Application: ') or
            not authorities[0].endswith(f' ({team_id})') or
            fields.get('TeamIdentifier') != team_id or
            fields.get('Identifier') != dmg_identifier(commit) or
            not fields.get('Timestamp', '').strip() or
            fields['Timestamp'].strip().lower() in ('none', 'not set', 'not-set', '(null)', '<none>')):
        raise ValueError('DMG lacks expected Developer ID identity or secure timestamp')
    return {'kind': 'developer-id-application', 'team_id': team_id,
            'identifier': fields['Identifier'], 'timestamp': fields['Timestamp'],
            'authorities': authorities, 'requirement': requirement}


def inspect_signed(directory, team_id, project_root, run):
    inspection = inspect_bundles(directory, project_root=project_root, run=run,
                                 expected_team_id=team_id)
    if not inspection.get('ok'):
        raise ValueError('Developer ID bundle inspection failed')
    return inspection


def public_failure(report, path, error):
    report['ok'] = False
    report.setdefault('errors', []).append(str(error) if isinstance(error, ValueError)
                                         else 'Packaging operation failed; raw details withheld')
    write_report(path, report)


def publish_file(source, destination):
    # Both paths live on the same output filesystem. A hard link is atomic
    # and exclusive, whereas POSIX rename may replace an existing target.
    os.link(source, destination, follow_symlinks=False)
    source.unlink()


def publish_directory(source, destination):
    # Reserve a new destination exclusively, then move only verified roots.
    # No data is ever merged into a directory that existed before this call.
    destination.mkdir(mode=0o700)
    try:
        for path in source.iterdir():
            path.rename(destination / path.name)
        source.rmdir()
    except Exception:
        shutil.rmtree(destination)
        raise


def verify(image, *, expected_source, expected_team_id, extract_to,
           report_path=None, run=run_command, project_root=ROOT):
    report = {'schema': 1, 'ok': False, 'platform': 'macos', 'errors': []}
    mounted, mountpoint, work = False, None, None
    destination = Path(extract_to)
    temporary = None
    try:
        source_commit(expected_source)
        developer_id_requirement(expected_team_id)
        image = Path(image)
        if image.is_symlink() or not image.is_file():
            raise ValueError('DMG must be a regular file')
        if image.stat().st_size > MAX_BYTES * 2:
            raise ValueError('DMG exceeds container byte bounds')
        image = image.resolve()
        version, _ = project_version(Path(project_root))
        if image.name != slug(version, expected_source) + '.dmg':
            raise ValueError('DMG filename/source/version mismatch')
        if destination.exists() or destination.is_symlink():
            raise ValueError('Refusing existing extraction destination')
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination = destination.parent.resolve() / destination.name
        checksum = image.with_suffix('.dmg.sha256')
        if checksum.is_symlink() or not checksum.is_file() or checksum.stat().st_size > 512:
            raise ValueError('Required DMG checksum sidecar is missing or unsafe')
        image_sha = digest(image.read_bytes())
        if checksum.read_text(encoding='ascii') != image_sha + '  ' + image.name + '\n':
            raise ValueError('DMG checksum sidecar mismatch')
        report.update(image=str(image), archive=str(image), sha256=image_sha,
                      source_commit=expected_source, team_id=expected_team_id)
        report['signature'] = verify_dmg(image, expected_team_id, expected_source, run)
        run(['/usr/bin/xcrun', 'stapler', 'validate', str(image)])
        run(['/usr/sbin/spctl', '-a', '-t', 'open', '--context', 'context:primary-signature',
             '-v', str(image)])
        run(['/usr/bin/hdiutil', 'verify', str(image)])
        report['notary_trust'] = {'stapled_ticket_valid': True, 'gatekeeper_accepted': True,
                                 'container': 'signed-udif-dmg'}
        # Never use TemporaryDirectory around a mount: recursive cleanup is
        # unsafe if attach partly succeeds or detach fails.
        work = Path(tempfile.mkdtemp(prefix='.signed-mount-', dir=destination.parent))
        work.chmod(0o700)
        mountpoint = work / 'volume'
        mountpoint.mkdir(mode=0o700)
        mounted = True  # Attach may mount successfully before reporting error.
        run(['/usr/bin/hdiutil', 'attach', '-readonly', '-nobrowse', '-noautoopen',
             '-mountpoint', str(mountpoint), str(image)])
        # Volume-root filesystem metadata is outside the distribution payload.
        # Never traverse or copy it: only inspect our exact versioned folder.
        mounted_payload = mountpoint / slug(version, expected_source)
        if mounted_payload.is_symlink() or not mounted_payload.is_dir():
            raise ValueError('DMG payload folder must be a real versioned directory')
        metadata = read_json(mounted_payload / 'manifest.json')
        validate_manifest(metadata, expected_source=expected_source,
                          expected_team_id=expected_team_id, project_root=project_root)
        verify_payload(mounted_payload, metadata)
        temporary = Path(tempfile.mkdtemp(prefix='.signed-extract-', dir=destination.parent))
        temporary.chmod(0o700)
        for name in PAYLOAD_ROOTS:
            run(['/usr/bin/ditto', str(mounted_payload / name), str(temporary / name)])
        verify_payload(temporary, metadata)
        report['inspection'] = inspect_signed(temporary, expected_team_id, project_root, run)
        run(['/usr/bin/hdiutil', 'detach', str(mountpoint)])
        mounted = False
        mountpoint.rmdir()
        work.rmdir()
        work = mountpoint = None
        if destination.exists() or destination.is_symlink():
            raise ValueError('Refusing existing extraction destination')
        publish_directory(temporary, destination)
        temporary = None
        report.update(ok=True, version=metadata['version'], staging_directory=str(destination))
        write_report(report_path, report)
        return report
    except Exception as error:
        public_failure(report, report_path, error)
        raise
    finally:
        detach_failed = False
        if mounted:
            try:
                run(['/usr/bin/hdiutil', 'detach', str(mountpoint)])
                mounted = False
            except Exception:
                detach_failed = True
                report['ok'] = False
                report['errors'].append('Read-only DMG detach failed; mount directory retained')
                write_report(report_path, report)
        if not mounted and work is not None:
            # rmdir fails safely if an unmounted directory contains anything.
            try:
                mountpoint.rmdir()
                work.rmdir()
            except OSError:
                pass
        if temporary is not None:
            shutil.rmtree(temporary)
        if detach_failed and sys.exc_info()[0] is None:
            raise ValueError('Read-only DMG detach failed; mount directory retained')


def instructions(version, commit):
    return f'''Tetorica SPU Plugin {version} Preview ({commit[:12]})

macOS 13 or newer, Apple Silicon and Intel.
Open the versioned Tetorica SPU Preview folder inside the downloaded disk image.
TetoricaSPU.app: standalone application.
TetoricaSPU.component: AUv2, place in ~/Library/Audio/Plug-Ins/Components/.
TetoricaSPU.vst3: place in ~/Library/Audio/Plug-Ins/VST3/.
Quit audio hosts and retain existing plugin bundles before changing them.
This preview uses Developer ID Application signatures. The distributed disk
image carries Apple's notarization ticket; retain the original downloaded DMG.
DAW state saves use schema 10. Keep earlier project copies before testing;
older plugin builds cannot read new saves. Old sound sources retain their
legacy playback when restored by this build. No game sample banks are included.
Real-device listening and long-session DAW use remain separate checks.
'''


def credentials(path, expected_team_id):
    value = read_json(path, maximum=65536)
    if (value.get('status') != 'prepared' or value.get('team_id') != expected_team_id or
            not re.fullmatch(r'[A-Fa-f0-9]{40}', str(value.get('identity_sha1', ''))) or
            not re.fullmatch(r'tetorica-notary-[a-z0-9_]{8}', str(value.get('notary_profile', '')))):
        raise ValueError('Signing credential preparation report is not ready for expected team')
    keychain = Path(value.get('keychain_path', ''))
    if not keychain.is_absolute() or keychain.is_symlink() or not keychain.is_file():
        raise ValueError('Prepared file-based keychain is missing or unsafe')
    return value


def build(args, *, run=run_command, environ=None, project_root=ROOT):
    environment = os.environ if environ is None else environ
    project_root = Path(project_root).resolve()
    report_path = getattr(args, 'report', None)
    report = {'schema': 1, 'ok': False, 'platform': 'macos', 'channel': 'signed-preview',
              'errors': [], 'notary': {'submission_id': None, 'status': 'not-submitted',
                                       'log': None}}
    try:
        if (sys.platform != 'darwin' or environment.get('GITHUB_ACTIONS') != 'true' or
                environment.get('GITHUB_REF') != 'refs/heads/main'):
            raise ValueError('Signed build requires a macOS GitHub Actions main-branch checkout')
        developer_id_requirement(args.expected_team_id)
        dirty = run(['git', '-C', str(project_root), 'status', '--porcelain', '--untracked-files=all'])
        if dirty.strip():
            raise ValueError('Commit all source/document changes before signed packaging')
        commit = source_commit(run(['git', '-C', str(project_root), 'rev-parse', 'HEAD']).strip())
        if environment.get('GITHUB_SHA') != commit:
            raise ValueError('Clean checkout HEAD does not match the GitHub Actions source SHA')
        version, _ = project_version(project_root)
        name = slug(version, commit)
        prepared = credentials(args.credentials, args.expected_team_id)
        input_dir, output = Path(args.input), Path(args.output)
        if input_dir.is_symlink() or not input_dir.is_dir():
            raise ValueError('Build input must be a real directory')
        input_dir = input_dir.resolve()
        if output.is_symlink():
            raise ValueError('Output directory must not be a symlink')
        output.mkdir(parents=True, exist_ok=True)
        output = output.resolve()
        image, checksum = output / (name + '.dmg'), output / (name + '.dmg.sha256')
        log_path = output / (name + '.notary-log.json')
        if any(path.exists() or path.is_symlink() for path in (image, checksum, log_path)):
            raise ValueError('Refusing to replace an existing signed candidate')
        report.update(source_commit=commit, version=version, team_id=args.expected_team_id,
                      signing_identity_sha1=prepared['identity_sha1'], image=str(image), archive=str(image))
        write_report(report_path, report)
        for bundle_name in CONTENT['macos']:
            source = input_dir / bundle_name
            if source.is_symlink() or not source.is_dir():
                raise ValueError('Missing/non-directory/symlink bundle: ' + bundle_name)
            bundle_files(source.resolve())
        with tempfile.TemporaryDirectory(prefix='.signed-build-', dir=output) as working:
            working = Path(working)
            image_root = working / 'image-root'
            image_root.mkdir()
            stage = image_root / name
            stage.mkdir()
            for bundle_name in CONTENT['macos']:
                shutil.copytree(input_dir / bundle_name, stage / bundle_name, symlinks=True)
            for source, target in ((project_root / 'LICENSE', 'LICENSE.txt'),
                                   (project_root / 'plugin/resources/THIRD_PARTY_NOTICES.txt',
                                    'THIRD_PARTY_NOTICES.txt')):
                if not stat.S_ISREG(source.lstat().st_mode):
                    raise ValueError('Package documentation must be a regular file')
                shutil.copyfile(source, stage / target)
            (stage / 'INSTALL.txt').write_text(instructions(version, commit), encoding='utf-8')
            sign = ['/usr/bin/codesign', '--sign', prepared['identity_sha1'], '--force',
                    '--options', 'runtime', '--timestamp', '--keychain', prepared['keychain_path']]
            for bundle_name in CONTENT['macos']:
                bundle = stage / bundle_name
                files, _ = bundle_files(bundle.resolve())
                for path in files:
                    with path.open('rb') as binary:
                        magic = binary.read(4)
                    if magic in MACHO_MAGICS:
                        run(sign + [str(path)])
                # Nested bundles are signed from the inside out, without --deep.
                nested = [path for path in bundle.rglob('*') if path.is_dir() and
                          not path.is_symlink() and (path / 'Contents/Info.plist').is_file()]
                for path in sorted(nested, key=lambda value: len(value.parts), reverse=True):
                    run(sign + [str(path)])
                run(sign + [str(bundle)])
            report['inspection'] = inspect_signed(stage, args.expected_team_id, project_root, run)
            metadata = {'schema': 1, 'product': PRODUCT, 'version': version,
                        'channel': 'signed-preview', 'source_commit': commit, 'platform': 'macos',
                        'label': LABEL['macos'], 'bundles': list(CONTENT['macos']),
                        'team_id': args.expected_team_id, 'signing': signing_metadata(args.expected_team_id),
                        'dependencies': read_json(project_root / 'dependencies.lock.json'),
                        'files': inventory(stage)}
            validate_manifest(metadata, expected_source=commit, expected_team_id=args.expected_team_id,
                              project_root=project_root)
            (stage / 'manifest.json').write_text(json.dumps(metadata, indent=2, sort_keys=True) + '\n',
                                                encoding='utf-8')
            verify_payload(stage, metadata)
            candidate = working / 'candidate.dmg'
            run(['/usr/bin/hdiutil', 'create', '-format', 'UDZO', '-srcfolder', str(image_root),
                 '-volname', 'Tetorica SPU Preview ' + version, '-o', str(candidate)])
            run(['/usr/bin/codesign', '--sign', prepared['identity_sha1'], '--keychain',
                 prepared['keychain_path'], '--timestamp', '--identifier', dmg_identifier(commit),
                 str(candidate)])
            report['signature'] = verify_dmg(candidate, args.expected_team_id, commit, run)
            run(['/usr/bin/hdiutil', 'verify', str(candidate)])
            authentication = ['--keychain-profile', prepared['notary_profile'],
                              '--keychain', prepared['keychain_path']]
            submitted = json.loads(run(['/usr/bin/xcrun', 'notarytool', 'submit', str(candidate),
                                         *authentication, '--no-wait', '--output-format', 'json'],
                                        timeout=300))
            submission_id = submitted.get('id')
            if not isinstance(submission_id, str) or not re.fullmatch(
                    r'[A-Fa-f0-9]{8}(?:-[A-Fa-f0-9]{4}){3}-[A-Fa-f0-9]{12}', submission_id):
                raise ValueError('Notary service did not return a valid submission identifier')
            report['notary'].update(submission_id=submission_id, status='Submitted')
            write_report(report_path, report)
            wait_error = None
            try:
                waited = json.loads(run(['/usr/bin/xcrun', 'notarytool', 'wait', submission_id,
                                          *authentication, '--timeout', '15m', '--output-format', 'json'],
                                         timeout=930))
                if waited.get('id', submission_id).lower() != submission_id.lower():
                    raise ValueError('Notary wait submission identity mismatch')
                report['notary']['status'] = waited.get('status', 'Unknown')
            except Exception as error:
                wait_error = error
                report['notary']['status'] = 'wait-failed-or-timed-out'
            finally:
                try:
                    # Use a private temporary target so publication cannot replace
                    # a pre-existing log, and only publish parsed JSON.
                    temporary_log = working / 'notary-log.json'
                    run(['/usr/bin/xcrun', 'notarytool', 'log', submission_id, *authentication,
                         str(temporary_log)], timeout=120)
                    notary_log = read_json(temporary_log, maximum=4 * 1024 * 1024)
                    if (not isinstance(notary_log.get('jobId'), str) or
                            notary_log['jobId'].lower() != submission_id.lower()):
                        raise ValueError('Notary log submission identity mismatch')
                    with log_path.open('x', encoding='utf-8') as target:
                        json.dump(notary_log, target, indent=2, sort_keys=True)
                        target.write('\n')
                    report['notary']['log'] = str(log_path)
                    report['notary']['log_status'] = notary_log.get('status', 'Unknown')
                    if wait_error is not None and isinstance(notary_log.get('status'), str):
                        report['notary']['status'] = notary_log['status']
                except Exception:
                    report['notary']['log_error'] = 'Notary log could not be retrieved or validated'
                write_report(report_path, report)
            if wait_error is not None:
                raise ValueError('Notary wait failed or timed out; see public submission report')
            if report['notary']['status'] != 'Accepted':
                raise ValueError('Notary service did not accept this exact disk image')
            if report['notary'].get('log_status') != 'Accepted':
                raise ValueError('Notary log does not confirm an accepted submission')
            if report['notary']['log'] is None:
                raise ValueError('Accepted notarization requires a retained public notary log')
            run(['/usr/bin/xcrun', 'stapler', 'staple', str(candidate)], timeout=180)
            run(['/usr/bin/xcrun', 'stapler', 'validate', str(candidate)])
            run(['/usr/bin/hdiutil', 'verify', str(candidate)])
            report['signature'] = verify_dmg(candidate, args.expected_team_id, commit, run)
            run(['/usr/sbin/spctl', '-a', '-t', 'open', '--context', 'context:primary-signature',
                 '-v', str(candidate)])
            final_sha = digest(candidate.read_bytes())
            temporary_checksum = working / 'candidate.dmg.sha256'
            temporary_checksum.write_text(final_sha + '  ' + image.name + '\n', encoding='ascii')
            if any(path.exists() or path.is_symlink() for path in (image, checksum)):
                raise ValueError('Refusing to replace an existing signed candidate')
            publish_file(candidate, image)
            publish_file(temporary_checksum, checksum)
            report.update(ok=True, sha256=final_sha, checksum=str(checksum),
                          notary_trust={'stapled_ticket_valid': True, 'gatekeeper_accepted': True,
                                        'container': 'signed-udif-dmg'})
        write_report(report_path, report)
        return report
    except Exception as error:
        public_failure(report, report_path, error)
        raise


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    building = commands.add_parser('build')
    building.add_argument('--input', type=Path, required=True)
    building.add_argument('--output', type=Path, required=True)
    building.add_argument('--credentials', type=Path, required=True)
    building.add_argument('--expected-team-id', required=True)
    building.add_argument('--report', type=Path, required=True)
    verifying = commands.add_parser('verify')
    verifying.add_argument('image', type=Path)
    verifying.add_argument('--expected-source', required=True)
    verifying.add_argument('--expected-team-id', required=True)
    verifying.add_argument('--extract-to', type=Path, required=True)
    verifying.add_argument('--report', type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        if args.command == 'build':
            result = build(args)
        else:
            result = verify(args.image, expected_source=args.expected_source,
                            expected_team_id=args.expected_team_id, extract_to=args.extract_to,
                            report_path=args.report)
    except Exception:
        print('Signed package operation failed; see the public JSON report', file=sys.stderr)
        return 1
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
