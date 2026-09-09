"""Read a resource's captured bytes out of a pixtool export-to-cpp tree.

`export-to-cpp` writes every resource's contents into a single `resources.bin`
as a bare concatenation of XPRESS-compressed blocks. There is no index: the
blocks are consumed strictly in program order by `ResourceReader::Read(buffer,
compressedSize)`, so a block's file offset is the sum of every compressed size
read before it. This script reconstructs that order from the generated source
and decompresses one block, which makes the captured contents of any resource
readable without building or running the exported project.

The walk is self-checking. If the reconstructed order were wrong, XPRESS would
fail on the target block rather than return plausible bytes.

  python scripts/Read-PixExportResource.py EXPORT_DIR --list-cbv 1024
  python scripts/Read-PixExportResource.py EXPORT_DIR --cbv 15728 589824 --out FILE
  python scripts/Read-PixExportResource.py EXPORT_DIR --resource 15739 --out FILE
"""
import argparse
import ctypes
import ctypes.wintypes as wintypes
import glob
import os
import re
import sys

READ = re.compile(r'g_resourceReader->Read\(\s*\w+\s*,\s*(\d+)\s*\)')
FUNC = re.compile(r'^(?:void|static void)\s+(\w+)\s*\(\s*\)', re.M)
CALL = re.compile(r'^\s*(\w+)\(\);\s*$')
PLACED = re.compile(r'CreateAndTrackPlacedResource\((\d+),\s*g_device\.Get\(\),'
                    r'\s*&resourceDesc,\s*[^,]+,\s*nullptr,\s*(\d+),\s*(\d+)\)')
CBV = re.compile(r'CreateConstantBufferView\([^;]*?GetGpuva\((\d+),\s*(\d+)\),\s*(\d+)\)')

# The driver files, in the order CreateAppResources() calls them.
DRIVERS = ('FrameResources_000.cpp', 'FrameResources_001.cpp')


def _text(path):
    with open(path, encoding='utf-8', errors='replace') as stream:
        return stream.read()


def read_sequence(export_dir):
    """Every read from resources.bin, in program order, as (label, size)."""
    per_function = {}
    for path in sorted(glob.glob(os.path.join(export_dir, '*.cpp'))):
        if os.path.basename(path) in DRIVERS:
            continue
        text = _text(path)
        starts = [(m.start(), m.group(1)) for m in FUNC.finditer(text)]
        for index, (position, name) in enumerate(starts):
            end = starts[index + 1][0] if index + 1 < len(starts) else len(text)
            sizes = [int(m.group(1)) for m in READ.finditer(text, position, end)]
            if sizes:
                per_function[name] = sizes

    sequence = []
    for driver in DRIVERS:
        path = os.path.join(export_dir, driver)
        if not os.path.exists(path):
            continue
        for line in _text(path).splitlines():
            inline = READ.search(line)
            if inline:
                sequence.append((driver + ':inline', int(inline.group(1))))
                continue
            call = CALL.match(line)
            if call and call.group(1) in per_function:
                for size in per_function[call.group(1)]:
                    sequence.append((call.group(1), size))
    return sequence


def placed_resources(export_dir):
    """heap id -> sorted [(heap offset, resource id)]."""
    heaps = {}
    for path in sorted(glob.glob(os.path.join(export_dir, '*.cpp'))):
        for m in PLACED.finditer(_text(path)):
            resource, heap, offset = int(m.group(1)), int(m.group(2)), int(m.group(3))
            heaps.setdefault(heap, []).append((offset, resource))
    for entries in heaps.values():
        entries.sort()
    return heaps


def resolve_gpuva(export_dir, handle, offset):
    """A CBV names a heap for placed resources. Return (resource, inner offset)."""
    entries = placed_resources(export_dir).get(handle)
    if not entries:
        return handle, offset          # committed or reserved: the handle is the resource
    for index, (start, resource) in enumerate(entries):
        end = entries[index + 1][0] if index + 1 < len(entries) else None
        if start <= offset and (end is None or offset < end):
            return resource, offset - start
    return None, None


def list_constant_buffer_views(export_dir, size):
    found = {}
    for path in sorted(glob.glob(os.path.join(export_dir, '*.cpp'))):
        base = os.path.basename(path)
        if not (base.startswith('Descriptors_') or base.startswith('ModifyDescriptors_')):
            continue
        for m in CBV.finditer(_text(path)):
            if int(m.group(3)) != size:
                continue
            key = (int(m.group(1)), int(m.group(2)))
            found[key] = found.get(key, 0) + 1
    return sorted(found.items(), key=lambda kv: -kv[1])


def decompress_block(blob, offset, compressed_size):
    cabinet = ctypes.WinDLL('Cabinet.dll')
    handle = wintypes.HANDLE()
    if not cabinet.CreateDecompressor(3, None, ctypes.byref(handle)):   # 3 = XPRESS
        raise OSError('CreateDecompressor failed')
    try:
        with open(blob, 'rb') as stream:
            stream.seek(offset)
            source = stream.read(compressed_size)
        if len(source) != compressed_size:
            raise EOFError('resources.bin ended inside the block')
        needed = ctypes.c_size_t()
        cabinet.Decompress(handle, source, len(source), None, 0, ctypes.byref(needed))
        buffer = (ctypes.c_ubyte * needed.value)()
        written = ctypes.c_size_t()
        if not cabinet.Decompress(handle, source, len(source), buffer,
                                  needed.value, ctypes.byref(written)):
            raise OSError('Decompress failed; the reconstructed read order is wrong')
        return bytes(buffer)[:written.value]
    finally:
        cabinet.CloseDecompressor(handle)


def extract(export_dir, resource, inner=0, length=None):
    blob = os.path.join(export_dir, 'resources.bin')
    target = 'CreateAndInitResource_%d' % resource
    offset = 0
    for label, size in read_sequence(export_dir):
        if label == target:
            data = decompress_block(blob, offset, size)
            data = data[inner:inner + length] if length else data[inner:]
            return data, offset, size
        offset += size
    raise LookupError('resource %d has no initialising read in this export' % resource)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('export_dir', help='the cpp/ directory produced by export-to-cpp')
    parser.add_argument('--resource', type=int, help='ApiObjectId to extract')
    parser.add_argument('--cbv', nargs=2, type=int, metavar=('HANDLE', 'OFFSET'),
                        help='resolve a GetGpuva(handle, offset) pair and extract it')
    parser.add_argument('--list-cbv', type=int, metavar='SIZE',
                        help='list constant buffer views of exactly SIZE bytes')
    parser.add_argument('--out', help='write the bytes here')
    parser.add_argument('--length', type=int, help='truncate the output to this many bytes')
    arguments = parser.parse_args()

    if arguments.list_cbv:
        for (handle, offset), count in list_constant_buffer_views(arguments.export_dir,
                                                                  arguments.list_cbv):
            resource, inner = resolve_gpuva(arguments.export_dir, handle, offset)
            print('GetGpuva(%d, %d) x%-4d -> resource %s + %s'
                  % (handle, offset, count, resource, inner))
        return 0

    inner, resource = 0, arguments.resource
    if arguments.cbv:
        resource, inner = resolve_gpuva(arguments.export_dir, *arguments.cbv)
        if resource is None:
            sys.exit('no placed resource covers that heap offset')
        print('GetGpuva(%d, %d) -> resource %d + %d'
              % (arguments.cbv[0], arguments.cbv[1], resource, inner))
    if resource is None:
        sys.exit('give --resource, --cbv or --list-cbv')

    data, offset, size = extract(arguments.export_dir, resource, inner, arguments.length)
    print('resource %d: file offset %d, compressed %d, %d bytes recovered'
          % (resource, offset, size, len(data)))
    if arguments.out:
        with open(arguments.out, 'wb') as stream:
            stream.write(data)
        print('wrote %s' % arguments.out)
    return 0


if __name__ == '__main__':
    sys.exit(main())
