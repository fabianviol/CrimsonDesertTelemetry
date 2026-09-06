# Preserved from research/light-source-tests at af5485b; original algorithms retained.
"""Read one exact indexed asset using CrimsonForge's existing readers.

Game files are opened read-only; outputs must use a fresh workspace path.
Unlike VFS preview, decompression errors are fatal, not returned as raw data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import xml.etree.ElementTree as ET


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--forge', type=Path, required=True)
    ap.add_argument('--deps', type=Path,
                    help='Optional isolated Python dependencies (e.g. lz4)')
    ap.add_argument('--index', type=Path, required=True)
    ap.add_argument('--path', required=True)
    ap.add_argument('--group', required=True)
    ap.add_argument('--xml-fragments', action='store_true',
                    help='Validate UTF-8 XML fragment files with multiple roots')
    ap.add_argument('--out', type=Path, required=True)
    args = ap.parse_args()
    outputs = [args.out, Path(str(args.out) + '.json'), Path(str(args.out) + '.log')]
    if any(p.exists() for p in outputs):
        ap.error('Output exists; use a fresh path.')
    catalog = json.loads(args.index.read_text(encoding='utf-8'))
    rows = [r for r in catalog['candidates']
            if r['group'] == args.group and r['path'].lower() == args.path.lower()]
    if len(rows) != 1:
        ap.error(f'Expected one exact indexed entry, found {len(rows)}.')
    row = dict(rows[0])
    row.pop('group')
    game_root = Path(catalog['packages']).resolve()
    archive = Path(row['paz_file']).resolve()
    if not archive.is_relative_to(game_root):
        ap.error('Archive path is outside the indexed package root.')
    if any(p.resolve().is_relative_to(game_root) for p in outputs):
        ap.error('Output inside game directory is prohibited.')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    if args.deps:
        sys.path.insert(0, str(args.deps.resolve()))
    sys.path.insert(0, str(args.forge.resolve()))
    from utils.logger import setup_logger
    setup_logger(log_level='WARNING', debug_mode=False, log_file=str(outputs[2]))
    from core.pamt_parser import PamtFileEntry
    from core.paz_reader import PazReader
    entry = PamtFileEntry(**row)
    size = entry.comp_size if entry.compressed else entry.orig_size
    if min(entry.offset, size, entry.orig_size) < 0 or max(size, entry.orig_size) > 16*1024*1024:
        ap.error('Invalid size or asset exceeds this bounded reader limit.')
    data = PazReader(str(archive.parent)).read(archive.name, entry.offset, size)
    raw_hash = hashlib.sha256(data).hexdigest()
    if entry.encrypted:
        from core.crypto_engine import decrypt
        data = decrypt(data, Path(entry.path).name)
    if entry.compressed:
        from core.compression_engine import decompress
        data = decompress(data, entry.orig_size, entry.compression_type)
    if len(data) != entry.orig_size:
        raise ValueError(f'Decoded length {len(data)} != expected {entry.orig_size}')
    validation = 'length only; unknown format'
    if entry.path.lower().endswith(('.xml', '_xml')):
        if args.xml_fragments:
            # Wrap only for validation; save the original decoded bytes unchanged.
            root = ET.fromstring('<ArchiveFragments>' + data.decode('utf-8-sig')
                                 + '</ArchiveFragments>')
            if not len(root):
                raise ValueError('XML fragment file contains no root elements')
            validation = f'XML fragments parsed, roots={len(root)}'
        else:
            root = ET.fromstring(data)
            validation = f'XML parsed, root={root.tag}'
    elif entry.path.lower().endswith('.prefab'):
        from core.prefab_parser import parse_prefab
        prefab = parse_prefab(data, entry.path)
        validation = f'prefab magic valid, {len(prefab.strings)} heuristic strings'
    with args.out.open('xb') as output:
        output.write(data)
    result = {'entry': row, 'stored_sha256': raw_hash,
              'decoded_sha256': hashlib.sha256(data).hexdigest(),
              'decoded_size': len(data), 'validation': validation,
              'lamp_identity_validated': False}
    with outputs[1].open('x', encoding='utf-8') as output:
        json.dump(result, output, indent=2)
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
