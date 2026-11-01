#!/usr/bin/env python3
"""Load packaged candidates in disposable hosts; AU installation is CI-only."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def run(*argv):
    subprocess.run([str(a) for a in argv], cwd=ROOT, check=True)


def windows(stage):
    common = ['-G', 'Visual Studio 17 2022', '-A', 'x64',
              '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded',
              '-DSMTG_USE_STATIC_CRT=ON']
    host = ROOT / 'build/windows-vst3-host'
    validator = ROOT / 'build/windows-vst3-validator'
    run('cmake', '-S', ROOT / 'tools/vst3-host', '-B', host, *common)
    run('cmake', '--build', host, '--config', 'Release',
        '--target', 'tetorica_vst3_audio_check', '--parallel', '4')
    run(host / 'Release/tetorica_vst3_audio_check.exe', stage / 'TetoricaSPU.vst3')
    run('cmake', '-S', ROOT / 'vendor/vst3sdk', '-B', validator, *common,
        '-DSMTG_ENABLE_VSTGUI_SUPPORT=OFF', '-DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF',
        '-DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF', '-DSMTG_CREATE_PLUGIN_LINK=OFF',
        '-DSMTG_RUN_VST_VALIDATOR=OFF')
    run('cmake', '--build', validator, '--config', 'Release',
        '--target', 'validator', '--parallel', '4')
    run(validator / 'bin/Release/validator.exe', stage / 'TetoricaSPU.vst3')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--report', type=Path, required=True,
                        help='Report from package_preview build/verify --extract-to')
    parser.add_argument('--ci-auval', action='store_true')
    args = parser.parse_args()
    report = json.loads(args.report.read_text())
    stage = Path(report['staging_directory'])
    platform = report['platform']
    checker = 'check_macos_bundles.py' if platform == 'macos' else 'check_windows_bundles.py'
    run(sys.executable, ROOT / 'scripts' / checker, '--input', stage)
    if platform == 'windows':
        if sys.platform != 'win32':
            raise SystemExit('Run the Windows host checks on Windows')
        windows(stage)
    elif platform == 'macos':
        if sys.platform != 'darwin':
            raise SystemExit('Run macOS host checks on macOS')
        if args.ci_auval:
            if os.environ.get('GITHUB_ACTIONS') != 'true':
                raise SystemExit('AU installation is restricted to ephemeral GitHub runners')
            run(sys.executable, 'scripts/install_au.py', '--install',
                '--source', stage / 'TetoricaSPU.component')
            run(sys.executable, 'scripts/validate_au.py', '--refresh-ci-registry')
        run('sh', 'scripts/check_vst3_audio.sh', stage / 'TetoricaSPU.vst3')
        run('sh', 'scripts/validate_vst3.sh', stage / 'TetoricaSPU.vst3')
        run('sh', 'scripts/check_au_state.sh', stage / 'TetoricaSPU.component')
    else:
        raise SystemExit('Unknown preview platform')
    print('PASS packaged candidate host checks: ' + platform)


if __name__ == '__main__':
    main()
