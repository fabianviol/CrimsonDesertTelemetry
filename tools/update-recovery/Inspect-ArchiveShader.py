# Preserved from research/light-source-tests at af5485b; original algorithms retained.
"""Validate a local PASC/DXBC artifact and disassemble with the installed SDK.

Only local research artifacts are written. Tool output contains metadata, not IR.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    for flag in ('input', 'out', 'dxc'):
        ap.add_argument('--' + flag, type=Path, required=True)
    args = ap.parse_args()
    artifacts = Path(__file__).resolve().parents[2] / 'artifacts' / 'light-research'
    outputs = [Path(str(args.out) + suffix) for suffix in ('.dxbc', '.ll', '.json')]
    if any(p.exists() or not p.resolve().is_relative_to(artifacts) for p in outputs):
        ap.error('Use fresh output names inside artifacts/light-research')
    if args.input.stat().st_size > 2 * 1024 * 1024:
        ap.error('Input exceeds 2 MiB limit')
    data = args.input.read_bytes()
    offset = 0
    if data[:4] == b'PASC':
        if len(data) < 20 or struct.unpack_from('<I', data, 8)[0] != len(data):
            raise ValueError('Invalid PASC length')
        offset = struct.unpack_from('<I', data, 16)[0]
    b = data[offset:]
    if len(b) < 32 or b[:4] != b'DXBC':
        raise ValueError('No DXBC at declared offset')
    version, size, count = struct.unpack_from('<III', b, 20)
    if version != 1 or size != len(b) or not 1 <= count <= 128 or 32 + 4*count > size:
        raise ValueError('Invalid DXBC header')
    parts = []
    for i in range(count):
        pos = struct.unpack_from('<I', b, 32 + 4*i)[0]
        if pos < 32 + 4*count or pos + 8 > size:
            raise ValueError('Invalid chunk offset')
        tag, length = struct.unpack_from('<4sI', b, pos)
        if pos + 8 + length > size:
            raise ValueError('Chunk outside container')
        parts.append({'tag': tag.decode('ascii'), 'offset': pos, 'size': length})
    if not any(p['tag'] == 'DXIL' for p in parts):
        raise ValueError('Container has no DXIL part (e.g. root signature only)')
    with outputs[0].open('xb') as f:
        f.write(b)
    result = subprocess.run([str(args.dxc), '-dumpbin', str(outputs[0]),
                             '-Fc', str(outputs[1])], capture_output=True, timeout=30)
    if result.returncode or not outputs[1].exists():
        raise RuntimeError(f'DXC failed, exit {result.returncode}; no IR printed')
    listing = outputs[1].read_text(encoding='utf-8')
    functions = re.findall(r'define\s+void\s+@([^\s(]+)', listing)
    if not functions:
        raise ValueError('Disassembly contains no defined entry function')
    report = {'input': str(args.input), 'input_sha256': hashlib.sha256(data).hexdigest(),
              'dxbc_offset': offset, 'dxbc_sha256': hashlib.sha256(b).hexdigest(),
              'parts': parts, 'defined_functions': functions,
              'dxc': str(args.dxc), 'dxc_exit': result.returncode,
              'listing_sha256': hashlib.sha256(outputs[1].read_bytes()).hexdigest()}
    with outputs[2].open('x', encoding='utf-8') as f:
        json.dump(report, f, indent=2)
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
