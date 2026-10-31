#!/usr/bin/env python3
"""Wait for AU discovery, then run auval once without hiding validation failures."""
import argparse
import getpass
import os
import re
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--refresh-ci-registry', action='store_true',
                        help='Restart this ephemeral GitHub runner user\'s registrar, not audio hosts')
    args = parser.parse_args()
    if args.refresh_ci_registry:
        if os.environ.get('GITHUB_ACTIONS') != 'true':
            raise SystemExit('Registrar refresh is restricted to GitHub Actions; no local service was changed')
        # Fresh runners may create Components for the first time after the
        # registrar started. Refresh only this disposable runner user's service.
        # Use normal SIGTERM, never kill audio hosts or alter security settings.
        result = subprocess.run(['/usr/bin/killall', '-u', getpass.getuser(),
                                 'AudioComponentRegistrar'], capture_output=True, text=True)
        if result.returncode not in (0, 1):
            raise SystemExit(f'Cannot refresh CI registrar: {result.stderr.strip()}')
        print(f'CI registrar refresh exit={result.returncode} (1 may mean not running)', flush=True)
    deadline = time.monotonic() + 30
    listing = None
    while time.monotonic() < deadline:
        try:
            listing = subprocess.run(['/usr/bin/auval', '-a'], capture_output=True,
                                     text=True, timeout=5)
        except subprocess.TimeoutExpired:
            listing = None
        if listing and listing.returncode == 0 and re.search(
                r'^\s*aumu\s+Tspu\s+Tetr\b', listing.stdout, re.MULTILINE):
            print('PASS: TetoricaSPU was discovered by the Audio Component service', flush=True)
            # Discovery retries do not turn a real API/render/MIDI failure green.
            raise SystemExit(subprocess.run(['/usr/bin/auval', '-v', 'aumu', 'Tspu', 'Tetr']).returncode)
        time.sleep(1)
    detail = 'listing timed out'
    if listing:
        detail = f'listing exit={listing.returncode}, stderr={listing.stderr.strip()[:2048]!r}'
    raise SystemExit(f'TetoricaSPU was not discovered within 30 seconds; auval validation was not run ({detail})')


if __name__ == '__main__':
    main()
