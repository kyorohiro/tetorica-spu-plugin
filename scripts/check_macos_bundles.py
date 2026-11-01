#!/usr/bin/env python3
"""Inspect a macOS Universal preview in place; never sign, install or launch it."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import stat
import subprocess
import sys


PROJECT_ROOT = Path(__file__).resolve().parents[1]
BUNDLES = (('app', 'TetoricaSPU.app'), ('au', 'TetoricaSPU.component'),
           ('vst3', 'TetoricaSPU.vst3'))
REQUIRED_ARCHITECTURES = {'arm64', 'x86_64'}
MACHO_MAGICS = {bytes.fromhex(value) for value in (
    'feedface', 'cefaedfe', 'feedfacf', 'cffaedfe',
    'cafebabe', 'bebafeca', 'cafebabf', 'bfbafeca')}
HARDENED_RUNTIME = 0x10000


def developer_id_requirement(team_id):
    if not isinstance(team_id, str) or not re.fullmatch(r'[A-Z0-9]{10}', team_id):
        raise ValueError('Expected Team ID must contain exactly 10 uppercase letters or digits')
    # A leading '=' makes this literal requirement source, rather than a path.
    return ('=anchor apple generic and '
            'certificate leaf[field.1.2.840.113635.100.6.1.13] exists and '
            f'certificate leaf[subject.OU] = "{team_id}"')


def run_command(arguments):
    result = subprocess.run(arguments, capture_output=True, text=True, timeout=30)
    if result.returncode:
        detail = (result.stderr or result.stdout).strip()[:4096]
        raise ValueError(f'{Path(arguments[0]).name} failed ({result.returncode}): {detail}')
    return result.stdout + result.stderr


def sha256(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def inside(path, root):
    try:
        path.relative_to(root)
        return True
    except ValueError:
        return False


def project_version(project_root):
    config = (project_root / 'plugin/config.h').read_text(encoding='utf-8')
    version = re.search(r'^#define\s+PLUG_VERSION_STR\s+"([^"]+)"', config, re.MULTILINE)
    numeric = re.search(r'^#define\s+PLUG_VERSION_HEX\s+(0x[0-9a-fA-F]+)', config, re.MULTILINE)
    if not version or not numeric:
        raise ValueError('Cannot read the project version in plugin/config.h')
    cmake = (project_root / 'CMakeLists.txt').read_text(encoding='utf-8')
    cmake_version = re.search(r'project\(\s*tetorica_spu_plugin\s+VERSION\s+([0-9.]+)', cmake)
    if not cmake_version or cmake_version.group(1) != version.group(1):
        raise ValueError('CMake project version and plugin/config.h disagree')
    parts = version.group(1).split('.')
    if len(parts) != 3 or not all(item.isdigit() for item in parts):
        raise ValueError('Project version must contain major.minor.patch')
    major, minor, patch = map(int, parts)
    number = int(numeric.group(1), 16)
    if major > 65535 or minor > 255 or patch > 255 or number != (major << 16 | minor << 8 | patch):
        raise ValueError('Numeric plugin version and project version disagree')
    return version.group(1), number


def bundle_files(bundle):
    """Walk real files once; permit only portable, non-dangling internal links."""
    files, links = [], []
    for directory, directories, names in os.walk(bundle, followlinks=False):
        for name in sorted(directories + names):
            path = Path(directory) / name
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                target = os.readlink(path)
                if Path(target).is_absolute() or not inside(path.resolve(strict=True), bundle):
                    raise ValueError(f'External or absolute symlink: {path.relative_to(bundle)}')
                links.append({'path': path.relative_to(bundle).as_posix(), 'target': target})
            elif stat.S_ISREG(mode):
                files.append(path)
            elif not stat.S_ISDIR(mode):
                raise ValueError(f'Unsupported special file: {path.relative_to(bundle)}')
    return sorted(files), sorted(links, key=lambda item: item['path'])


def signature_fields(detail):
    fields = {}
    authorities = []
    for line in detail.splitlines():
        key, separator, value = line.partition('=')
        if separator:
            if key == 'Authority':
                authorities.append(value)
            else:
                fields[key] = value
    return fields, authorities


def signature(path, run, *, expected_team_id=None):
    # Verification includes every Universal slice. The default proves integrity
    # only. Developer ID checks still do not establish Gatekeeper/notary policy.
    verify = ['/usr/bin/codesign', '--verify', '--strict', '--all-architectures']
    if expected_team_id is not None:
        requirement = developer_id_requirement(expected_team_id)
        verify += ['--test-requirement', requirement]
    run(verify + [str(path)])
    if expected_team_id is not None:
        slices = {}
        for architecture in sorted(REQUIRED_ARCHITECTURES):
            # --all-architectures applies to verification, not display; without
            # --arch, codesign only reports the host's preferred slice.
            detail = run(['/usr/bin/codesign', '-d', '--verbose=4', '--arch',
                          architecture, str(path)])
            fields, authorities = signature_fields(detail)
            label = f'{path.name} ({architecture})'
            if fields.get('Signature') == 'adhoc' or not authorities:
                raise ValueError(f'{label} lacks a Developer ID Application signature')
            if fields.get('TeamIdentifier') != expected_team_id:
                raise ValueError(f'{label} has an unexpected TeamIdentifier')
            if (not authorities[0].startswith('Developer ID Application: ') or
                    not authorities[0].endswith(f' ({expected_team_id})')):
                raise ValueError(f'{label} lacks the expected Developer ID Application authority')
            flags = re.search(r'^CodeDirectory\b[^\r\n]*\bflags=(0x[0-9a-fA-F]+)\b',
                              detail, re.MULTILINE)
            if flags is None or not int(flags.group(1), 16) & HARDENED_RUNTIME:
                raise ValueError(f'{label} does not enable the hardened runtime')
            timestamp = fields.get('Timestamp', '').strip()
            if not timestamp or timestamp.lower() in ('none', 'not set', 'not-set', '(null)', '<none>'):
                raise ValueError(f'{label} lacks a trusted Timestamp')
            slices[architecture] = {'authorities': authorities,
                                    'teamIdentifier': fields['TeamIdentifier'],
                                    'cdhash': fields.get('CDHash'),
                                    'codeDirectoryFlags': flags.group(1),
                                    'hardenedRuntime': True, 'timestamp': timestamp}
        return {'valid': True, 'kind': 'developer-id-application',
                'teamIdentifier': expected_team_id, 'requirement': requirement,
                'architectures': slices, 'notarization': 'not-checked'}
    detail = run(['/usr/bin/codesign', '-d', '--verbose=4', str(path)])
    fields, authorities = signature_fields(detail)
    if fields.get('Signature') == 'adhoc':
        kind = 'ad-hoc'
    elif authorities:
        kind = 'certificate'
    else:
        raise ValueError(f'Cannot identify a verified code signature: {path.name}')
    return {'valid': True, 'kind': kind, 'authorities': authorities,
            'teamIdentifier': fields.get('TeamIdentifier'), 'cdhash': fields.get('CDHash'),
            'notarization': 'not-checked'}


def macho(path, bundle, run, *, expected_team_id=None):
    architectures = run(['/usr/bin/lipo', '-archs', str(path)]).strip().split()
    if set(architectures) != REQUIRED_ARCHITECTURES or len(architectures) != 2:
        raise ValueError(f'{path.relative_to(bundle)} is not exactly arm64 + x86_64: {architectures}')
    libraries = run(['/usr/bin/otool', '-arch', 'all', '-L', str(path)])
    dependencies = sorted(set(re.findall(r'^\s+(.+?)\s+\(compatibility version ', libraries, re.MULTILINE)))
    if not dependencies:
        raise ValueError(f'Cannot read linked dylibs in {path.relative_to(bundle)}')
    # The current plugin statically links all non-system code. Reject unresolved
    # @rpath/@loader_path or build/home/Homebrew libraries rather than guessing
    # which machine-local dependency a recipient might happen to have.
    external = [item for item in dependencies
                if not item.startswith(('/System/Library/', '/usr/lib/')) or '..' in Path(item).parts]
    if external:
        raise ValueError(f'{path.relative_to(bundle)} has non-system dylib dependencies: {external}')
    commands = run(['/usr/bin/otool', '-arch', 'all', '-l', str(path)])
    rpaths, minimums, current = [], [], None
    for line in commands.splitlines():
        entry = line.strip()
        if entry.startswith('cmd '):
            current = entry[4:]
        elif current == 'LC_RPATH' and entry.startswith('path '):
            value, marker, _ = entry[5:].rpartition(' (offset ')
            if not marker:
                raise ValueError(f'Cannot parse LC_RPATH in {path.relative_to(bundle)}')
            rpaths.append(value)
        elif current == 'LC_BUILD_VERSION' and entry.startswith('minos '):
            minimums.append(entry.split()[1])
        elif current == 'LC_VERSION_MIN_MACOSX' and entry.startswith('version '):
            minimums.append(entry.split()[1])
    if len(minimums) != len(architectures):
        raise ValueError(f'Cannot read the minimum macOS version for both slices: {path.relative_to(bundle)}')
    if any(not item.startswith(('/System/Library/', '/usr/lib/')) or '..' in Path(item).parts
           for item in rpaths):
        raise ValueError(f'{path.relative_to(bundle)} contains a non-system runtime search path: {rpaths}')
    return {'path': path.relative_to(bundle).as_posix(), 'sha256': sha256(path),
            'mode': oct(stat.S_IMODE(path.stat().st_mode)), 'architectures': sorted(architectures),
            'dylibDependencies': dependencies, 'rpaths': sorted(set(rpaths)),
            'minimumMacOS': sorted(set(minimums)),
            'signature': signature(path, run, expected_team_id=expected_team_id)}


def inspect_bundle(bundle, format_name, expected_version, numeric_version, project_root, run,
                   *, expected_team_id=None):
    if bundle.is_symlink() or not bundle.is_dir():
        raise ValueError('Missing bundle or bundle root is a symlink')
    bundle = bundle.resolve(strict=True)
    files, links = bundle_files(bundle)
    info_path = bundle / 'Contents/Info.plist'
    if not info_path.is_file() or info_path.stat().st_size > 1024 * 1024:
        raise ValueError('Missing or oversized Contents/Info.plist')
    with info_path.open('rb') as source:
        info = plistlib.load(source)
    if not isinstance(info, dict):
        raise ValueError('Info.plist must be a dictionary')
    for key in ('CFBundleVersion', 'CFBundleShortVersionString'):
        if info.get(key) != expected_version:
            raise ValueError(f'{key} does not match project version {expected_version}')
    executable_name = info.get('CFBundleExecutable')
    if not isinstance(executable_name, str) or not executable_name or Path(executable_name).name != executable_name:
        raise ValueError('Unsafe or missing CFBundleExecutable')
    executable = bundle / 'Contents/MacOS' / executable_name
    if not executable.is_file() or not executable.stat().st_mode & 0o111:
        raise ValueError('Bundle executable is absent or lost its executable permissions')
    if format_name == 'au':
        components = info.get('AudioComponents')
        if not isinstance(components, list) or len(components) != 1 or not isinstance(components[0], dict):
            raise ValueError('AU must contain exactly one AudioComponents dictionary')
        component = components[0]
        if (component.get('version') != numeric_version or component.get('type') != 'aumu' or
                component.get('subtype') != 'Tspu' or component.get('manufacturer') != 'Tetr'):
            raise ValueError('AU identity/version differs from the project')
    resources = bundle / 'Contents/Resources'
    required = {'Roboto-Regular.ttf': project_root / 'plugin/resources/fonts/Roboto-Regular.ttf',
                'THIRD_PARTY_NOTICES.txt': project_root / 'plugin/resources/THIRD_PARTY_NOTICES.txt'}
    resource_hashes = {}
    for name, original in required.items():
        target = resources / name
        if not target.is_file() or target.stat().st_size == 0 or sha256(target) != sha256(original):
            raise ValueError(f'Required bundled resource is absent or stale: {name}')
        resource_hashes[name] = sha256(target)
    if format_name == 'app':
        nib = info.get('NSMainNibFile')
        if not isinstance(nib, str) or Path(nib).name != nib or not (resources / (nib + '.nib')).exists():
            raise ValueError('Standalone main-menu nib is absent')
    binaries = []
    for path in files:
        with path.open('rb') as source:
            magic = source.read(4)
        if magic in MACHO_MAGICS:
            binaries.append(macho(path, bundle, run, expected_team_id=expected_team_id))
    if not any(item['path'] == executable.relative_to(bundle).as_posix() for item in binaries):
        raise ValueError('CFBundleExecutable is not a Mach-O binary')
    minimum = info.get('LSMinimumSystemVersion')
    def version_tuple(value):
        if not isinstance(value, str) or not re.fullmatch(r'[0-9]+(?:\.[0-9]+){0,2}', value):
            raise ValueError('Invalid or absent minimum macOS version')
        parts = tuple(int(item) for item in value.split('.'))
        return parts + (0,) * (3 - len(parts))
    declared_minimum = version_tuple(minimum)
    if any(not item['minimumMacOS'] or any(version_tuple(value) > declared_minimum
           for value in item['minimumMacOS']) for item in binaries):
        raise ValueError('Mach-O deployment target exceeds the Info.plist minimum macOS version')
    # Per-Mach-O verification above is not sufficient to seal resources/plist.
    run(['/usr/bin/codesign', '--verify', '--deep', '--strict', '--all-architectures', str(bundle)])
    return {'format': format_name, 'path': bundle.name, 'version': expected_version,
            'minimumSystemVersion': info.get('LSMinimumSystemVersion'), 'fileCount': len(files),
            'symlinks': links, 'resources': resource_hashes, 'machO': binaries,
            'signature': signature(bundle, run, expected_team_id=expected_team_id)}


def inspect_bundles(input_dir, project_root=PROJECT_ROOT, run=run_command, *, expected_team_id=None):
    input_dir, project_root = Path(input_dir).resolve(), Path(project_root).resolve()
    report = {'schema': 1, 'ok': False, 'input': str(input_dir), 'architectures': sorted(REQUIRED_ARCHITECTURES),
              'dependencyPolicy': 'system-only', 'signingPolicy': 'valid-signature-not-trust-assessed',
              'notarization': 'not-checked', 'bundles': [], 'errors': []}
    try:
        if expected_team_id is not None:
            report['signingRequirement'] = developer_id_requirement(expected_team_id)
            report['signingPolicy'] = 'developer-id-application-exact-team'
            report['expectedTeamIdentifier'] = expected_team_id
        version, numeric = project_version(project_root)
        report['version'] = version
    except (OSError, ValueError) as error:
        report['errors'].append(str(error))
        return report
    for format_name, name in BUNDLES:
        try:
            report['bundles'].append(inspect_bundle(input_dir / name, format_name, version,
                                                     numeric, project_root, run,
                                                     expected_team_id=expected_team_id))
        except (OSError, ValueError, RuntimeError, plistlib.InvalidFileException, subprocess.TimeoutExpired) as error:
            report['errors'].append(f'{name}: {error}')
    report['ok'] = not report['errors']
    return report


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, required=True, help='Directory containing APP, AU and VST3 bundles')
    parser.add_argument('--output', type=Path, help='Also save the JSON inspection report here')
    parser.add_argument('--expected-team-id',
                        help='Require this Developer ID Application Team ID, hardened runtime and trusted timestamps')
    args = parser.parse_args(argv)
    report = inspect_bundles(args.input, expected_team_id=args.expected_team_id)
    text = json.dumps(report, ensure_ascii=False, sort_keys=True, indent=2) + '\n'
    if args.output:
        args.output.write_text(text, encoding='utf-8')
    sys.stdout.write(text)
    return 0 if report['ok'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
