"""Tests for resolving a shader register to the resource bound at a dispatch."""
import importlib.util
import os
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

_spec = importlib.util.spec_from_file_location(
    'pixbindings', os.path.join(ROOT, 'scripts', 'Resolve-PixExportBindings.py'))
binds = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(binds)


FRAME_RESOURCES = '''void CreateAppResources_000()
{
    {
        D3D12_ROOT_PARAMETER1 rootParameters[2];
        rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        {
            static D3D12_DESCRIPTOR_RANGE1 descriptorRanges[2];
            descriptorRanges[0] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 5, 39, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 0 };
            descriptorRanges[1] = { D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 12, 10, 39, D3D12_DESCRIPTOR_RANGE_FLAG_NONE, 4294967295 };
            rootParameters[0].DescriptorTable = { 2, descriptorRanges };
        }
        rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParameters[1].Descriptor = { 0, 1 };
        CreateAndTrackRootSignature(700, g_device.Get(), 0, signature.Get());
    }
}
'''

CREATE_PSOS = '''void CreateComputePipelineState_900()
{
    cpsoDesc.pRootSignature = GetRootSignature(700);
}
'''

# Slot 300 is written twice: once at init, then again mid-frame. A dispatch before
# the rewrite must see the first resource, one after it the second.
DESCRIPTORS = ('    CreateUnorderedAccessView_Tex3D(GetResource(11).Get(), nullptr, '
               'GetCpuDescriptor(g_descriptorHeap_9.Get(), 300), DXGI_FORMAT_UNKNOWN);\n'
               '    CreateUnorderedAccessView_Tex3D(GetResource(22).Get(), nullptr, '
               'GetCpuDescriptor(g_descriptorHeap_9.Get(), 304), DXGI_FORMAT_UNKNOWN);\n')

MODIFY = ('void ModifyDescriptors_9_0()\n{\n'
          '    CreateUnorderedAccessView_Tex3D(GetResource(33).Get(), nullptr, '
          'GetCpuDescriptor(g_descriptorHeap_9.Get(), 300), DXGI_FORMAT_UNKNOWN);\n}\n')

COMMANDS = ('void PopulateCommandList_1()\n{\n'
            '    list->SetComputeRootSignature(GetRootSignature(700));\n'
            '    list->SetPipelineState(GetPipelineState(900).Get());\n'
            '    list->SetComputeRootDescriptorTable(0, GetGpuDescriptor(g_descriptorHeap_9.Get(), 300));\n'
            '    list->Dispatch(1, 1, 1);\n}\n'
            'void PopulateCommandList_2()\n{\n'
            '    list->SetComputeRootSignature(GetRootSignature(700));\n'
            '    list->SetPipelineState(GetPipelineState(900).Get());\n'
            '    list->SetComputeRootDescriptorTable(0, GetGpuDescriptor(g_descriptorHeap_9.Get(), 300));\n'
            '    list->Dispatch(1, 1, 1);\n}\n')

WORKER = ('void RenderFrame_000()\n{\n'
          '    PopulateCommandList_1();\n'
          '    ModifyDescriptors_9_0();\n'
          '    PopulateCommandList_2();\n}\n')


def make_export():
    directory = tempfile.mkdtemp()
    for name, text in (('FrameResources_000.cpp', FRAME_RESOURCES),
                       ('CreatePSOs.cpp', CREATE_PSOS),
                       ('Descriptors_000.cpp', DESCRIPTORS),
                       ('ModifyDescriptors_000.cpp', MODIFY),
                       ('CommandLists_000.cpp', COMMANDS),
                       ('RenderFrameWorker_000.cpp', WORKER)):
        with open(os.path.join(directory, name), 'w', encoding='utf-8') as stream:
            stream.write(text)
    return directory


class RootSignatureTests(unittest.TestCase):
    def setUp(self):
        self.directory = make_export()
        self.signatures = binds.root_signatures(self.directory)

    def test_the_signature_is_found_by_its_id(self):
        self.assertIn(700, self.signatures)

    def test_a_table_parameter_keeps_all_of_its_ranges(self):
        self.assertEqual(len(self.signatures[700][0]['ranges']), 2)

    def test_range_fields_are_read_in_the_d3d12_order(self):
        first = self.signatures[700][0]['ranges'][0]
        self.assertEqual((first['kind'], first['count'], first['base_register'],
                          first['space'], first['offset']),
                         ('UAV', 4, 5, 39, 0))

    def test_a_root_descriptor_parameter_keeps_its_register(self):
        self.assertEqual(self.signatures[700][1]['type'], 'CBV')
        self.assertEqual(self.signatures[700][1]['register'], 0)

    def test_a_pipeline_state_is_linked_to_its_signature(self):
        self.assertEqual(binds.pso_root_signatures(self.directory)[900], 700)


class RangeOffsetTests(unittest.TestCase):
    def test_an_explicit_offset_is_kept(self):
        ranges = [{'count': 4, 'offset': 8}]
        self.assertEqual(binds.range_offsets(ranges), [8])

    def test_append_follows_the_end_of_the_previous_range(self):
        # OFFSET_APPEND is the trap: the offset is not in the struct at all.
        ranges = [{'count': 4, 'offset': 0}, {'count': 12, 'offset': binds.APPEND}]
        self.assertEqual(binds.range_offsets(ranges), [0, 4])

    def test_append_at_the_start_means_zero(self):
        self.assertEqual(binds.range_offsets([{'count': 3, 'offset': binds.APPEND}]), [0])


class DescriptorWriteParsingTests(unittest.TestCase):
    def test_tex3d_uav_helper_names_are_parsed(self):
        body = ('CreateUnorderedAccessView_Tex3D(GetResource(44).Get(), nullptr, '
                'GetCpuDescriptor(g_descriptorHeap_9.Get(), 704), DXGI_FORMAT_UNKNOWN);')
        self.assertEqual(binds.descriptor_writes(body), [('UAV', 9, 704, 44)])

    def test_tex3d_srv_helper_names_are_parsed(self):
        body = ('CreateShaderResourceView_Tex3D(GetResource(55).Get(), '
                'GetCpuDescriptor(g_descriptorHeap_9.Get(), 705), DXGI_FORMAT_UNKNOWN);')
        self.assertEqual(binds.descriptor_writes(body), [('SRV', 9, 705, 55)])


class HeapIndexTests(unittest.TestCase):
    def setUp(self):
        self.parameters = binds.root_signatures(make_export())[700]

    def test_a_register_inside_the_first_range(self):
        self.assertEqual(binds.heap_index(self.parameters, 'UAV', 5, 39), (0, 0))

    def test_a_register_offset_within_its_range(self):
        self.assertEqual(binds.heap_index(self.parameters, 'UAV', 6, 39), (0, 1))

    def test_a_register_in_an_appended_range_lands_after_the_first(self):
        self.assertEqual(binds.heap_index(self.parameters, 'UAV', 10, 39), (0, 4))
        self.assertEqual(binds.heap_index(self.parameters, 'UAV', 15, 39), (0, 9))

    def test_a_register_no_range_covers_is_unresolved(self):
        self.assertIsNone(binds.heap_index(self.parameters, 'UAV', 99, 39))

    def test_a_matching_register_in_another_space_is_unresolved(self):
        self.assertIsNone(binds.heap_index(self.parameters, 'UAV', 5, 36))

    def test_the_view_kind_has_to_match(self):
        self.assertIsNone(binds.heap_index(self.parameters, 'SRV', 5, 39))


class TimeAccuracyTests(unittest.TestCase):
    def setUp(self):
        self.directory = make_export()

    def test_a_dispatch_sees_the_descriptor_as_it_stood_then(self):
        rows = binds.resolve(self.directory, 900, [('UAV', 5, 39)])
        self.assertEqual([resource for _, row in rows for _, resource, _ in row], [11, 33])

    def test_the_appended_range_resolves_to_its_own_slot(self):
        # Register 10 is the first of the appended range, so it lands four
        # descriptors past the table base: slot 300 + 4.
        rows = binds.resolve(self.directory, 900, [('UAV', 10, 39)])
        self.assertEqual(rows[0][1][0][1], 22)

    def test_an_unbound_root_parameter_is_reported_not_guessed(self):
        parameters = binds.root_signatures(self.directory)[700]
        self.assertEqual(binds.heap_index(parameters, 'CBV', 0, 1), None)

    def test_a_pipeline_state_with_no_signature_is_refused(self):
        with self.assertRaises(LookupError):
            binds.resolve(self.directory, 12345, [('UAV', 5, 39)])


class RegisterParsingTests(unittest.TestCase):
    def test_each_prefix_maps_to_its_view_kind(self):
        self.assertEqual(binds.parse_register('u15,space39'), ('UAV', 15, 39))
        self.assertEqual(binds.parse_register('t7,space36'), ('SRV', 7, 36))
        self.assertEqual(binds.parse_register('b1,space35'), ('CBV', 1, 35))

    def test_nonsense_is_refused(self):
        with self.assertRaises(Exception):
            binds.parse_register('nonsense')


if __name__ == '__main__':
    unittest.main()
