"""Compare the scanner-visible surface of two PE files.

Written to answer "what in this build makes an antivirus engine flag it", where
the useful answer names one characteristic rather than one feature. Generic and
ML verdicts key off what a file looks like from the outside: which APIs it
imports, how its sections are laid out, how compressed they look, and which
strings sit in it. Diffing that surface between a build that scans clean and one
that does not narrows the search far faster than removing features one at a time.

Pure standard library, read-only, no uploads.

  python scripts/Compare-PeSurface.py CLEAN.asi FLAGGED.asi
  python scripts/Compare-PeSurface.py CLEAN.asi FLAGGED.asi --strings
"""
import argparse
import collections
import math
import os
import re
import struct
import sys

# Imports that generic engines weight heavily for injector/loader behaviour.
# Presence is not guilt -- this plugin legitimately hooks a process it is loaded
# into -- but a NEW one between two builds is a strong lead.
NOTABLE = {
    'VirtualAlloc', 'VirtualAllocEx', 'VirtualProtect', 'VirtualProtectEx',
    'WriteProcessMemory', 'ReadProcessMemory', 'CreateRemoteThread',
    'CreateRemoteThreadEx', 'OpenProcess', 'NtOpenProcess', 'SetWindowsHookExW',
    'SetWindowsHookExA', 'LoadLibraryA', 'LoadLibraryW', 'LoadLibraryExW',
    'GetProcAddress', 'IsDebuggerPresent', 'CheckRemoteDebuggerPresent',
    'NtQueryInformationProcess', 'OutputDebugStringA', 'OutputDebugStringW',
    'DebugActiveProcess', 'SuspendThread', 'ResumeThread', 'SetThreadContext',
    'GetThreadContext', 'QueueUserAPC', 'CreateFileMappingW', 'MapViewOfFile',
    'FlushInstructionCache', 'GetAsyncKeyState', 'AdjustTokenPrivileges',
    'CreateToolhelp32Snapshot', 'Module32FirstW', 'Process32FirstW',
    'WinHttpOpen', 'WinHttpConnect', 'WinHttpSendRequest', 'URLDownloadToFileW',
}


def entropy(data):
    if not data:
        return 0.0
    counts = collections.Counter(data)
    length = len(data)
    return -sum((n / length) * math.log2(n / length) for n in counts.values())


class Image:
    def __init__(self, path):
        self.path = path
        with open(path, 'rb') as stream:
            self.data = stream.read()
        data = self.data
        self.lfanew = struct.unpack_from('<I', data, 0x3C)[0]
        if data[self.lfanew:self.lfanew + 4] != b'PE\0\0':
            raise SystemExit('%s is not a PE image' % path)
        self.section_count = struct.unpack_from('<H', data, self.lfanew + 6)[0]
        self.optional_size = struct.unpack_from('<H', data, self.lfanew + 20)[0]
        self.magic = struct.unpack_from('<H', data, self.lfanew + 24)[0]
        self.plus = self.magic == 0x20B
        self.sections = self._sections()
        self.directories = self._directories()

    def _sections(self):
        base = self.lfanew + 24 + self.optional_size
        out = []
        for index in range(self.section_count):
            offset = base + index * 40
            name = self.data[offset:offset + 8].rstrip(b'\0').decode('ascii', 'replace')
            vsize, vaddr, rawsize, rawptr = struct.unpack_from('<IIII', self.data, offset + 8)
            chars = struct.unpack_from('<I', self.data, offset + 36)[0]
            body = self.data[rawptr:rawptr + rawsize]
            out.append({'name': name, 'vaddr': vaddr, 'vsize': vsize, 'rawsize': rawsize,
                        'rawptr': rawptr, 'chars': chars, 'entropy': entropy(body)})
        return out

    def _directories(self):
        # Data directory sits after the optional header's fixed part.
        offset = self.lfanew + 24 + (112 if self.plus else 96)
        count = struct.unpack_from('<I', self.data, self.lfanew + 24 + (108 if self.plus else 92))[0]
        out = []
        for index in range(min(count, 16)):
            rva, size = struct.unpack_from('<II', self.data, offset + index * 8)
            out.append((rva, size))
        return out

    def offset_of(self, rva):
        for section in self.sections:
            if section['vaddr'] <= rva < section['vaddr'] + max(section['vsize'], section['rawsize']):
                return section['rawptr'] + (rva - section['vaddr'])
        return None

    def cstring(self, rva):
        offset = self.offset_of(rva)
        if offset is None:
            return ''
        end = self.data.find(b'\0', offset)
        return self.data[offset:end].decode('ascii', 'replace')

    def imports(self):
        """{dll: set(function names)} from the import directory."""
        result = {}
        if len(self.directories) < 2:
            return result
        rva, _ = self.directories[1]
        table = self.offset_of(rva)
        if table is None:
            return result
        index = 0
        while True:
            entry = table + index * 20
            fields = struct.unpack_from('<IIIII', self.data, entry)
            lookup, _, _, name_rva, first_thunk = fields
            if not any(fields):
                break
            dll = self.cstring(name_rva).lower()
            names = set()
            thunk_rva = lookup or first_thunk
            thunk = self.offset_of(thunk_rva)
            if thunk is not None:
                step = 8 if self.plus else 4
                slot = 0
                while True:
                    raw = struct.unpack_from('<Q' if self.plus else '<I', self.data,
                                             thunk + slot * step)[0]
                    if not raw:
                        break
                    ordinal_flag = 1 << (63 if self.plus else 31)
                    if raw & ordinal_flag:
                        names.add('#%d' % (raw & 0xFFFF))
                    else:
                        names.add(self.cstring(int(raw) + 2))
                    slot += 1
            result.setdefault(dll, set()).update(names)
            index += 1
        return result


def ascii_strings(data, minimum=6):
    return set(m.group().decode('ascii')
               for m in re.finditer(rb'[\x20-\x7e]{%d,}' % minimum, data))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('clean', help='the build that scans clean')
    parser.add_argument('flagged', help='the build that is flagged')
    parser.add_argument('--strings', action='store_true',
                        help='also diff ASCII strings (noisy; use to hunt a specific lead)')
    parser.add_argument('--min-length', type=int, default=8)
    arguments = parser.parse_args()

    left, right = Image(arguments.clean), Image(arguments.flagged)
    print('clean   %s  %d bytes' % (os.path.basename(left.path), len(left.data)))
    print('flagged %s  %d bytes  (%+d)'
          % (os.path.basename(right.path), len(right.data), len(right.data) - len(left.data)))

    print('\n== sections ==')
    names = [s['name'] for s in left.sections] + \
            [s['name'] for s in right.sections if s['name'] not in
             [t['name'] for t in left.sections]]
    for name in names:
        a = next((s for s in left.sections if s['name'] == name), None)
        b = next((s for s in right.sections if s['name'] == name), None)
        def show(s):
            return ('vsize %9d  entropy %.3f' % (s['vsize'], s['entropy'])) if s else 'absent'
        mark = ''
        if a and b:
            if abs(a['entropy'] - b['entropy']) > 0.15:
                mark = '   <-- entropy moved'
            elif a['vsize'] != b['vsize']:
                mark = '   (size only)'
        else:
            mark = '   <-- section added/removed'
        print('  %-10s clean: %-34s flagged: %-34s%s' % (name, show(a), show(b), mark))

    li, ri = left.imports(), right.imports()
    print('\n== imported DLLs ==')
    for dll in sorted(set(li) | set(ri)):
        if dll not in li:
            print('  + %s   <-- NEW in the flagged build' % dll)
        elif dll not in ri:
            print('  - %s   (only in the clean build)' % dll)

    added, removed = {}, {}
    for dll in sorted(set(li) | set(ri)):
        new = ri.get(dll, set()) - li.get(dll, set())
        gone = li.get(dll, set()) - ri.get(dll, set())
        if new:
            added[dll] = new
        if gone:
            removed[dll] = gone

    print('\n== imported functions added in the flagged build ==')
    if not added:
        print('  none')
    for dll, names in added.items():
        flagged = sorted(n for n in names if n in NOTABLE)
        plain = sorted(n for n in names if n not in NOTABLE)
        print('  %s' % dll)
        for name in flagged:
            print('      %-36s <-- weighted by generic engines' % name)
        for name in plain:
            print('      %s' % name)

    print('\n== imported functions only in the clean build ==')
    if not removed:
        print('  none')
    for dll, names in removed.items():
        print('  %s: %s' % (dll, ', '.join(sorted(names))))

    print('\n== notable imports present in each ==')
    def notable(imports):
        return sorted({n for names in imports.values() for n in names if n in NOTABLE})
    print('  clean  : %s' % (', '.join(notable(li)) or 'none'))
    print('  flagged: %s' % (', '.join(notable(ri)) or 'none'))

    if arguments.strings:
        ls = ascii_strings(left.data, arguments.min_length)
        rs = ascii_strings(right.data, arguments.min_length)
        new = sorted(rs - ls)
        print('\n== %d strings only in the flagged build ==' % len(new))
        for value in new[:400]:
            print('  %s' % value)
        if len(new) > 400:
            print('  ... %d more' % (len(new) - 400))
    return 0


if __name__ == '__main__':
    sys.exit(main())
