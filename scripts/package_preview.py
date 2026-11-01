#!/usr/bin/env python3
"""Build or verify self-contained Preview ZIPs; never publish or install them."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PRODUCT = 'tetorica-spu-plugin'
CONTENT = {'macos': ('TetoricaSPU.app', 'TetoricaSPU.component', 'TetoricaSPU.vst3'),
           'windows': ('TetoricaSPU.vst3',)}
LABEL = {'macos': 'macos-universal', 'windows': 'windows-x64-vst3'}
MAX_BYTES = 128 * 1024 * 1024


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_name(name):
    p = PurePosixPath(name)
    if not name or ':' in name or '\\' in name or any(ord(c) < 32 for c in name) or p.is_absolute() or '..' in p.parts or str(p) != name:
        raise ValueError('Unsafe archive path: ' + name)
    return p


def inventory(directory):
    files = {}
    for path in sorted(directory.rglob('*')):
        if path.is_dir() and not path.is_symlink():
            continue
        name = path.relative_to(directory).as_posix()
        mode = path.lstat().st_mode
        if path.is_symlink():
            target = os.readlink(path)
            if Path(target).is_absolute() or not path.resolve().is_relative_to(directory.resolve()):
                raise ValueError('Symlink leaves package: ' + name)
            data = target.encode('utf-8')
            kind = 'symlink'
        elif path.is_file():
            data = path.read_bytes()
            kind = 'file'
        else:
            raise ValueError('Special package entry: ' + name)
        files[name] = {'kind': kind, 'mode': stat.S_IMODE(mode), 'bytes': len(data),
                       'sha256': digest(data)}
    return files


def inspect(platform, directory):
    script = ROOT / 'scripts' / ('check_macos_bundles.py' if platform == 'macos'
                                 else 'check_windows_bundles.py')
    process = subprocess.run([sys.executable, str(script), '--input', str(directory)],
                             capture_output=True, text=True)
    if process.returncode:
        raise ValueError('Binary inspection failed:\n' + process.stdout + process.stderr)
    return json.loads(process.stdout)


def instructions(platform, version, commit):
    common = f"""Tetorica SPU Plugin {version} Preview ({commit[:12]})

Development candidate, not an official release.
DAW state saves use schema 10. Keep earlier project copies before testing;
older plugin builds cannot read new saves. Old sound sources retain their
legacy playback when restored by this build.
No game sample banks are included.
"""
    if platform == 'macos':
        return common + """
macOS 13 or newer, Apple Silicon and Intel.
TetoricaSPU.app: standalone application.
TetoricaSPU.component: AUv2, place in ~/Library/Audio/Plug-Ins/Components/.
TetoricaSPU.vst3: place in ~/Library/Audio/Plug-Ins/VST3/.
Quit audio hosts and retain existing plugin bundles before changing them.
These copied bundles have valid ad-hoc signatures, not Developer ID signatures.
They are not notarized; macOS may prevent opening downloaded development builds.
Real-device listening and long-session DAW use remain separate checks.
"""
    return common + r"""
Windows x64 VST3 instrument. Copy the entire TetoricaSPU.vst3 directory to
C:\Program Files\Common Files\VST3\ (or a VST3 folder configured in your DAW).
Keep the .vst3 directory structure intact; restart/rescan the DAW afterward.
This candidate does not include a standalone EXE, AU, Linux plugin or ASIO SDK.
The VST3 uses your DAW's audio driver. It is not Authenticode-signed.
Real-device listening and long-session DAW use remain separate checks.
"""


def build(args):
    if (args.platform == 'macos') != (sys.platform == 'darwin'):
        raise ValueError('Build this candidate on its target OS')
    if args.platform == 'windows' and sys.platform != 'win32':
        raise ValueError('Windows candidate inspection requires Windows')
    dirty = subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT, text=True)
    if dirty.strip():
        raise ValueError('Commit source/document changes before packaging; working tree is dirty')
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    config = (ROOT / 'plugin/config.h').read_text()
    version = re.search(r'#define PLUG_VERSION_STR "([^"]+)"', config).group(1)
    if not re.fullmatch(r'[0-9]+\.[0-9]+\.[0-9]+', version):
        raise ValueError('Unsafe version')
    slug = f'{PRODUCT}-{version}-preview-{commit[:12]}-{LABEL[args.platform]}'
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    destination, archive = output / slug, output / (slug + '.zip')
    if destination.exists() or archive.exists():
        raise ValueError('Refusing to replace an existing candidate: ' + slug)
    with tempfile.TemporaryDirectory(prefix='.preview-', dir=output) as temp:
        stage = Path(temp) / slug
        stage.mkdir()
        for name in CONTENT[args.platform]:
            source = args.input / name
            if source.is_symlink() or not source.is_dir():
                raise ValueError('Missing/non-directory/symlink bundle: ' + str(source))
            shutil.copytree(source, stage / name, symlinks=True)
        shutil.copyfile(ROOT / 'LICENSE', stage / 'LICENSE.txt')
        shutil.copyfile(ROOT / 'plugin/resources/THIRD_PARTY_NOTICES.txt',
                        stage / 'THIRD_PARTY_NOTICES.txt')
        if args.platform == 'windows':
            shutil.copyfile(ROOT / 'plugin/resources/WINDOWS_THIRD_PARTY_NOTICES.txt',
                            stage / 'WINDOWS_THIRD_PARTY_NOTICES.txt')
        (stage / 'INSTALL.txt').write_text(instructions(args.platform, version, commit),
                                          encoding='utf-8')
        if args.platform == 'macos':
            if not args.ad_hoc_sign:
                raise ValueError('macOS Preview requires explicit --ad-hoc-sign of staging copies')
            for name in CONTENT['macos']:
                subprocess.run(['codesign', '--force', '--sign', '-', str(stage / name)],
                               check=True, capture_output=True, text=True)
        checked = inspect(args.platform, stage)
        files = inventory(stage)
        metadata = {'schema': 1, 'product': PRODUCT, 'version': version,
                    'channel': 'preview', 'source_commit': commit, 'platform': args.platform,
                    'label': LABEL[args.platform], 'bundles': list(CONTENT[args.platform]),
                    'signing': 'ad-hoc; not notarized' if args.platform == 'macos' else 'unsigned',
                    'dependencies': json.loads((ROOT / 'dependencies.lock.json').read_text()),
                    'files': files}
        (stage / 'manifest.json').write_text(json.dumps(metadata, indent=2) + '\n')
        temp_zip = Path(temp) / 'candidate.zip'
        with zipfile.ZipFile(temp_zip, 'w', compression=zipfile.ZIP_DEFLATED) as z:
            for path in sorted(stage.rglob('*')):
                if path.is_dir() and not path.is_symlink():
                    continue
                name = slug + '/' + path.relative_to(stage).as_posix()
                info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                info.create_system = 3
                info.external_attr = path.lstat().st_mode << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                z.writestr(info, os.readlink(path).encode() if path.is_symlink() else path.read_bytes())
        verified = verify(temp_zip)
        if verified['source_commit'] != commit:
            raise ValueError('Packaged source identity changed')
        stage.rename(destination)
        temp_zip.rename(archive)
    sha = digest(archive.read_bytes())
    checksum = archive.with_suffix('.zip.sha256')
    checksum.write_text(sha + '  ' + archive.name + '\n')
    return {'ok': True, 'archive': str(archive), 'sha256': sha,
            'staging_directory': str(destination), 'source_commit': commit,
            'platform': args.platform, 'inspection': checked}


def verify(archive, extract_to=None):
    with zipfile.ZipFile(archive) as z:
        entries = z.infolist()
        if len(entries) > 2000 or sum(i.file_size for i in entries) > MAX_BYTES:
            raise ValueError('Archive exceeds candidate bounds')
        names = [i.filename for i in entries]
        if len(names) != len(set(names)):
            raise ValueError('Duplicate archive entries')
        roots = {safe_name(n).parts[0] for n in names}
        if len(roots) != 1:
            raise ValueError('Archive must contain one candidate root')
        slug = roots.pop()
        if not re.fullmatch(PRODUCT + r'-[0-9.]+-preview-[0-9a-f]{12}-(macos-universal|windows-x64-vst3)', slug):
            raise ValueError('Unexpected candidate root')
        manifest_name = slug + '/manifest.json'
        metadata = json.loads(z.read(manifest_name))
        if metadata['schema'] != 1 or metadata['product'] != PRODUCT or metadata['platform'] not in CONTENT:
            raise ValueError('Unknown package manifest')
        if metadata['bundles'] != list(CONTENT[metadata['platform']]):
            raise ValueError('Unexpected platform bundle list')
        expected_slug = f"{PRODUCT}-{metadata['version']}-preview-{metadata['source_commit'][:12]}-{LABEL[metadata['platform']]}"
        if expected_slug != slug:
            raise ValueError('Manifest/archive identity mismatch')
        if metadata['platform'] == 'windows':
            folded = set()
            reserved = {'CON', 'PRN', 'AUX', 'NUL'} | {f'{p}{n}' for p in ('COM', 'LPT') for n in range(1, 10)}
            for name in metadata['files']:
                parts = safe_name(name).parts
                if any(p.endswith(('.', ' ')) or p.split('.')[0].upper() in reserved for p in parts):
                    raise ValueError('Reserved/aliased Windows path')
                if name.casefold() in folded:
                    raise ValueError('Case-insensitive Windows path collision')
                folded.add(name.casefold())
        required = {'INSTALL.txt', 'LICENSE.txt', 'THIRD_PARTY_NOTICES.txt'}
        if metadata['platform'] == 'windows':
            required |= {'WINDOWS_THIRD_PARTY_NOTICES.txt',
                         'TetoricaSPU.vst3/Contents/x86_64-win/TetoricaSPU.vst3'}
        else:
            required |= {name + '/Contents/MacOS/TetoricaSPU' for name in CONTENT['macos']}
        if not required.issubset(metadata['files']):
            raise ValueError('Missing required binaries or package documentation')
        expected = {slug + '/' + safe_name(name).as_posix() for name in metadata['files']}
        if set(names) != expected | {manifest_name}:
            raise ValueError('Missing/unrecorded archive entries')
        symlinks = []
        for name, spec in metadata['files'].items():
            full = slug + '/' + name
            info, data = z.getinfo(full), z.read(full)
            mode = info.external_attr >> 16
            if len(data) != spec['bytes'] or digest(data) != spec['sha256'] or stat.S_IMODE(mode) != spec['mode']:
                raise ValueError('Archive bytes/permissions mismatch: ' + name)
            if spec['kind'] == 'symlink':
                if not stat.S_ISLNK(mode):
                    raise ValueError('Symlink kind mismatch')
                target = data.decode('utf-8')
                if Path(target).is_absolute() or ':' in target or '\\' in target:
                    raise ValueError('Unsafe symlink target')
                # Resolve lexically within the candidate; do not follow disk links.
                parts = list(PurePosixPath(name).parent.parts)
                for part in PurePosixPath(target).parts:
                    if part == '..':
                        if not parts:
                            raise ValueError('Symlink leaves candidate')
                        parts.pop()
                    elif part != '.':
                        parts.append(part)
                if '/'.join(parts) not in metadata['files']:
                    raise ValueError('Dangling/unrecorded symlink')
                symlinks.append((name, target))
            elif spec['kind'] != 'file' or not stat.S_ISREG(mode):
                raise ValueError('Unexpected archive file type')
        checksum = Path(archive).with_suffix('.zip.sha256')
        if checksum.exists() and checksum.read_text().split()[0] != digest(Path(archive).read_bytes()):
            raise ValueError('ZIP checksum mismatch')
        result = {'ok': True, 'archive': str(archive), 'source_commit': metadata['source_commit'],
                  'platform': metadata['platform'], 'files': len(metadata['files'])}
        if extract_to is not None:
            destination = extract_to.resolve() / slug
            if destination.exists():
                raise ValueError('Refusing to merge into existing extracted candidate')
            destination.mkdir(parents=True)
            # Write regular files first; never extract through a symlink.
            for name in metadata['files'] | {'manifest.json': {'kind': 'file', 'mode': 0o644}}:
                spec = metadata['files'].get(name, {'kind': 'file', 'mode': 0o644})
                if spec['kind'] != 'file':
                    continue
                path = destination / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(z.read(slug + '/' + name))
                path.chmod(spec['mode'])
            for name, target in symlinks:
                path = destination / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.symlink_to(target)
            result['staging_directory'] = str(destination)
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    subs = parser.add_subparsers(dest='command', required=True)
    b = subs.add_parser('build')
    b.add_argument('--platform', choices=list(CONTENT), required=True)
    b.add_argument('--input', type=Path, required=True)
    b.add_argument('--output', type=Path, default=ROOT / 'release')
    b.add_argument('--ad-hoc-sign', action='store_true')
    b.add_argument('--report', type=Path)
    v = subs.add_parser('verify')
    v.add_argument('archive', type=Path)
    v.add_argument('--extract-to', type=Path)
    v.add_argument('--report', type=Path)
    args = parser.parse_args()
    try:
        report = build(args) if args.command == 'build' else verify(args.archive, args.extract_to)
        text = json.dumps(report, indent=2) + '\n'
        if args.report:
            args.report.parent.mkdir(parents=True, exist_ok=True)
            args.report.write_text(text)
        print(text, end='')
    except (ValueError, OSError, KeyError, zipfile.BadZipFile, subprocess.CalledProcessError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
