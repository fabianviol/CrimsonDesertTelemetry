"""Offline evidence only: check literal World Builder signatures against a PE file.

Reads third-party C++ as text; never imports/builds/executes it or opens a game
process. Reports all matches, not compatibility approval. Disk bytes may differ
from unpacked live code. JSON outputs are exclusive-create, never overwritten.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path


def sections(data):
    if data[:2] != b'MZ':
        raise ValueError('Not a PE image')
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('Invalid PE signature')
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optsize = struct.unpack_from('<H', data, pe + 20)[0]
    opt = pe + 24
    if struct.unpack_from('<H', data, opt)[0] != 0x20b:
        raise ValueError('Expected PE32+')
    image_base = struct.unpack_from('<Q', data, opt + 24)[0]
    result = []
    for i in range(count):
        pos = opt + optsize + i * 40
        vs, va, size, raw = struct.unpack_from('<IIII', data, pos + 8)
        flags = struct.unpack_from('<I', data, pos + 36)[0]
        if size and raw + size > len(data):
            raise ValueError('Section exceeds file')
        result.append(dict(name=data[pos:pos + 8].rstrip(b'\0').decode('ascii', 'replace'),
                           rva=va, size=size, raw=raw, virtual_size=vs, flags=flags))
    return image_base, result


def patterns(source):
    """Literal calls only, retaining source line and enclosing function as context."""
    calls = re.compile(r'\b(ResolveSig|FindPatternCount|FindPattern)\(\s*'
                       r'(?:"([^"\n]+)"\s*,\s*)?"([0-9A-Fa-f? ]+)"')
    functions = list(re.finditer(r'^static [^\n;={}]*?\b([A-Za-z_]\w*)\([^;\n]*\)\s*\{', source, re.M))
    out = []
    for match in calls.finditer(source):
        preceding = [f.group(1) for f in functions if f.start() < match.start()]
        out.append(dict(api=match.group(1), name=match.group(2) or (preceding[-1] if preceding else '?'),
                        line=source.count('\n', 0, match.start()) + 1,
                        pattern=match.group(3)))
    return out


def find_matches(data, table, pattern):
    tokens = pattern.split()
    if not tokens or all(t in ('?', '??') for t in tokens):
        raise ValueError('Empty/all-wildcard pattern')
    expression = b''.join(b'.' if t in ('?', '??') else re.escape(bytes([int(t, 16)])) for t in tokens)
    regex = re.compile(expression, re.S)
    # Search the longest fixed run in C-speed bytes.find, then verify the mask.
    # This avoids a full-image lookahead at every byte for every signature.
    runs = []
    for start, token in enumerate(tokens):
        if token in ('?', '??') or (start and tokens[start - 1] not in ('?', '??')):
            continue
        end = start
        while end < len(tokens) and tokens[end] not in ('?', '??'):
            end += 1
        runs.append((start, bytes(int(t, 16) for t in tokens[start:end])))
    anchor_offset, anchor = max(runs, key=lambda r: len(r[1]))
    hits = []
    for sec in table:
        if not sec['flags'] & 0x20000000:
            continue
        end = sec['raw'] + min(sec['size'], sec['virtual_size'])
        cursor = sec['raw']
        while True:
            hit = data.find(anchor, cursor, end)
            if hit < 0:
                break
            cursor = hit + 1
            candidate = hit - anchor_offset
            if candidate >= sec['raw'] and candidate + len(tokens) <= end and regex.match(data, candidate, end):
                hits.append(sec['rva'] + candidate - sec['raw'])
    return hits


def read_rva(data, table, rva, size):
    for sec in table:
        delta = rva - sec['rva']
        if 0 <= delta and delta + size <= min(sec['size'], sec['virtual_size']):
            return data[sec['raw'] + delta:sec['raw'] + delta + size]
    return None


def collector_vtables(data, table, image_base, method_rva):
    """Reproduce the documented 12-slot shape, including equality constraints."""
    found = []
    target = struct.pack('<Q', image_base + method_rva)
    for sec in table:
        if sec['flags'] & 0x20000000 or not sec['flags'] & 0x40000000:
            continue
        body = data[sec['raw']:sec['raw'] + min(sec['size'], sec['virtual_size'])]
        for hit in re.finditer(re.escape(target), body):
            if hit.start() < 40 or hit.start() % 8:
                continue
            rva = sec['rva'] + hit.start() - 40
            raw = read_rva(data, table, rva, 96)
            if raw is None:
                continue
            f = struct.unpack('<12Q', raw)
            executable = all(any(s['flags'] & 0x20000000 and
                                 s['rva'] <= p - image_base < s['rva'] + s['virtual_size']
                                 for s in table) for p in f)
            if executable and f[0] == f[9] and f[3] == f[7] and f[6] == f[8] and f[5] == image_base + method_rva:
                found.append(rva)
    return found


def inspect(data, source):
    base, table = sections(data)
    evidence = []
    for item in patterns(source):
        hits = find_matches(data, table, item['pattern'])
        record = dict(item, count=len(hits), rvas=[hex(h) for h in hits])
        if item['name'] == 'ResolveProbeCollectorVtable' and len(hits) == 1:
            record['collector_vtables'] = [hex(r) for r in collector_vtables(data, table, base, hits[0])]
        if item['name'] == 'WorldGlobalRef':
            record['referenced_globals'] = sorted({hex(h + 7 + struct.unpack('<i', read_rva(data, table, h + 3, 4))[0]) for h in hits})
        evidence.append(record)
    return dict(scope='Offline literal-signature matches only; not live validation or permission to call.',
                exe_sha256=hashlib.sha256(data).hexdigest(),
                image_base=hex(base), patterns=evidence)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Refusing to overwrite existing output')
    source_bytes = args.source.read_bytes()
    report = inspect(args.exe.read_bytes(), source_bytes.decode('utf-8-sig'))
    report.update(exe=str(args.exe.resolve()), source=str(args.source.resolve()),
                  source_sha256=hashlib.sha256(source_bytes).hexdigest())
    with args.out.open('x', encoding='utf-8') as stream:
        json.dump(report, stream, indent=2)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
