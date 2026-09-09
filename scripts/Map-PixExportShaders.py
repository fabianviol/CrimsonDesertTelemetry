"""Map compute PSO ApiObjectIds in a pixtool export to their shader entry names.

`CreatePSOs.cpp` reads each pipeline state's bytecode from `resources.bin`, so the
same read-order walk that recovers a resource recovers the DXIL. A DXIL container
keeps its entry point name as a readable string, which turns the export into a
PSO-to-shader index -- the piece needed before a dispatch in `CommandLists_*.cpp`
can be attributed to a shader.

A name that does not appear was not dispatched with a PSO created in the capture.

`--extract` writes one pipeline state's DXBC container out. That is the shader the
frame ACTUALLY ran, not a variant picked from the game's archives, so it settles
questions a variant cannot. Disassemble it with the DXC call in docs/TOOLING.md.

  python scripts/Map-PixExportShaders.py EXPORT_DIR
  python scripts/Map-PixExportShaders.py EXPORT_DIR --out map.csv
  python scripts/Map-PixExportShaders.py EXPORT_DIR --grep Ambient
  python scripts/Map-PixExportShaders.py EXPORT_DIR --extract 22274 --out pso.dxbc
"""
import argparse
import importlib.util
import os
import re
import sys

# This engine names entries either with a stage suffix (RenderDiffuseCS) or a
# lowercase stage prefix (csPrecomputeAmbient). Match both, or the second family --
# which is the whole atmospheric-scattering set -- is silently missed.
ENTRY = re.compile(
    rb'(?<![A-Za-z0-9_])('
    rb'[A-Za-z_][A-Za-z0-9_]{2,96}(?:CS|VS|PS|GS|HS|DS|MS|AS)'
    rb'|(?:cs|vs|ps|gs|hs|ds|ms|as)[A-Z][A-Za-z0-9_]{2,96}'
    rb')\x00')
STAGE = re.compile(r'Create(Compute|Graphics)PipelineState_(\d+)')


def _reader():
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(
        'pixreader', os.path.join(here, 'Read-PixExportResource.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def pipeline_blocks(reader, export_dir):
    """(kind, pso id, file offset, compressed size) for every PSO that reads bytecode."""
    offset = 0
    for label, size in reader.read_sequence(export_dir):
        match = STAGE.match(label)
        if match:
            yield match.group(1).lower(), int(match.group(2)), offset, size
        offset += size


BYTECODE = re.compile(r'cpsoDesc\.(?:CS|VS|PS|GS|HS|DS|MS|AS) = \{[^,]+,\s*(\d+)\s*\}')


def bytecode_length(export_dir, pso):
    """The declared bytecode size, so a container is not padded with the next stage."""
    path = os.path.join(export_dir, 'CreatePSOs.cpp')
    if not os.path.exists(path):
        return None
    with open(path, encoding='utf-8', errors='replace') as stream:
        text = stream.read()
    for kind in ('Compute', 'Graphics'):
        marker = 'void Create%sPipelineState_%d()' % (kind, pso)
        if marker not in text:
            continue
        start = text.index(marker)
        end = text.find('\nvoid Create', start + 1)
        found = BYTECODE.search(text, start, end if end > 0 else len(text))
        if found:
            return int(found.group(1))
    return None


def extract_bytecode(reader, export_dir, pso):
    """The DXBC container for one pipeline state, as the captured frame used it.

    This is the shader the frame ACTUALLY ran, not a variant chosen out of the
    game's archives, so it settles questions an archive variant cannot.
    """
    blob = os.path.join(export_dir, 'resources.bin')
    for _, found, offset, size in pipeline_blocks(reader, export_dir):
        if found != pso:
            continue
        data = reader.decompress_block(blob, offset, size)
        length = bytecode_length(export_dir, pso)
        return data[:length] if length else data
    raise LookupError('pipeline state %d has no bytecode read in this export' % pso)


def entry_names(data):
    """Entry point names inside a DXIL container, in order of appearance."""
    seen, out = set(), []
    for match in ENTRY.finditer(data):
        name = match.group(1).decode('ascii')
        if name not in seen:
            seen.add(name)
            out.append(name)
    return out


def build(reader, export_dir, progress=None):
    blob = os.path.join(export_dir, 'resources.bin')
    rows = []
    blocks = list(pipeline_blocks(reader, export_dir))
    for index, (kind, pso, offset, size) in enumerate(blocks):
        if progress and index % 25 == 0:
            print('  %d / %d' % (index, len(blocks)), file=sys.stderr)
        try:
            data = reader.decompress_block(blob, offset, size)
        except Exception as error:                      # a block we cannot read is not fatal
            rows.append((kind, pso, ['<unreadable: %s>' % error]))
            continue
        rows.append((kind, pso, entry_names(data)))
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('export_dir')
    parser.add_argument('--out', help='write the map as CSV here')
    parser.add_argument('--grep', help='only print rows whose names contain this substring')
    parser.add_argument('--extract', type=int, metavar='PSO',
                        help="write this pipeline state's DXBC container to --out")
    arguments = parser.parse_args()

    reader = _reader()

    if arguments.extract is not None:
        if not arguments.out:
            sys.exit('--extract needs --out')
        data = extract_bytecode(reader, arguments.export_dir, arguments.extract)
        with open(arguments.out, 'wb') as stream:
            stream.write(data)
        print('pipeline state %d: %d bytes (%r) -> %s'
              % (arguments.extract, len(data), data[:4], arguments.out))
        for name in entry_names(data) or ['<no entry name found>']:
            print('  %s' % name)
        return 0

    rows = build(reader, arguments.export_dir, progress=True)

    if arguments.out:
        with open(arguments.out, 'w', encoding='utf-8') as stream:
            stream.write('kind,pso,entry\n')
            for kind, pso, names in rows:
                for name in names or ['<none>']:
                    stream.write('%s,%d,%s\n' % (kind, pso, name))
        print('wrote %s (%d pipeline states)' % (arguments.out, len(rows)))

    for kind, pso, names in rows:
        joined = ', '.join(names) or '<no entry name found>'
        if arguments.grep and arguments.grep.lower() not in joined.lower():
            continue
        print('%-8s %-8d %s' % (kind, pso, joined))
    return 0


if __name__ == '__main__':
    sys.exit(main())
