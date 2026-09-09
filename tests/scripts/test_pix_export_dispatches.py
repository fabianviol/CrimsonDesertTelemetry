"""Tests for attributing a pixtool export's dispatches to a resource."""
import importlib.util
import os
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

_spec = importlib.util.spec_from_file_location(
    'pixdispatches', os.path.join(ROOT, 'scripts', 'Find-PixExportDispatches.py'))
dispatches = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(dispatches)


# The generated calls name the resource BEFORE the descriptor for UAVs and SRVs,
# and after it for CBVs. Both orders have to resolve or a whole view type is lost.
DESCRIPTORS = [
    "    CreateUnorderedAccessView_Buffer(GetResource(99).Get(), nullptr, "
    "GetCpuDescriptor(g_descriptorHeap_4.Get(), 700), DXGI_FORMAT_UNKNOWN, "
    "D3D12_UAV_DIMENSION_BUFFER, 0, 64, 16, 0, D3D12_BUFFER_UAV_FLAG_NONE);",
    "    CreateUnorderedAccessView_Buffer(GetResource(11).Get(), nullptr, "
    "GetCpuDescriptor(g_descriptorHeap_4.Get(), 701), DXGI_FORMAT_UNKNOWN, "
    "D3D12_UAV_DIMENSION_BUFFER, 0, 4, 16, 0, D3D12_BUFFER_UAV_FLAG_NONE);",
    "    CreateShaderResourceView_Buffer(GetResource(99).Get(), "
    "GetCpuDescriptor(g_descriptorHeap_4.Get(), 703), DXGI_FORMAT_UNKNOWN, 0, 64, 16);",
]

COMMANDS = [
    "    list->SetPipelineState(GetPipelineState(500).Get());",
    "    list->SetComputeRootDescriptorTable(3, GetGpuDescriptor(g_descriptorHeap_4.Get(), 700));",
    "    list->Dispatch(1, 1, 1);",
    "    list->SetPipelineState(GetPipelineState(501).Get());",
    "    list->SetComputeRootDescriptorTable(3, GetGpuDescriptor(g_descriptorHeap_4.Get(), 701));",
    "    list->Dispatch(1, 1, 1);",
    "    list->SetComputeRootDescriptorTable(3, GetGpuDescriptor(g_descriptorHeap_4.Get(), 700));",
    "    list->SetPipelineState(GetPipelineState(502).Get());",
    "    list->Dispatch(1, 1, 1);",
]


class DescriptorLookupTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp()
        self._write('Descriptors_000.cpp', DESCRIPTORS)
        self._write('CommandLists_000.cpp', COMMANDS)

    def _write(self, name, lines):
        with open(os.path.join(self.directory, name), 'w', encoding='utf-8') as stream:
            stream.write('\n'.join(lines) + '\n')

    def test_a_uav_is_found_with_the_resource_named_first(self):
        self.assertEqual(dispatches.descriptor_slots(self.directory, 99, 'uav'), {700})

    def test_an_srv_over_the_same_resource_is_a_separate_view_kind(self):
        self.assertEqual(dispatches.descriptor_slots(self.directory, 99, 'srv'), {703})

    def test_another_resource_does_not_leak_in(self):
        self.assertEqual(dispatches.descriptor_slots(self.directory, 11, 'uav'), {701})

    def test_a_resource_with_no_view_yields_nothing(self):
        self.assertEqual(dispatches.descriptor_slots(self.directory, 12345, 'uav'), set())


class DispatchWalkTests(DescriptorLookupTests):
    def test_only_the_dispatch_that_bound_the_slot_is_reported(self):
        found = list(dispatches.dispatches(self.directory, {700}))
        self.assertEqual([(pipeline, kind) for _, pipeline, kind in found],
                         [(500, 'Dispatch')])

    def test_a_pipeline_change_after_the_binding_disarms_it(self):
        # The third dispatch binds slot 700 but then switches pipeline state, so
        # the binding no longer belongs to the shader that runs.
        found = [pipeline for _, pipeline, _ in dispatches.dispatches(self.directory, {700})]
        self.assertNotIn(502, found)

    def test_a_slot_nobody_binds_produces_no_dispatch(self):
        self.assertEqual(list(dispatches.dispatches(self.directory, {703})), [])


class NameLookupTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp()

    def test_names_come_from_the_shader_map(self):
        path = os.path.join(self.directory, 'map.csv')
        with open(path, 'w', encoding='utf-8') as stream:
            stream.write('kind,pso,entry\ncompute,500,ClearVoxelsBufferCS\n')
        self.assertEqual(dispatches.load_names(path), {500: ['ClearVoxelsBufferCS']})

    def test_a_missing_map_is_not_fatal(self):
        self.assertEqual(dispatches.load_names(os.path.join(self.directory, 'nope.csv')), {})

    def test_no_map_at_all_is_not_fatal(self):
        self.assertEqual(dispatches.load_names(None), {})


if __name__ == '__main__':
    unittest.main()
