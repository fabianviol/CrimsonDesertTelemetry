"""Name the dispatches in a pixtool export that can reach a given resource.

A dispatch reaches a resource through a descriptor table whose base descriptor
views it. This walks `CommandLists_*.cpp` in order, tracking the current pipeline
state, and reports every `Dispatch` preceded by a table binding that starts at one
of the resource's descriptors. Names come from `Map-PixExportShaders.py`.

"Can reach" is the honest claim. A table covers a range from its base, and this
shows the resource sits at the base of a table the dispatch bound -- not that the
shader wrote that particular region.

  python scripts/Find-PixExportDispatches.py EXPORT_DIR --resource 15739 --map map.csv
  python scripts/Find-PixExportDispatches.py EXPORT_DIR --resource 15739 --view uav
"""
import argparse
import csv
import glob
import os
import re
import sys

VIEWS = {'uav': 'CreateUnorderedAccessView', 'srv': 'CreateShaderResourceView',
         'cbv': 'CreateConstantBufferView', 'any': 'Create[A-Za-z]*View'}
TABLE = re.compile(r'Set(?:Compute|Graphics)RootDescriptorTable\(\d+,\s*'
                   r'GetGpuDescriptor\(g_descriptorHeap_\d+\.Get\(\),\s*(\d+)\)')
PIPELINE = re.compile(r'SetPipelineState\(GetPipelineState\((\d+)\)')
DISPATCH = re.compile(r'->(Dispatch|DispatchMesh|DispatchRays|ExecuteIndirect)\(')


def descriptor_slots(export_dir, resource, view):
    """Heap slots whose view is created over this resource.

    The generated calls put the resource before the descriptor for UAVs and SRVs
    (`CreateUnorderedAccessView_Buffer(GetResource(N)..., GetCpuDescriptor(...))`)
    and after it for CBVs, so both orders have to be accepted.
    """
    creator = VIEWS.get(view, VIEWS['any'])
    descriptor = r'GetCpuDescriptor\(g_descriptorHeap_\d+\.Get\(\),\s*(\d+)\)'
    pattern = re.compile(
        creator + r'[A-Za-z0-9_]*\((?:' +
        descriptor + r'[^;]*?GetResource\(' + str(resource) + r'\)' +
        r'|' +
        r'GetResource\(' + str(resource) + r'\)[^;]*?' + descriptor +
        r')')
    slots = set()
    for path in sorted(glob.glob(os.path.join(export_dir, '*.cpp'))):
        base = os.path.basename(path)
        if not (base.startswith('Descriptors_') or base.startswith('ModifyDescriptors_')):
            continue
        with open(path, encoding='utf-8', errors='replace') as stream:
            for match in pattern.finditer(stream.read()):
                slots.add(int(match.group(1) or match.group(2)))
    return slots


def load_names(path):
    names = {}
    if not path or not os.path.exists(path):
        return names
    with open(path, encoding='utf-8') as stream:
        for row in csv.DictReader(stream):
            names.setdefault(int(row['pso']), []).append(row['entry'])
    return names


def dispatches(export_dir, slots):
    """(file, pipeline state, kind) for every dispatch that bound one of these slots."""
    for path in sorted(glob.glob(os.path.join(export_dir, 'CommandLists_*.cpp'))):
        pipeline, armed = None, False
        with open(path, encoding='utf-8', errors='replace') as stream:
            for line in stream:
                found = PIPELINE.search(line)
                if found:
                    pipeline, armed = int(found.group(1)), False
                table = TABLE.search(line)
                if table and int(table.group(1)) in slots:
                    armed = True
                call = DISPATCH.search(line)
                if armed and call:
                    yield os.path.basename(path), pipeline, call.group(1)
                    armed = False


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('export_dir')
    parser.add_argument('--resource', type=int, required=True)
    parser.add_argument('--view', default='uav', choices=sorted(VIEWS))
    parser.add_argument('--map', help='CSV from Map-PixExportShaders.py, for names')
    arguments = parser.parse_args()

    slots = descriptor_slots(arguments.export_dir, arguments.resource, arguments.view)
    print('%s descriptors over resource %d: %d' % (arguments.view.upper(),
                                                   arguments.resource, len(slots)))
    if not slots:
        return 0
    names = load_names(arguments.map)
    seen = set()
    for source, pipeline, kind in dispatches(arguments.export_dir, slots):
        key = (pipeline, kind)
        if key in seen:
            continue
        seen.add(key)
        entry = ', '.join(names.get(pipeline, [])) or '<unnamed pipeline state>'
        print('  %-22s %-14s pso %-8s %s' % (source, kind, pipeline, entry))
    if not seen:
        print('  no dispatch bound a table starting at one of them')
    return 0


if __name__ == '__main__':
    sys.exit(main())
