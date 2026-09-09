"""Resolve a shader register to the resource actually bound at a dispatch.

Knowing that a descriptor table can reach a resource is only a candidate. A real
binding needs the whole chain:

    shader register (u15, space39)
      -> root parameter and descriptor range in the PSO's root signature
      -> offset within that range
      -> descriptor heap index
      -> the descriptor AS IT STOOD at that point in the frame
      -> resource

The last step matters because this export rewrites descriptors mid-frame:
`RenderFrameWorker_000.cpp` interleaves `ModifyDescriptors_*()` calls, one
descriptor each, with `PopulateCommandList_*()` calls. Walking that file in order
gives a timeline, so a heap slot is resolved to whatever it held at the time
rather than to whatever it held last.

  python scripts/Resolve-PixExportBindings.py EXPORT_DIR --pso 22274 --register u5,space39
  python scripts/Resolve-PixExportBindings.py EXPORT_DIR --pso 22274 --all-uav
"""
import argparse
import glob
import os
import re
import sys

APPEND = 0xFFFFFFFF

RANGE = re.compile(
    r'descriptorRanges\[(\d+)\]\s*=\s*\{\s*'
    r'D3D12_DESCRIPTOR_RANGE_TYPE_(\w+),\s*'
    r'(\d+),\s*(\d+),\s*(\d+),\s*'
    r'[^,]+,\s*(\d+)\s*\}')
PARAM_TYPE = re.compile(r'rootParameters\[(\d+)\]\.ParameterType\s*=\s*D3D12_ROOT_PARAMETER_TYPE_(\w+)')
PARAM_TABLE = re.compile(r'rootParameters\[(\d+)\]\.DescriptorTable\s*=\s*\{\s*(\d+),')
PARAM_DESC = re.compile(r'rootParameters\[(\d+)\]\.Descriptor\s*=\s*\{\s*(\d+),\s*(\d+)')
ROOTSIG = re.compile(r'CreateAndTrackRootSignature\((\d+),')

PSO_ROOTSIG = re.compile(r'(?:cpso|gpso)Desc\.pRootSignature = GetRootSignature\((\d+)\)')

VIEW_WRITE = re.compile(
    r'Create(ShaderResourceView|UnorderedAccessView|ConstantBufferView|Sampler)[A-Za-z0-9_]*\('
    r'(?:GetResource\((\d+)\)[^;]*?)?'
    r'GetCpuDescriptor\(g_descriptorHeap_(\d+)\.Get\(\),\s*(\d+)\)'
    r'(?:[^;]*?GetResource\((\d+)\))?')

SET_ROOTSIG = re.compile(r'Set(?:Compute|Graphics)RootSignature\(GetRootSignature\((\d+)\)')
SET_TABLE = re.compile(r'Set(?:Compute|Graphics)RootDescriptorTable\((\d+),\s*'
                       r'GetGpuDescriptor\(g_descriptorHeap_(\d+)\.Get\(\),\s*(\d+)\)')
SET_PSO = re.compile(r'SetPipelineState\(GetPipelineState\((\d+)\)')
DISPATCH = re.compile(r'->(Dispatch|DispatchMesh|DispatchRays|ExecuteIndirect)\(')
FUNCTION = re.compile(r'^(?:void|static void)\s+(\w+)\s*\(\s*\)', re.M)
CALL = re.compile(r'^\s*(\w+)\(\);\s*$')

KIND = {'ShaderResourceView': 'SRV', 'UnorderedAccessView': 'UAV',
        'ConstantBufferView': 'CBV', 'Sampler': 'SAMPLER'}
REGISTER_KIND = {'t': 'SRV', 'u': 'UAV', 'b': 'CBV', 's': 'SAMPLER'}


def _text(path):
    with open(path, encoding='utf-8', errors='replace') as stream:
        return stream.read()


def _bodies(path):
    """name -> source text, for every zero-argument function in a file."""
    text = _text(path)
    starts = [(m.start(), m.group(1)) for m in FUNCTION.finditer(text)]
    out = {}
    for index, (position, name) in enumerate(starts):
        end = starts[index + 1][0] if index + 1 < len(starts) else len(text)
        out[name] = text[position:end]
    return out


def root_signatures(export_dir):
    """id -> {parameter index: {'type': ..., 'ranges': [...]}}."""
    signatures = {}
    for path in sorted(glob.glob(os.path.join(export_dir, 'FrameResources_*.cpp'))):
        text = _text(path)
        position = 0
        for created in ROOTSIG.finditer(text):
            block = text[position:created.end()]
            position = created.end()
            parameters = {}
            for match in PARAM_TYPE.finditer(block):
                parameters[int(match.group(1))] = {'type': match.group(2), 'ranges': []}
            # Ranges are written immediately before the DescriptorTable they belong to.
            for table in PARAM_TABLE.finditer(block):
                index = int(table.group(1))
                start = block.rfind('static D3D12_DESCRIPTOR_RANGE1', 0, table.start())
                if start < 0 or index not in parameters:
                    continue
                for entry in RANGE.finditer(block, start, table.start()):
                    parameters[index]['ranges'].append({
                        'kind': entry.group(2),
                        'count': int(entry.group(3)),
                        'base_register': int(entry.group(4)),
                        'space': int(entry.group(5)),
                        'offset': int(entry.group(6)),
                    })
            for descriptor in PARAM_DESC.finditer(block):
                index = int(descriptor.group(1))
                if index in parameters:
                    parameters[index]['register'] = int(descriptor.group(2))
                    parameters[index]['space'] = int(descriptor.group(3))
            signatures[int(created.group(1))] = parameters
    return signatures


def pso_root_signatures(export_dir):
    path = os.path.join(export_dir, 'CreatePSOs.cpp')
    if not os.path.exists(path):
        return {}
    out = {}
    for name, body in _bodies(path).items():
        match = re.search(r'PipelineState_(\d+)$', name)
        signature = PSO_ROOTSIG.search(body)
        if match and signature:
            out[int(match.group(1))] = int(signature.group(1))
    return out


def descriptor_writes(body):
    """(kind, heap, slot, resource) for each view creation in a block of source."""
    out = []
    for match in VIEW_WRITE.finditer(body):
        resource = match.group(2) or match.group(5)
        out.append((KIND[match.group(1)], int(match.group(3)), int(match.group(4)),
                    int(resource) if resource else None))
    return out


def range_offsets(ranges):
    """Resolve OFFSET_APPEND against the running end of the preceding ranges."""
    offsets, cursor = [], 0
    for entry in ranges:
        start = cursor if entry['offset'] == APPEND else entry['offset']
        offsets.append(start)
        cursor = start + entry['count']
    return offsets


def heap_index(parameters, kind, register, space):
    """(parameter index, offset within the table) for a shader register, or None."""
    for index, parameter in sorted(parameters.items()):
        if parameter['type'] != 'DESCRIPTOR_TABLE':
            continue
        for entry, start in zip(parameter['ranges'], range_offsets(parameter['ranges'])):
            if entry['kind'] != kind or entry['space'] != space:
                continue
            if entry['base_register'] <= register < entry['base_register'] + entry['count']:
                return index, start + (register - entry['base_register'])
    return None


def timeline(export_dir):
    """The frame in order: descriptor writes and command list bodies, interleaved."""
    modify = {}
    for path in sorted(glob.glob(os.path.join(export_dir, 'ModifyDescriptors_*.cpp'))):
        modify.update(_bodies(path))
    commands = {}
    for path in sorted(glob.glob(os.path.join(export_dir, 'CommandLists_*.cpp'))):
        commands.update(_bodies(path))

    events = []
    for path in sorted(glob.glob(os.path.join(export_dir, 'Descriptors_*.cpp'))):
        events.append(('descriptors', _text(path)))          # init state, before the frame
    for path in sorted(glob.glob(os.path.join(export_dir, 'RenderFrameWorker_*.cpp'))):
        for line in _text(path).splitlines():
            call = CALL.match(line)
            if not call:
                continue
            name = call.group(1)
            if name in modify:
                events.append(('descriptors', modify[name]))
            elif name in commands:
                events.append(('commands', commands[name]))
    return events


def resolve(export_dir, pso, registers):
    """Every dispatch of `pso`, with each requested register resolved."""
    signatures = root_signatures(export_dir)
    owners = pso_root_signatures(export_dir)
    parameters = signatures.get(owners.get(pso), {})
    if not parameters:
        raise LookupError('no root signature for pipeline state %d' % pso)

    wanted = []
    for kind, register, space in registers:
        found = heap_index(parameters, kind, register, space)
        wanted.append((kind, register, space, found))

    slots = {}                       # (heap, slot) -> resource, as of now
    tables = {}                      # root parameter -> (heap, base slot)
    current, results = None, []
    for event, body in timeline(export_dir):
        if event == 'descriptors':
            for kind, heap, slot, resource in descriptor_writes(body):
                slots[(heap, slot)] = resource
            continue
        for line in body.splitlines():
            signature = SET_ROOTSIG.search(line)
            if signature:
                tables = {}
            pipeline = SET_PSO.search(line)
            if pipeline:
                current = int(pipeline.group(1))
            table = SET_TABLE.search(line)
            if table:
                tables[int(table.group(1))] = (int(table.group(2)), int(table.group(3)))
            call = DISPATCH.search(line)
            if call and current == pso:
                row = []
                for kind, register, space, found in wanted:
                    name = '%s%d,space%d' % ({v: k for k, v in REGISTER_KIND.items()}[kind],
                                             register, space)
                    if not found:
                        row.append((name, None, 'no range covers this register'))
                        continue
                    parameter, offset = found
                    if parameter not in tables:
                        row.append((name, None, 'root parameter %d not bound' % parameter))
                        continue
                    heap, base = tables[parameter]
                    resource = slots.get((heap, base + offset))
                    row.append((name, resource,
                                'heap %d slot %d' % (heap, base + offset)))
                results.append((call.group(1), row))
    return results


def parse_register(text):
    match = re.fullmatch(r'([tubs])(\d+),\s*space(\d+)', text.strip())
    if not match:
        raise argparse.ArgumentTypeError('expected e.g. u15,space39')
    return REGISTER_KIND[match.group(1)], int(match.group(2)), int(match.group(3))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('export_dir')
    parser.add_argument('--pso', type=int, required=True)
    parser.add_argument('--register', action='append', type=parse_register, default=[],
                        help='e.g. u15,space39 (repeatable)')
    arguments = parser.parse_args()
    if not arguments.register:
        sys.exit('give at least one --register')

    for index, (kind, row) in enumerate(resolve(arguments.export_dir, arguments.pso,
                                                arguments.register)):
        print('%s #%d' % (kind, index))
        for name, resource, note in row:
            print('  %-16s -> %-12s %s' % (name,
                                           'resource %s' % resource if resource else '(none)',
                                           note))
    return 0


if __name__ == '__main__':
    sys.exit(main())
