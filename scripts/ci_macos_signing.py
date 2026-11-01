#!/usr/bin/env python3
"""Create and remove CI-only, file-based signing/notary credentials.

Usage (on the macOS Actions runner only)::

    python3 scripts/ci_macos_signing.py prepare --report "$RUNNER_TEMP/signing.json"
    python3 scripts/ci_macos_signing.py cleanup --report "$RUNNER_TEMP/signing.json"

Pass the five repository secrets through the environment of the prepare step,
never command-line arguments. The report contains public identity metadata and
temporary paths only. Keep it in RUNNER_TEMP, outside uploaded artifacts. Run
cleanup in an Actions ``always()`` step, including when prepare fails.

Apple documents ``notarytool store-credentials PROFILE --apple-id ID --team-id
TEAM --password PASSWORD`` in Customizing the notarization workflow, and the
explicit file-based ``--keychain`` option in TN3147:
https://developer.apple.com/documentation/security/customizing-the-notarization-workflow
https://developer.apple.com/documentation/technotes/tn3147-migrating-to-the-latest-notarization-tool

The Apple CLIs require some passwords in their argv. This module never prints
commands, subprocess output, environment values, or exception details. It never
exports keys, changes the default keychain, or grants all applications access.
"""
import argparse
import base64
import binascii
import hashlib
import json
import os
from pathlib import Path
import re
import secrets
import shlex
import stat
import subprocess
import sys
import tempfile


SECRET_NAMES = ('BUILD_CERTIFICATE_BASE64', 'P12_PASSWORD', 'APPLE_ID',
                'APPLE_TEAM_ID', 'APPLE_APP_SPECIFIC_PASSWORD')
DIRECTORY_PREFIX = 'tetorica-signing-'
DIRECTORY_PATTERN = re.compile(r'tetorica-signing-[a-z0-9_]{8}')
IDENTITY_PATTERN = re.compile(r'^\s*\d+\) ([A-Fa-f0-9]{40}) "([^"\r\n]+)"\s*$')
KEYCHAIN_NAME = 'signing.keychain-db'
STATE_NAME = 'ownership.json'
P12_NAME = 'certificate.p12'
MAX_P12_BYTES = 1024 * 1024
KEYCHAIN_LOCK_NAMES = {
    '.fl' + hashlib.sha1(name.encode('ascii')).hexdigest()[:8].upper()
    for name in (KEYCHAIN_NAME, KEYCHAIN_NAME.removesuffix('-db'))
}


class SigningError(Exception):
    """Only fixed, deliberately public error messages belong in this exception."""


def context(environ):
    if environ.get('GITHUB_ACTIONS') != 'true' or sys.platform != 'darwin':
        raise SigningError('Credential operations require a macOS GitHub Actions runner')
    raw = environ.get('RUNNER_TEMP', '')
    if not raw or not Path(raw).is_absolute():
        raise SigningError('RUNNER_TEMP must be an existing absolute runner directory')
    root = Path(raw).resolve(strict=True)
    if not root.is_dir() or root.stat().st_uid != os.getuid():
        raise SigningError('RUNNER_TEMP must be owned by the runner user')
    run = [environ.get(name, '') for name in ('GITHUB_RUN_ID', 'GITHUB_RUN_ATTEMPT', 'GITHUB_JOB')]
    if not all(run):
        raise SigningError('GitHub Actions run identity is missing')
    return root, run


def report_location(path, root):
    path = Path(path)
    if (not path.is_absolute() or path.parent.resolve(strict=True) != root or
            not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9_.-]{0,127}\.json', path.name)):
        raise SigningError('Report must be a named JSON file directly inside RUNNER_TEMP')
    return root / path.name


def private_stat(path, *, directory=False):
    info = path.lstat()
    kind = stat.S_ISDIR if directory else stat.S_ISREG
    if (not kind(info.st_mode) or info.st_uid != os.getuid() or
            stat.S_IMODE(info.st_mode) != (0o700 if directory else 0o600) or
            (not directory and info.st_nlink != 1)):
        raise SigningError('Refusing an unowned, linked, or non-private credential path')
    return info


def write_json(path, value, *, exclusive=False):
    # Once credential creation starts, cancellation must never leave a
    # truncated report/journal that the always-step cleanup cannot read.
    temporary = path if exclusive else path.with_name(path.name + '.' + secrets.token_hex(16) + '.tmp')
    if not exclusive:
        private_stat(path)
    descriptor = os.open(temporary, os.O_WRONLY | os.O_NOFOLLOW | os.O_CREAT | os.O_EXCL, 0o600)
    try:
        with os.fdopen(descriptor, 'w', encoding='utf-8') as target:
            private_stat(temporary)
            json.dump(value, target, indent=2, sort_keys=True)
            target.write('\n')
            target.flush()
            os.fsync(target.fileno())
        if not exclusive:
            os.replace(temporary, path)
    finally:
        if not exclusive:
            temporary.unlink(missing_ok=True)


def read_json(path):
    info = private_stat(path)
    if info.st_size > 65536:
        raise SigningError('Credential state is oversized')
    descriptor = os.open(path, os.O_RDONLY | os.O_NOFOLLOW)
    with os.fdopen(descriptor, encoding='utf-8') as source:
        value = json.load(source)
    if not isinstance(value, dict):
        raise SigningError('Credential state is invalid')
    return value


def file_identity(path):
    info = path.stat()
    return [info.st_dev, info.st_ino]


class Executor:
    def __init__(self, environ):
        # Children need only the specific credentials supplied in their argv.
        self.environ = {key: value for key, value in environ.items() if key not in SECRET_NAMES}

    def __call__(self, arguments, operation, *, timeout=120):
        try:
            result = subprocess.run(arguments, capture_output=True, text=True,
                                    stdin=subprocess.DEVNULL, timeout=timeout,
                                    env=self.environ, check=False)
        except subprocess.TimeoutExpired:
            raise SigningError(f'{operation} timed out; raw tool output withheld') from None
        except Exception:
            raise SigningError(f'{operation} could not run; raw tool output withheld') from None
        if result.returncode:
            raise SigningError(f'{operation} failed; raw tool output withheld')
        return result.stdout


def search_entries(run):
    output = run(['/usr/bin/security', 'list-keychains', '-d', 'user'], 'Read keychain search list')
    try:
        entries = shlex.split(output)
    except ValueError:
        raise SigningError('Cannot parse keychain search list') from None
    if any(not Path(item).is_absolute() for item in entries):
        raise SigningError('Cannot parse keychain search list')
    return entries


def ensure_search_entry(keychain, run):
    # codesign(1): --keychain narrows identity selection, but the supplied file
    # must also be in the user search list for certificate-chain construction.
    # Preserve every existing entry and append only our isolated keychain.
    entries = search_entries(run)
    if str(keychain) not in entries:
        run(['/usr/bin/security', 'list-keychains', '-d', 'user', '-s', *entries, str(keychain)],
            'Add temporary keychain to search list')


def remove_search_entry(keychain, run):
    # Remove only our exact path, preserving pre-existing/concurrent entries.
    entries = search_entries(run)
    kept = [item for item in entries if item != str(keychain)]
    if kept != entries:
        run(['/usr/bin/security', 'list-keychains', '-d', 'user', '-s', *kept],
            'Restore keychain search list')


def select_identity(output, team):
    identities = []
    for line in output.splitlines():
        match = IDENTITY_PATTERN.fullmatch(line)
        if match:
            fingerprint, identity = match.groups()
            if identity.startswith('Developer ID Application: '):
                identities.append((fingerprint.upper(), identity))
    # find-identity -v -p codesigning limits results to currently valid
    # certificate/private-key identities. Do not choose a first/partial match.
    if len(identities) != 1 or not identities[0][1].endswith(f' ({team})'):
        raise SigningError('Expected exactly one valid Developer ID Application identity for APPLE_TEAM_ID')
    return identities[0]


def credential_filename(name):
    known = {STATE_NAME, P12_NAME, KEYCHAIN_NAME, KEYCHAIN_NAME + '-wal', KEYCHAIN_NAME + '-shm'}
    # Apple Security's AtomicFile hashes the keychain basename to a .flXXXXXXXX
    # lock name; its atomic-write API can also leave keychain .sb-* siblings.
    # Source: apple-oss-distributions/Security, OSX/libsecurity_filedb/lib/AtomicFile.cpp.
    return (name in known or name in KEYCHAIN_LOCK_NAMES or
            re.fullmatch(r'ownership\.json\.[a-f0-9]{32}\.tmp', name) is not None or
            re.fullmatch(r'signing\.keychain(?:-db)?\.sb-[A-Za-z0-9_-]{1,128}', name) is not None)


def owned_state(report, root, run_identity):
    directory = Path(report.get('state', '')).parent
    if (directory.parent != root or not DIRECTORY_PATTERN.fullmatch(directory.name) or
            report.get('state') != str(directory / STATE_NAME) or
            report.get('keychain_path') != str(directory / KEYCHAIN_NAME) or
            report.get('notary_profile') != 'tetorica-notary-' + directory.name[len(DIRECTORY_PREFIX):]):
        raise SigningError('Refusing credential paths outside the owned runner directory')
    private_stat(directory, directory=True)
    state = read_json(directory / STATE_NAME)
    if (state.get('schema_version') != 1 or state.get('owner') != 'tetorica-ci-macos-signing' or
            state.get('run') != run_identity or state.get('directory_identity') != file_identity(directory) or
            state.get('report') != report.get('report') or
            not re.fullmatch(r'[a-f0-9]{64}', str(report.get('ownership_id', ''))) or
            state.get('ownership_id') != report.get('ownership_id') or
            state.get('keychain_path') != report.get('keychain_path')):
        raise SigningError('Credential ownership journal does not match this Actions run')
    for child in directory.iterdir():
        if not credential_filename(child.name):
            raise SigningError('Refusing an unexpected file in the credential directory')
        # Keychain permissions are set by security, but never accept links.
        info = child.lstat()
        if not stat.S_ISREG(info.st_mode) or info.st_uid != os.getuid() or info.st_nlink != 1:
            raise SigningError('Refusing an unowned or linked credential file')
    return directory, state


def cleanup(report_path, *, environ=None, run=None):
    environ = os.environ if environ is None else environ
    root, run_identity = context(environ)
    path = report_location(report_path, root)
    if not path.exists() and not path.is_symlink():
        return {'status': 'absent'}
    report = read_json(path)
    if report.get('report') != str(path) or report.get('schema_version') != 1:
        raise SigningError('Credential report does not match its location')
    if report.get('status') == 'cleaned':
        return report
    directory, state = owned_state(report, root, run_identity)
    execute = Executor(environ) if run is None else run
    failed = False
    # Even if the Apple CLI fails, remove private material from disk. Keep the
    # journal if search-list cleanup needs another attempt in the always step.
    keychain = directory / KEYCHAIN_NAME
    try:
        remove_search_entry(keychain, execute)
    except Exception:
        failed = True
    if state.get('keychain_creation_attempted') and keychain.exists():
        try:
            execute(['/usr/bin/security', 'delete-keychain', str(keychain)], 'Delete temporary keychain')
        except Exception:
            # An owned file can still be unlinked if securityd is unavailable.
            pass
    companions = []
    for candidate in directory.iterdir():
        if candidate.name == STATE_NAME:
            continue
        # Recheck after securityd deletes/rewrites its files. Never follow links.
        info = candidate.lstat()
        if (not credential_filename(candidate.name) or not stat.S_ISREG(info.st_mode) or
                info.st_uid != os.getuid() or info.st_nlink != 1):
            raise SigningError('Refusing an unowned, linked, or unexpected credential companion')
        if candidate.name not in (P12_NAME, KEYCHAIN_NAME):
            companions.append(candidate.name)
        candidate.unlink()
    report['removed_keychain_companions'] = sorted(companions)
    report['status'] = 'cleanup_failed' if failed else 'cleaned'
    write_json(path, report)
    if failed:
        raise SigningError('Temporary credential files removed; keychain search-list cleanup needs retry')
    (directory / STATE_NAME).unlink()
    directory.rmdir()
    return report


def prepare(report_path, *, environ=None, run=None):
    environ = os.environ if environ is None else environ
    root, run_identity = context(environ)
    path = report_location(report_path, root)
    if path.exists() or path.is_symlink():
        raise SigningError('Refusing to replace an existing credential report')
    if any(not environ.get(name) for name in SECRET_NAMES):
        raise SigningError('One or more required signing/notary environment secrets are missing')
    team = environ['APPLE_TEAM_ID']
    if not re.fullmatch(r'[A-Z0-9]{10}', team):
        raise SigningError('APPLE_TEAM_ID must be a ten-character Apple team identifier')
    expected_team = environ.get('EXPECTED_TEAM_ID', '')
    if not re.fullmatch(r'[A-Z0-9]{10}', expected_team) or team != expected_team:
        raise SigningError('APPLE_TEAM_ID does not match the expected public team identifier')
    encoded = environ['BUILD_CERTIFICATE_BASE64']
    if len(encoded) > MAX_P12_BYTES * 2:
        raise SigningError('Signing certificate input is oversized')
    # Both a single-line macOS base64 export and the line-wrapped output of
    # other base64 tools are common repository-secret inputs. Ignore ASCII
    # whitespace only; validate every remaining character strictly.
    encoded = encoded.translate({ord(character): None for character in ' \t\r\n'})
    try:
        p12 = base64.b64decode(encoded, validate=True)
    except (ValueError, binascii.Error):
        raise SigningError('Signing certificate input is not valid base64') from None
    if not p12 or len(p12) > MAX_P12_BYTES:
        raise SigningError('Signing certificate input is empty or oversized')
    execute = Executor(environ) if run is None else run
    directory = Path(tempfile.mkdtemp(prefix=DIRECTORY_PREFIX, dir=root))
    directory.chmod(0o700)
    keychain = directory / KEYCHAIN_NAME
    profile = 'tetorica-notary-' + directory.name[len(DIRECTORY_PREFIX):]
    report = {'schema_version': 1, 'status': 'preparing', 'report': str(path),
              'state': str(directory / STATE_NAME), 'keychain_path': str(keychain),
              'notary_profile': profile, 'team_id': team,
              'identity_sha1': None, 'signing_identity': None,
              'ownership_id': secrets.token_hex(32)}
    journal_ready = False
    try:
        write_json(path, report, exclusive=True)
        state = {'schema_version': 1, 'owner': 'tetorica-ci-macos-signing',
                 'run': run_identity, 'directory_identity': file_identity(directory),
                 'report': str(path), 'ownership_id': report['ownership_id'],
                 'keychain_path': str(keychain), 'keychain_creation_attempted': False}
        write_json(directory / STATE_NAME, state, exclusive=True)
        journal_ready = True
        descriptor = os.open(directory / P12_NAME, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(descriptor, 'wb') as target:
            target.write(p12)
        # The ephemeral password lives only in this process, never the journal.
        password = secrets.token_urlsafe(48)
        state['keychain_creation_attempted'] = True
        write_json(directory / STATE_NAME, state)
        execute(['/usr/bin/security', 'create-keychain', '-p', password, str(keychain)],
                'Create temporary keychain')
        execute(['/usr/bin/security', 'set-keychain-settings', '-lut', '21600', str(keychain)],
                'Configure temporary keychain')
        execute(['/usr/bin/security', 'unlock-keychain', '-p', password, str(keychain)],
                'Unlock temporary keychain')
        execute(['/usr/bin/security', 'import', str(directory / P12_NAME), '-k', str(keychain),
                 '-P', environ['P12_PASSWORD'], '-t', 'agg', '-f', 'pkcs12',
                 '-T', '/usr/bin/codesign'], 'Import signing identity')
        (directory / P12_NAME).unlink()
        execute(['/usr/bin/security', 'set-key-partition-list', '-S', 'apple-tool:,apple:,codesign:',
                 '-s', '-k', password, str(keychain)], 'Authorize codesign access')
        identities = execute(['/usr/bin/security', 'find-identity', '-v', '-p', 'codesigning', str(keychain)],
                             'Find valid signing identity')
        fingerprint, identity = select_identity(identities, team)
        execute(['/usr/bin/xcrun', 'notarytool', 'store-credentials', profile,
                 '--apple-id', environ['APPLE_ID'], '--team-id', team,
                 '--password', environ['APPLE_APP_SPECIFIC_PASSWORD'], '--keychain', str(keychain)],
                'Store notary credentials', timeout=180)
        ensure_search_entry(keychain, execute)
        report['keychain_companions'] = sorted(
            child.name for child in directory.iterdir()
            if credential_filename(child.name) and child.name not in (STATE_NAME, KEYCHAIN_NAME, P12_NAME))
        report.update(status='prepared', identity_sha1=fingerprint, signing_identity=identity)
        write_json(path, report)
        return report
    except BaseException as error:
        cleanup_failed = False
        if journal_ready:
            try:
                cleanup(path, environ=environ, run=execute)
            except BaseException:
                cleanup_failed = True
        else:
            # No Apple command ran before both the report and journal existed.
            for child in directory.iterdir():
                child.unlink()
            directory.rmdir()
        detail = str(error) if isinstance(error, SigningError) else 'Credential preparation failed; details withheld'
        if cleanup_failed:
            detail += '; run the always-step cleanup again'
        raise SigningError(detail) from None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('operation', choices=('prepare', 'cleanup'))
    parser.add_argument('--report', type=Path, required=True)
    arguments = parser.parse_args()
    try:
        result = globals()[arguments.operation](arguments.report)
    except SigningError as error:
        print(str(error), file=sys.stderr)
        return 1
    except Exception:
        print('Credential operation failed; details withheld', file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    sys.exit(main())
