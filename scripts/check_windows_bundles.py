#!/usr/bin/env python3
# SPDX-License-Identifier: BSD-3-Clause
"""Inspect Windows PE/font artifacts without executing plug-in code or a GUI.

Default: the distributable VST3. --include-standalone additionally inspects the
internal APP build; it does not authorize packaging that ASIO-enabled program.
Resource checks require native Windows and use data/resource-only module loads.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys

ROOT = Path(__file__).resolve().parents[1]
FONT = ROOT / 'plugin/resources/fonts/Roboto-Regular.ttf'
FONT_NAME = 'Roboto-Regular.ttf'
FONT_TYPE = 'TTF'
MAX_PE_BYTES = 512 * 1024 * 1024
MAX_RESOURCE_NAMES = 256


def select_font_resource_name(names):
    """Match pinned IPlugPaths.cpp LocateResource/EnumResNameProc exactly.

    Its resource-compiler convention includes an extra first/last quote around
    a name. It strips those characters unconditionally, compares lower case,
    and forwards the original enumerated name to LoadWinResource. In particular,
    an unquoted-only name must not pass this check: the UI cannot locate it.
    """
    matches = [name for name in names if isinstance(name, str)
               and name[1:-1].lower() == FONT_NAME.lower()]
    if len(matches) != 1:
        raise ValueError('Expected one font resource discoverable by pinned iPlug '
                         f'LocateResource, found {len(matches)}; {FONT_TYPE} names: '
                         + json.dumps(names, ensure_ascii=True))
    return matches[0]


def sha256_file(path):
    digest = hashlib.sha256()
    with path.open('rb') as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b''):
            digest.update(chunk)
    return digest.hexdigest()


def inspect_pe(path, expect_dll):
    """Bounded structural PE checks; this is not an executable loader."""
    path = Path(path)
    size = path.stat().st_size
    if not path.is_file() or not 64 <= size <= MAX_PE_BYTES:
        raise ValueError('PE file is missing, empty or exceeds 512 MiB')
    with path.open('rb') as source:
        dos = source.read(64)
        if dos[:2] != b'MZ':
            raise ValueError('Missing DOS MZ signature')
        offset, = struct.unpack_from('<I', dos, 60)
        if offset < 64 or offset > size - 24:
            raise ValueError('PE header offset is outside the file')
        source.seek(offset)
        header = source.read(24)
        if header[:4] != b'PE\0\0':
            raise ValueError('Missing PE signature')
        machine, sections = struct.unpack_from('<HH', header, 4)
        optional_size, characteristics = struct.unpack_from('<HH', header, 20)
        if machine != 0x8664:
            raise ValueError(f'Expected AMD64 machine 0x8664, got 0x{machine:04x}')
        if not sections or optional_size < 112 or offset + 24 + optional_size + sections * 40 > size:
            raise ValueError('Truncated optional header or section table')
        optional = source.read(optional_size)
        magic, = struct.unpack_from('<H', optional)
        if magic != 0x20b:
            raise ValueError('Expected PE32+ optional header')
        if not characteristics & 0x0002:
            raise ValueError('PE is not marked as an executable image')
        dll = bool(characteristics & 0x2000)
        if dll != expect_dll:
            raise ValueError('PE DLL flag does not match the expected APP/VST3 kind')
        subsystem, = struct.unpack_from('<H', optional, 68)
    return {'machine': 'AMD64', 'machine_value': machine, 'pe32_plus': True,
            'dll': dll, 'subsystem': subsystem, 'sections': sections}


def inspect_font_resource(path, expected):
    if os.name != 'nt':
        raise RuntimeError('Exact PE resource inspection requires native Windows')
    from ctypes import wintypes
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.LoadLibraryExW.argtypes = [wintypes.LPCWSTR, wintypes.HANDLE, wintypes.DWORD]
    kernel.LoadLibraryExW.restype = wintypes.HMODULE
    kernel.FindResourceW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR, wintypes.LPCWSTR]
    kernel.FindResourceW.restype = wintypes.HANDLE
    enum_callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HMODULE,
                                          wintypes.LPCWSTR, ctypes.c_void_p,
                                          ctypes.c_ssize_t)
    kernel.EnumResourceNamesW.argtypes = [wintypes.HMODULE, wintypes.LPCWSTR,
                                         enum_callback_type, ctypes.c_ssize_t]
    kernel.EnumResourceNamesW.restype = wintypes.BOOL
    kernel.SizeofResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.SizeofResource.restype = wintypes.DWORD
    kernel.LoadResource.argtypes = [wintypes.HMODULE, wintypes.HANDLE]
    kernel.LoadResource.restype = wintypes.HANDLE
    kernel.LockResource.argtypes = [wintypes.HANDLE]
    kernel.LockResource.restype = ctypes.c_void_p
    kernel.FreeLibrary.argtypes = [wintypes.HMODULE]
    kernel.FreeLibrary.restype = wintypes.BOOL
    # Neither flag resolves executable imports or calls the module's DllMain.
    # https://learn.microsoft.com/windows/win32/api/libloaderapi/nf-libloaderapi-loadlibraryexw
    flags = 0x00000002 | 0x00000020  # AS_DATAFILE | AS_IMAGE_RESOURCE
    module = kernel.LoadLibraryExW(str(path.resolve()), None, flags)
    if not module:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        names = []
        enum_errors = []

        @enum_callback_type
        def collect_name(_module, _type, name, _parameter):
            # MAKEINTRESOURCE values share the pointer argument. The framework
            # skips those IDs; keep them only as diagnostics, never dereference.
            if len(names) >= MAX_RESOURCE_NAMES:
                enum_errors.append('Resource-name count exceeds inspection bound')
                return False
            try:
                names.append(int(name or 0) if not name or name <= 0xffff
                             else ctypes.wstring_at(name))
            except (ValueError, OSError) as error:
                enum_errors.append(str(error))
                return False
            return True

        if not kernel.EnumResourceNamesW(module, FONT_TYPE, collect_name, 0):
            error_code = ctypes.get_last_error()
            raise ValueError(f'Cannot enumerate {FONT_TYPE} resources '
                             f'(WinError {error_code}); names: {names!r}; '
                             f'diagnostics: {enum_errors!r}')
        selected_name = select_font_resource_name(names)
        resource = kernel.FindResourceW(module, selected_name, FONT_TYPE)
        if not resource:
            raise ValueError(f'Enumerated {FONT_TYPE} resource {selected_name!r} '
                             f'cannot be loaded (WinError {ctypes.get_last_error()}); '
                             f'names: {names!r}')
        size = kernel.SizeofResource(module, resource)
        if size != len(expected):
            raise ValueError(f'Embedded Roboto size {size} differs from source {len(expected)}')
        loaded = kernel.LoadResource(module, resource)
        if not loaded:
            raise ctypes.WinError(ctypes.get_last_error())
        pointer = kernel.LockResource(loaded)
        if not pointer:
            raise ctypes.WinError(ctypes.get_last_error())
        data = ctypes.string_at(pointer, size)
        if data != expected:
            raise ValueError('Embedded Roboto bytes differ from the unchanged source font')
        return {'name': selected_name, 'requested_name': FONT_NAME,
                'enumerated_names': names, 'type': FONT_TYPE, 'bytes': size,
                'sha256': hashlib.sha256(data).hexdigest(), 'exact_source_match': True,
                'inspection': 'LoadLibraryExW AS_DATAFILE|AS_IMAGE_RESOURCE; '
                              'pinned iPlug LocateResource enumeration; FindResourceW'}
    finally:
        if not kernel.FreeLibrary(module):
            raise ctypes.WinError(ctypes.get_last_error())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--input', type=Path, default=ROOT / 'build/windows/out')
    parser.add_argument('--output', type=Path, help='Write the complete JSON report here')
    parser.add_argument('--include-standalone', action='store_true',
                        help='Also inspect the internal APP.exe; not an instruction to distribute it')
    args = parser.parse_args(argv)
    report = {'schema': 1, 'platform': sys.platform, 'input': str(args.input.resolve()),
              'scope': 'PE architecture and exact font resources only; no device/editor/DllMain execution',
              'standalone_included_for_internal_inspection': args.include_standalone,
              'artifacts': [], 'errors': [], 'passed': False}
    try:
        expected = FONT.read_bytes()
        if not expected or expected[:4] != b'\0\1\0\0':
            raise ValueError('Source Roboto font is not the expected nonempty TrueType font')
        report['source_font'] = {'path': str(FONT), 'bytes': len(expected),
                                 'sha256': hashlib.sha256(expected).hexdigest()}
        targets = [('VST3', Path('TetoricaSPU.vst3/Contents/x86_64-win/TetoricaSPU.vst3'), True)]
        if args.include_standalone:
            targets.append(('APP-internal-only', Path('TetoricaSPU.exe'), False))
        for kind, relative, dll in targets:
            path = args.input / relative
            artifact = {'kind': kind, 'path': relative.as_posix(), 'passed': False}
            report['artifacts'].append(artifact)
            try:
                artifact['pe'] = inspect_pe(path, dll)
                artifact['bytes'] = path.stat().st_size
                artifact['sha256'] = sha256_file(path)
                artifact['font'] = inspect_font_resource(path, expected)
                artifact['passed'] = True
            except (OSError, ValueError, RuntimeError) as error:
                artifact['error'] = str(error)
                report['errors'].append(f'{relative.as_posix()}: {error}')
        report['passed'] = bool(report['artifacts']) and not report['errors']
    except (OSError, ValueError, RuntimeError) as error:
        report['errors'].append(str(error))
    output = json.dumps(report, ensure_ascii=False, indent=2) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding='utf-8')
    sys.stdout.write(output)
    return 0 if report['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
