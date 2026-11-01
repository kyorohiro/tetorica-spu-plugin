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
MAX_STATE = MAX_BANK + 1024
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


def decode_raw_sample(source):
    if len(source) < 64:
        raise ValueError('Raw ADPCM source header is truncated')
    version, flags, count, name_bytes = struct.unpack_from('<4I', source)
    root, left, right = struct.unpack_from('<3d', source, 16)
    loop, packed, pitch, reserved1, reserved2, reserved3 = struct.unpack_from('<6I', source, 40)
    if version != 1 or flags & ~7 or reserved1 or reserved2 or reserved3 or not count or count % 16 or count > 520192 or name_bytes > 320 or pitch > 16383 or (not flags & 1 and loop) or len(source) != 64 + count + name_bytes + (448 if flags & 4 else 0):
        raise ValueError('Invalid Raw ADPCM header, flags, metadata or lengths')
    if not math.isfinite(root) or root <= 0 or any(not math.isfinite(value) or not -1 <= value <= 1 for value in (left, right)):
        raise ValueError('Invalid Raw ADPCM tuning or signed source volume')
    adpcm = source[64:64 + count]
    try:
        name = source[64 + count:64 + count + name_bytes].decode('utf-8', errors='strict')
    except UnicodeDecodeError as error:
        raise ValueError('Raw ADPCM name is not valid UTF-8') from error
    if '\0' in name:
        raise ValueError('Raw ADPCM name contains NUL')
    marker = None
    for at in range(0, count, 16):
        header, packet_flags = adpcm[at], adpcm[at + 1]
        if header >> 4 > 4 or header & 15 > 12 or packet_flags & ~7 or bool(packet_flags & 1) != (at + 16 == count):
            raise ValueError('Invalid Raw ADPCM packet header, flags or END position')
        if packet_flags & 4:
            if marker is not None:
                raise ValueError('Raw ADPCM has multiple loop markers')
            marker = at // 16 * 28
    looping = bool(flags & 1)
    if bool(adpcm[-15] & 2) != looping or (not looping and marker is not None) or (looping and (loop % 28 or loop >= count // 16 * 28)) or (marker is not None and marker != loop):
        raise ValueError('Raw ADPCM loop metadata disagrees with its packets')
    waveform = source[64 + count + name_bytes:] if flags & 4 else None
    if waveform is not None:
        if root != 44100 / 112 or not looping or loop != 0 or count != 64 or any(not math.isfinite(value) or not -1 <= value <= 1 for value in struct.unpack('<112f', waveform)):
            raise ValueError('Invalid Raw ADPCM authored waveform metadata')
    return {'name': name, 'name_bytes': name_bytes, 'adpcm_bytes': count, 'adpcm_sha256': sha(adpcm),
            'base_frequency_hz': root, 'volume_left': left, 'volume_right': right,
            'adsr1': packed & 65535, 'adsr2': packed >> 16, 'inspector_pitch': pitch,
            'reverb_send': bool(flags & 2), 'loop_start_samples': loop if looping else None,
            'native_loop_supported': not looping or bool(adpcm[loop // 28 * 16 + 1] & 2),
            'waveform_samples': 112 if waveform is not None else None,
            'waveform_sha256': sha(waveform) if waveform is not None else None,
            'waveform_consistency_checked': False if waveform is not None else None}


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
                    envelope={'enabled': False, 'live': True, 'adsr1': 0x000f, 'adsr2': 0x1fc0},
                    reverb={'preset': 0, 'enabled': False, 'use_tone_send': False,
                            'volume_left': .25, 'volume_right': .25},
                    noise={'enabled': False, 'clock': 48},
                    pmon={'enabled': False, 'waveform': 0, 'ratio': 1., 'depth': .5, 'decay_ms': 500.},
                    morph={'enabled': False, 'amount': 0., 'endpoints': 'default-sine-saw'},
                    bank_bytes=0, bank_sha256=None), b''
    if len(payload) < 40 or payload[:8] != b'TSPUST01':
        raise ValueError('Expected complete TSPUST01 custom state or legacy Gain')
    schema, total, kind, program, tone, size = struct.unpack_from('<6I', payload, 8)
    if schema not in (1, 2, 3, 4, 5, 6, 7, 8) or total != len(payload) or size > MAX_BANK or total != {1: 40, 2: 48, 3: 72, 4: 72, 5: 80, 6: 112, 7: 1024, 8: 1024}[schema] + size:
        raise ValueError('Invalid state schema, total or bank length')
    if kind not in ((0, 1, 2, 3) if schema >= 8 else (0, 1, 2) if schema >= 4 else (0, 1)) or program > 127 or tone > 15:
        raise ValueError('Invalid state kind or sparse selection')
    if kind == 0 and (size or program or tone):
        raise ValueError('Builtin state has an unexpected bank or selection')
    if kind == 1 and not size:
        raise ValueError('VAB state has no embedded bank')
    if kind == 2 and (size != 448 or program or tone):
        raise ValueError('PCM cycle must contain 112 samples and no VAB selection')
    if kind == 3 and (size < 64 or program or tone):
        raise ValueError('Raw ADPCM source has an invalid length or VAB selection')
    gain, = struct.unpack_from('<d', payload, 32 + size)
    if not math.isfinite(gain) or not 0 <= gain <= 100:
        raise ValueError('Invalid custom-state Gain')
    packed, flags = struct.unpack_from('<2I', payload, 40 + size) if schema >= 2 else (0x1fc0000f, 2)
    if flags & ~3 or packed & 0x20000000:
        raise ValueError('Unknown envelope flags or reserved custom ADSR2 bit13')
    preset, effects, left, right = struct.unpack_from('<2I2d', payload, 48 + size) if schema >= 3 else (0, 0, .25, .25)
    if preset > 7 or effects & ~3 or any(not math.isfinite(value) or not 0 <= value <= 1 for value in (left, right)):
        raise ValueError('Invalid reverb preset, flags or return volumes')
    generator, clock = struct.unpack_from('<2I', payload, 72 + size) if schema >= 5 else (0, 48)
    if generator & ~1 or clock > 63:
        raise ValueError('Invalid Noise flags or clock')
    pflags, waveform, ratio, depth, decay = struct.unpack_from('<2I3d', payload, 80 + size) if schema >= 6 else (0, 0, 1., .5, 500.)
    if pflags & ~1 or waveform > 3 or not math.isfinite(ratio) or not .01 <= ratio <= 16 or not math.isfinite(depth) or not 0 <= depth <= 1 or not math.isfinite(decay) or not 0 <= decay <= 5000:
        raise ValueError('Invalid PMON flags, waveform, ratio, depth or decay')
    morph = {'enabled': False, 'amount': 0., 'endpoints': 'default-sine-saw'}
    if schema >= 7:
        mflags, reserved, amount = struct.unpack_from('<2Id', payload, 112 + size)
        a, b = payload[128 + size:576 + size], payload[576 + size:1024 + size]
        if mflags & ~1 or reserved or not math.isfinite(amount) or not 0 <= amount <= 1:
            raise ValueError('Invalid Morph flags, reserved word or amount')
        if any(not math.isfinite(value) or not -1 <= value <= 1 for value in struct.unpack('<224f', a + b)):
            raise ValueError('Morph endpoints must contain finite PCM in [-1, 1]')
        morph = {'enabled': bool(mflags & 1), 'amount': amount, 'samples_per_endpoint': 112,
                 'a_sha256': sha(a), 'b_sha256': sha(b)}
    bank = payload[32:32 + size]
    if kind == 2 and any(not math.isfinite(value) or not -1 <= value <= 1 for value in struct.unpack('<112f', bank)):
        raise ValueError('PCM cycle samples must be finite in [-1, 1]')
    raw = decode_raw_sample(bank) if kind == 3 else None
    return dict(result, schema=schema, kind={0: 'Builtin', 1: 'VAB', 2: 'PcmCycle', 3: 'AdpcmSample'}[kind],
                program=program, tone=tone, gain_percent=gain,
                envelope={'enabled': bool(flags & 1), 'live': bool(flags & 2),
                          'adsr1': packed & 65535, 'adsr2': packed >> 16},
                reverb={'preset': preset, 'enabled': bool(effects & 1), 'use_tone_send': bool(effects & 2),
                        'volume_left': left, 'volume_right': right},
                noise={'enabled': bool(generator & 1), 'clock': clock},
                pmon={'enabled': bool(pflags & 1), 'waveform': waveform, 'ratio': ratio, 'depth': depth, 'decay_ms': decay},
                morph=morph,
                adpcm_sample=raw,
                bank_bytes=size if kind == 1 else 0, bank_sha256=sha(bank) if kind == 1 else None,
                pcm_samples=112 if kind == 2 else None, pcm_sha256=sha(bank) if kind == 2 else None), bank if kind == 1 else b''


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
              'scope': 'Observed AU XML fragments and exact embedded bytes only; not a GarageBand container decoder or active-track binding. Raw ADPCM packet structure and metadata are checked; optional PCM is not re-encoded to verify its ADPCM consistency. Multiple/history candidates are reported, never silently selected. GUI reopen and audio export verify the active instrument separately.'}
    print(json.dumps(report, indent=2))
    return 0 if passed else 1


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, ValueError) as error:
        raise SystemExit(f'GarageBand state inspection failed: {error}')
