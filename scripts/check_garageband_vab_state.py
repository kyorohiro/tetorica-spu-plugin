#!/usr/bin/env python3
"""Read embedded AU XML class-info fragments from a saved GarageBand project.

This checks observed XML fragments, not GarageBand's undocumented container or
active-track binding. Never writes the project or resolves stored file paths.
Without --expected-vab this is an inspection of matching AU payloads only.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import plistlib
import struct

MAX_BANK = 1024 * 1024
MAX_STATE = MAX_BANK + 40
MAX_PROJECT_DATA = 64 * 1024 * 1024
MAX_XML = 2 * 1024 * 1024
MAX_ALTERNATIVES = 128
MAX_XML_FRAGMENTS = 4096
IDENTITY = {'type': int.from_bytes(b'aumu', 'big'),
            'subtype': int.from_bytes(b'Tspu', 'big'),
            'manufacturer': int.from_bytes(b'Tetr', 'big')}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def bounded_read(path, maximum):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f'Expected a regular, non-symlink file: {path}')
    if path.stat().st_size > maximum:
        raise ValueError(f'File exceeds {maximum} bytes: {path}')
    with path.open('rb') as source:
        data = source.read(maximum + 1)
    if len(data) > maximum:
        raise ValueError(f'File grew beyond {maximum} bytes: {path}')
    return data


def project_files(project):
    if project.is_file():
        return [project]
    if project.is_symlink() or not project.is_dir():
        raise ValueError(f'Expected a project directory or ProjectData file: {project}')
    found = []
    # Both root spellings are seen on macOS. Inodes deduplicate case aliases;
    # files themselves must be ordinary files, never links to external media.
    for name in ('ProjectData', 'projectData'):
        path = project / name
        if path.exists():
            found.append(path)
    alternatives = project / 'Alternatives'
    if alternatives.exists():
        if alternatives.is_symlink() or not alternatives.is_dir():
            raise ValueError('Alternatives must be a non-symlink directory')
        directories = sorted(alternatives.iterdir())
        if len(directories) > MAX_ALTERNATIVES:
            raise ValueError('Too many alternative entries for bounded inspection')
        for directory in directories:
            if directory.is_symlink():
                raise ValueError(f'Refusing an alternative symlink: {directory}')
            if directory.is_dir() and (directory / 'ProjectData').exists():
                found.append(directory / 'ProjectData')
    unique = {}
    for path in found:
        if path.is_symlink():
            raise ValueError(f'Refusing a ProjectData symlink: {path}')
        identity = path.stat()
        unique.setdefault((identity.st_dev, identity.st_ino), path)
    if not unique:
        raise ValueError('No root or Alternatives/*/ProjectData files found')
    return list(unique.values())


def decode_payload(payload):
    if not isinstance(payload, bytes):
        raise ValueError('AU data is not a byte string')
    if len(payload) > MAX_STATE:
        raise ValueError('AU data exceeds the custom-state bound')
    result = {'payload_bytes': len(payload), 'payload_sha256': sha(payload)}
    if len(payload) == 8 and payload != b'TSPUST01':
        gain, = struct.unpack('<d', payload)
        if not math.isfinite(gain) or not 0 <= gain <= 100:
            raise ValueError('Invalid legacy Gain')
        return dict(result, schema='legacy-gain-only', kind='Builtin',
                    program=0, tone=0, gain_percent=gain,
                    bank_bytes=0, bank_sha256=None), b''
    if len(payload) < 40 or payload[:8] != b'TSPUST01':
        raise ValueError('Expected complete TSPUST01 custom state or legacy Gain')
    schema, total, kind, program, tone, size = struct.unpack_from('<6I', payload, 8)
    if schema != 1 or total != len(payload) or size > MAX_BANK or total != 40 + size:
        raise ValueError('Invalid state schema, total or bank length')
    if kind not in (0, 1) or program > 127 or tone > 15:
        raise ValueError('Invalid state kind or sparse selection')
    if kind == 0 and (size or program or tone):
        raise ValueError('Builtin state has an unexpected bank or selection')
    if kind == 1 and not size:
        raise ValueError('VAB state has no embedded bank')
    gain, = struct.unpack_from('<d', payload, 32 + size)
    if not math.isfinite(gain) or not 0 <= gain <= 100:
        raise ValueError('Invalid custom-state Gain')
    bank = payload[32:32 + size]
    return dict(result, schema=schema, kind='VAB' if kind else 'Builtin',
                program=program, tone=tone, gain_percent=gain,
                bank_bytes=size, bank_sha256=sha(bank) if size else None), bank


def scan_file(path):
    data = bounded_read(path, MAX_PROJECT_DATA)
    records, position, xml_count, malformed = [], 0, 0, 0
    while True:
        start = data.find(b'<?xml', position)
        if start < 0:
            break
        xml_count += 1
        if xml_count > MAX_XML_FRAGMENTS:
            raise ValueError(f'Too many XML fragments in {path}')
        end = data.find(b'</plist>', start, min(len(data), start + MAX_XML))
        if end < 0:
            malformed += 1
            position = start + 5
            continue
        end += len(b'</plist>')
        position = end
        fragment = data[start:end]
        try:
            if b'<!ENTITY' in fragment:
                raise ValueError('Entity declarations are unsupported')
            obj = plistlib.loads(fragment)
        except Exception:
            malformed += 1
            continue
        if not isinstance(obj, dict) or any(obj.get(key) != value for key, value in IDENTITY.items()):
            continue
        record = {'file': str(path), 'xml_offset': start, 'xml_end': end,
                  'xml_bytes': end - start, 'xml_sha256': sha(fragment),
                  'au_identity': IDENTITY,
                  'au_version': obj.get('version') if isinstance(obj.get('version'), int) else None}
        try:
            fields, bank = decode_payload(obj.get('data'))
            record.update(fields)
            records.append((record, bank))
        except ValueError as error:
            record['error'] = str(error)
            records.append((record, None))
    return records, {'file': str(path), 'bytes': len(data), 'sha256': sha(data),
                     'xml_fragments': xml_count, 'unparsed_xml_fragments': malformed}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('project', type=Path)
    parser.add_argument('--expected-vab', type=Path)
    parser.add_argument('--program', type=int, default=3)
    parser.add_argument('--tone', type=int, default=1)
    parser.add_argument('--gain', type=float, default=99.)
    args = parser.parse_args()
    if not 0 <= args.program <= 127 or not 0 <= args.tone <= 15 or not math.isfinite(args.gain) or not 0 <= args.gain <= 100:
        parser.error('Program, Tone or Gain is out of range')
    expected = bounded_read(args.expected_vab, MAX_BANK) if args.expected_vab else None
    if expected is not None and (not expected or expected[:4] != b'pBAV'):
        parser.error('Expected VAB must contain a standard pBAV header')
    candidates, files = [], []
    for path in project_files(args.project):
        found, file_record = scan_file(path)
        files.append(file_record)
        for record, bank in found:
            if expected is not None:
                record['matches_expected'] = bool('error' not in record and record['kind'] == 'VAB'
                    and bank == expected and record['program'] == args.program
                    and record['tone'] == args.tone and abs(record['gain_percent'] - args.gain) <= 1e-9)
            candidates.append(record)
    matches = [n for n, record in enumerate(candidates) if record.get('matches_expected')]
    conflicts = [n for n, record in enumerate(candidates) if expected is not None and not record.get('matches_expected')]
    passed = bool(matches) if expected is not None else bool(candidates) and all('error' not in record for record in candidates)
    report = {'project': str(args.project), 'mode': 'expected-vab' if expected is not None else 'inspect-only',
              'files': files, 'matching_au_candidates': candidates,
              'expected': {'file': str(args.expected_vab), 'bytes': len(expected), 'sha256': sha(expected),
                           'program': args.program, 'tone': args.tone, 'gain_percent': args.gain} if expected is not None else None,
              'expected_candidate_indices': matches, 'conflicting_candidate_indices': conflicts,
              'passed': passed, 'active_selection_proven': False,
              'scope': 'Observed AU XML fragments and exact embedded bytes only; not a GarageBand container decoder or active-track binding. Multiple/history candidates are reported, never silently selected. GUI reopen and audio export verify the active instrument separately.'}
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f'GarageBand state inspection failed: {error}')
