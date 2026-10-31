#!/usr/bin/env python3
"""Compare the two locally exported PCM16 GarageBand verification files."""
import argparse
import array
import hashlib
import json
import math
from pathlib import Path
import sys
import wave


def read(path):
    with wave.open(str(path), 'rb') as wav:
        fmt = {'sample_rate': wav.getframerate(), 'channels': wav.getnchannels(),
               'sample_bytes': wav.getsampwidth(), 'frames': wav.getnframes()}
        if fmt['channels'] != 2 or fmt['sample_bytes'] != 2:
            raise SystemExit('Expected uncompressed stereo PCM16 WAVE')
        samples = array.array('h', wav.readframes(fmt['frames']))
    if sys.byteorder != 'little':
        samples.byteswap()
    if not samples:
        raise SystemExit('Export is empty')
    rms = math.sqrt(sum(float(x) * x for x in samples) / len(samples)) / 32768
    return samples, dict(fmt, file=path.name, sha256=hashlib.sha256(path.read_bytes()).hexdigest(),
                         peak=max(map(abs, samples)) / 32768, rms=rms)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('before', type=Path)
    parser.add_argument('after', type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    a, first = read(args.before)
    b, second = read(args.after)
    for key in ('sample_rate', 'channels', 'sample_bytes', 'frames'):
        if first[key] != second[key]:
            raise SystemExit(f'Export formats differ: {key}')
    if first['frames'] == 0 or min(first['rms'], second['rms']) < 0.001:
        raise SystemExit('Export is empty or effectively silent')
    difference = max(abs(x-y) for x, y in zip(a, b))
    if difference > 1:
        raise SystemExit(f'Export PCM differs by more than one PCM16 unit: {difference}')
    result = {'exports': [first, second], 'max_difference_pcm16': difference,
              'rms_difference': math.sqrt(sum(float(x-y)**2 for x, y in zip(a,b))/len(a))/32768,
              'passed': True,
              'scope': 'GarageBand exports include host effects and output processing; not a physical loopback or an absolute Gain calibration.'}
    text = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.write_text(text)
    print(text, end='')


if __name__ == '__main__':
    main()
