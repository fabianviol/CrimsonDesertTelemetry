# Preserved from research/light-source-tests at af5485b; original algorithms retained.
"""Read PAMT indexes with CrimsonForge; never write to game files.

Only candidate metadata is saved. No PAZ payloads are read at this stage.
Run with -B to avoid bytecode writes in the separate CrimsonForge checkout.
"""
import argparse
from collections import Counter
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import re
import sys


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--forge', type=Path, required=True)
    ap.add_argument('--packages', type=Path, required=True,
                    help='Directory containing numbered folders with 0.pamt')
    ap.add_argument('--out', type=Path, required=True)
    ap.add_argument('--group', action='append',
                    help='Only these four-digit package groups (repeatable)')
    ap.add_argument('--pattern', default=r'light|effect|emitter|lamp|torch|fire|flame|brazier|candle|shrine|altar|gimmick',
                    help='Case-insensitive candidate path regex')
    args = ap.parse_args()
    if args.out.exists() or args.out.with_suffix('.log').exists():
        ap.error('Output already exists; choose a fresh label.')
    if any(p.resolve().is_relative_to(args.packages.resolve())
           for p in (args.out, args.out.with_suffix('.log'))):
        ap.error('Output inside game package directory is prohibited.')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    sys.path.insert(0, str(args.forge.resolve()))
    from utils.logger import setup_logger
    setup_logger(log_level='WARNING', debug_mode=False,
                 log_file=str(args.out.with_suffix('.log').resolve()))
    from core.pamt_parser import parse_pamt

    if args.group and any(not re.fullmatch(r'[0-9]{4}', g) for g in args.group):
        ap.error('Group must be four digits.')
    pattern = re.compile(args.pattern, re.I)
    # Keep all matching names, even unknown extensions. Only index metadata,
    # never infer a file's type or a specific lamp's identity from its name.
    indexes = sorted(args.packages.glob('*/0.pamt'))
    if args.group:
        indexes = [p for p in indexes if p.parent.name in args.group]
        if {p.parent.name for p in indexes} != set(args.group):
            ap.error('A requested package group is missing.')
    groups, errors, candidates, controls = [], [], [], []
    extensions = Counter()
    for index in indexes:
        try:
            parsed = parse_pamt(str(index), paz_dir=str(index.parent))
            if not parsed.file_entries:
                raise ValueError('Empty index; cannot serve as a reader control')
            groups.append({'group': index.parent.name,
                           'entries': len(parsed.file_entries),
                           'sha256': hashlib.sha256(parsed.raw_data).hexdigest()})
            for entry in parsed.file_entries:
                if not pattern.search(entry.path):
                    continue
                row = {'group': index.parent.name, **asdict(entry)}
                candidates.append(row)
                extensions[Path(entry.path).suffix.lower()] += 1
                if entry.path.replace('\\', '/').lower().endswith('/lightpreset.xml'):
                    controls.append(row)
        except Exception as exc:
            errors.append({'group': index.parent.name, 'error': str(exc)})
        print(f'{index.parent.name}: parsed={len(groups)} failed={len(errors)} '
              f'candidates={len(candidates)}', flush=True)

    result = {'packages': str(args.packages.resolve()),
              'requested_groups': args.group, 'candidate_pattern': args.pattern,
              'index_count': len(indexes), 'groups': groups, 'errors': errors,
              'entry_count': sum(g['entries'] for g in groups),
              'candidate_count': len(candidates),
              'candidate_extensions': dict(extensions.most_common()),
              'known_name_controls': controls, 'candidates': candidates,
              'validity': {'all_discovered_indexes_parsed': bool(indexes) and not errors,
                           'known_lightpreset_name_found': bool(controls),
                           'payload_reader_validated': False,
                           'lamp_identity_validated': False}}
    with args.out.open('x', encoding='utf-8') as output:
        json.dump(result, output, ensure_ascii=False, indent=2)
    print(json.dumps({k: v for k, v in result.items()
                      if k not in ('candidates', 'groups')}, ensure_ascii=False, indent=2))
    return 0 if indexes and not errors else 2


if __name__ == '__main__':
    raise SystemExit(main())
