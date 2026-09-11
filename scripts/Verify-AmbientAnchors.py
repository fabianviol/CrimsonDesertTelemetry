"""Check the ambient hook anchors in ambient_probe.h against a game executable.

The two ambient hook RVAs are exact-executable addresses held as constants in
`native/CrimsonDesertTelemetry.Asi/src/ambient_probe.h`. They are NOT generated
from a build definition, so a game update silently invalidates them: the preflight
then fails and `/v1/ambient` reports unavailable while the light path still works.

This reads the constants out of the header, so the check cannot drift from the code,
and verifies every anchor `CheckAmbientPreflight` tests:

  * the two hook signatures, at AmbientHookRvas
  * `Dispatch(1,1,1)` (FF 90 28 03 00 00) six bytes before each hook
  * the two `[sky+0x98]` source-field loads, at AmbientSourceRvas

When an anchor misses it searches the image for the signature and reports where it
moved, so a whole-region shift shows up as one delta shared by both paths. A
signature found more than once is NOT a relocation candidate — say so and stop.

  python scripts/Verify-AmbientAnchors.py "C:/.../bin64/CrimsonDesert.exe"

Read-only. It never writes, patches or launches anything.
"""
import argparse
import os
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
HEADER = os.path.join(HERE, os.pardir, 'native', 'CrimsonDesertTelemetry.Asi',
                      'src', 'ambient_probe.h')
IMAGE_SCN_MEM_EXECUTE = 0x20000000

# Anchors the preflight tests that are literals in render_capture.cpp rather than
# named constants, because they are the same for both paths.
DISPATCH = bytes([0xFF, 0x90, 0x28, 0x03, 0x00, 0x00])


def parse_header(path):
    """The RVA arrays and byte signatures, straight out of the header."""
    text = open(path, encoding='utf-8').read()

    def rvas(name):
        match = re.search(r'%s\{([^}]*)\}' % name, text)
        if not match:
            raise SystemExit('%s not found in %s' % (name, path))
        return [int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', match.group(1))]

    def signature(name):
        match = re.search(r'%s\{([^}]*)\}' % name, text, re.S)
        if not match:
            raise SystemExit('%s not found in %s' % (name, path))
        return bytes(int(v, 16) for v in re.findall(r'0x[0-9A-Fa-f]+', match.group(1)))

    return (rvas('AmbientHookRvas'), rvas('AmbientSourceRvas'),
            [signature('AmbientSignatureA'), signature('AmbientSignatureB')])


def sections(data):
    e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
    count = struct.unpack_from('<H', data, e_lfanew + 6)[0]
    optional = struct.unpack_from('<H', data, e_lfanew + 20)[0]
    base = e_lfanew + 24 + optional
    out = []
    for index in range(count):
        offset = base + index * 40
        name = data[offset:offset + 8].rstrip(b'\0').decode('ascii', 'replace')
        virtual_size, virtual_address, raw_size, raw_pointer = struct.unpack_from(
            '<IIII', data, offset + 8)
        characteristics = struct.unpack_from('<I', data, offset + 36)[0]
        out.append((name, virtual_address, virtual_size, raw_pointer, raw_size,
                    bool(characteristics & IMAGE_SCN_MEM_EXECUTE)))
    return out


def read_rva(data, table, rva, count):
    """Bytes at an RVA, and whether the section holding it is executable."""
    for _, virtual_address, virtual_size, raw_pointer, _, executable in table:
        if virtual_address <= rva < virtual_address + virtual_size:
            start = raw_pointer + (rva - virtual_address)
            return data[start:start + count], executable
    return None, False


def find_rvas(data, table, signature):
    found, start = [], 0
    while True:
        offset = data.find(signature, start)
        if offset < 0:
            return found
        start = offset + 1
        for _, virtual_address, _, raw_pointer, raw_size, executable in table:
            if raw_pointer <= offset < raw_pointer + raw_size:
                found.append((virtual_address + (offset - raw_pointer), executable))
                break


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('executable', help='the game executable to check')
    parser.add_argument('--header', default=HEADER)
    arguments = parser.parse_args()

    hooks, sources, signatures = parse_header(arguments.header)
    with open(arguments.executable, 'rb') as stream:
        data = stream.read()
    table = sections(data)

    print('header   %s' % os.path.normpath(arguments.header))
    print('image    %s (%d bytes)' % (arguments.executable, len(data)))
    print()

    anchors = []
    for path in (0, 1):
        anchors.append(('hook %d signature' % path, hooks[path], signatures[path]))
        anchors.append(('hook %d dispatch' % path, hooks[path] - 6, DISPATCH))
    # Both source loads are read from the header; their expected bytes differ only
    # in the destination register, so take them from the image's own preflight pair.
    for path, expected in enumerate((bytes([0x48, 0x8B, 0xAF, 0x98, 0, 0, 0]),
                                     bytes([0x48, 0x8B, 0x9D, 0x98, 0, 0, 0]))):
        anchors.append(('path %d source load' % path, sources[path], expected))

    misses = 0
    for label, rva, expected in anchors:
        found, executable = read_rva(data, table, rva, len(expected))
        if found == expected and executable:
            print('  MATCH  %-20s rva 0x%07X' % (label, rva))
            continue
        misses += 1
        print('  MISS   %-20s rva 0x%07X' % (label, rva))
        print('           expected %s' % expected.hex(' '))
        print('           found    %s%s' % (found.hex(' ') if found else 'unmapped',
                                            '' if executable else '  (not executable)'))

    if not misses:
        print('\nAll ambient anchors match. The preflight can pass on this executable.')
        return 0

    print('\n%d anchor(s) miss. Searching the image for the hook signatures:' % misses)
    relocations = []
    for path, signature in enumerate(signatures):
        hits = find_rvas(data, table, signature)
        if len(hits) != 1:
            print('  signature %d: %d occurrences — NOT a relocation candidate; '
                  'a unique anchor is required.' % (path, len(hits)))
            relocations.append(None)
            continue
        rva, executable = hits[0]
        delta = rva - hooks[path]
        print('  signature %d: unique at rva 0x%07X (%s), delta %+#x'
              % (path, rva, 'executable' if executable else 'NOT EXECUTABLE', delta))
        relocations.append(delta if executable else None)

    if relocations and all(d is not None for d in relocations) and len(set(relocations)) == 1:
        delta = relocations[0]
        print('\nBoth paths moved by the same %+#x. Candidate constants:' % delta)
        print('  AmbientHookRvas{%s};'
              % ', '.join('0x%07X' % (rva + delta) for rva in hooks))
        print('  AmbientSourceRvas{%s};'
              % ', '.join('0x%07X' % (rva + delta) for rva in sources))
        print('Re-run this check after editing the header; it must report all MATCH.')
        print('Also update SkyAmbientReader.SkyProducerRva to the new AmbientHookRvas[0].')
    else:
        print('\nNo single shared shift. This is not a plain relocation; disassemble '
              'before hooking anything.')
    return 1


if __name__ == '__main__':
    sys.exit(main())
