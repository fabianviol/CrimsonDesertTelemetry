"""Tests for the pixtool export reader and the ambient buffer decoder."""
import importlib.util
import os
import struct
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def _load(name, filename):
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, 'scripts', filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


reader = _load('pixreader', 'Read-PixExportResource.py')
ambient = _load('ambientsh', 'Decode-AmbientSH.py')
shadermap = _load('shadermap', 'Map-PixExportShaders.py')


CREATE_RESOURCES = '''#include "pch.h"

// ApiObjectId = 40
void CreateAndInitResource_40()
{
    CreateAndTrackPlacedResource(40, g_device.Get(), &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, 7, 0);
    std::vector<BYTE> uncompressedData;
    g_resourceReader->Read(uncompressedData, 11);
}

// ApiObjectId = 41
void CreateAndInitResource_41()
{
    CreateAndTrackPlacedResource(41, g_device.Get(), &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, 7, 65536);
    std::vector<BYTE> uncompressedData;
    g_resourceReader->Read(uncompressedData, 22);
    g_resourceReader->Read(uncompressedData, 33);
}

// ApiObjectId = 42
void CreateAndInitResource_42()
{
    CreateAndTrackPlacedResource(42, g_device.Get(), &resourceDesc, D3D12_RESOURCE_STATE_COMMON, nullptr, 7, 131072);
}
'''

DRIVER = '''#include "pch.h"

void CreateAppResources_000()
{
    CreateAndInitResource_40();
    g_resourceReader->Read(accelStructureData, 5);
    CreateAndInitResource_41();
    CreateAndInitResource_42();
}
'''

DESCRIPTORS = '''    CreateConstantBufferView(GetCpuDescriptor(g_heap.Get(), 1), GetGpuva(7, 65536), 1024);
    CreateConstantBufferView(GetCpuDescriptor(g_heap.Get(), 2), GetGpuva(7, 65536), 1024);
    CreateConstantBufferView(GetCpuDescriptor(g_heap.Get(), 3), GetGpuva(7, 0), 256);
'''


def make_export(directory):
    with open(os.path.join(directory, 'CreateAndInitResources_000.cpp'), 'w') as stream:
        stream.write(CREATE_RESOURCES)
    with open(os.path.join(directory, 'FrameResources_000.cpp'), 'w') as stream:
        stream.write(DRIVER)
    with open(os.path.join(directory, 'Descriptors_000.cpp'), 'w') as stream:
        stream.write(DESCRIPTORS)


class ExportReaderTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.mkdtemp()
        make_export(self.directory)

    def test_read_order_follows_the_driver_not_the_file_order(self):
        sequence = reader.read_sequence(self.directory)
        self.assertEqual([size for _, size in sequence], [11, 5, 22, 33])

    def test_a_function_contributes_every_one_of_its_reads(self):
        labels = [label for label, _ in reader.read_sequence(self.directory)]
        self.assertEqual(labels.count('CreateAndInitResource_41'), 2)

    def test_a_function_without_reads_consumes_nothing(self):
        labels = [label for label, _ in reader.read_sequence(self.directory)]
        self.assertNotIn('CreateAndInitResource_42', labels)

    def test_gpuva_resolves_a_heap_offset_to_the_placed_resource(self):
        self.assertEqual(reader.resolve_gpuva(self.directory, 7, 65536), (41, 0))

    def test_gpuva_resolves_an_offset_inside_a_placed_resource(self):
        self.assertEqual(reader.resolve_gpuva(self.directory, 7, 65600), (41, 64))

    def test_gpuva_falls_back_to_the_resource_for_an_untracked_handle(self):
        self.assertEqual(reader.resolve_gpuva(self.directory, 999, 16), (999, 16))

    def test_listing_counts_views_of_the_requested_size_only(self):
        found = reader.list_constant_buffer_views(self.directory, 1024)
        self.assertEqual(found, [((7, 65536), 2)])

    def test_a_missing_resource_is_reported_rather_than_guessed(self):
        with self.assertRaises(LookupError):
            reader.extract(self.directory, 42)


def ambient_buffer(direct=(0.4, 0.3, 0.2), ninth=(0.05, 0.04, 0.03), extra=None):
    values = [0.0] * 256
    for channel in range(3):
        values[(channel * 2) * 4] = direct[channel]      # the DC term of each channel
        values[(channel * 2 + 1) * 4] = direct[channel] * 0.1
        values[6 * 4 + channel] = ninth[channel]
    values[7 * 4] = 1234.0
    for index, value in (extra or {}).items():
        values[index] = value
    return struct.pack('<256f', *values)


class AmbientDecoderTests(unittest.TestCase):
    def test_split_gives_three_channels_of_nine(self):
        sets = ambient.sets_from_buffer(ambient_buffer())
        coefficients = ambient.harmonics(sets[0])
        self.assertEqual(len(coefficients), 3)
        self.assertTrue(all(len(channel) == 9 for channel in coefficients))

    def test_the_ninth_coefficient_comes_from_slot_six(self):
        sets = ambient.sets_from_buffer(ambient_buffer(ninth=(0.05, 0.04, 0.03)))
        self.assertEqual([channel[8] for channel in ambient.harmonics(sets[0])],
                         [0.05000000074505806, 0.03999999910593033, 0.029999999329447746])

    def test_slot_seven_is_kept_out_of_the_harmonics(self):
        sets = ambient.sets_from_buffer(ambient_buffer())
        flat = [value for channel in ambient.harmonics(sets[0]) for value in channel]
        self.assertNotIn(1234.0, flat)

    def test_the_direct_term_is_the_largest_in_a_smooth_environment(self):
        sets = ambient.sets_from_buffer(ambient_buffer())
        _, _, peak = ambient.describe(sets[0])
        self.assertEqual(peak, [0, 0, 0])

    def test_a_set_holding_only_a_colour_is_not_read_as_harmonics(self):
        # Slot 56 in the lantern capture carries a bare RGB triple, not a set.
        data = ambient_buffer(extra={56 * 4: 0.02, 56 * 4 + 1: 0.016, 56 * 4 + 2: 0.006})
        sets = ambient.sets_from_buffer(data)
        self.assertTrue(ambient.is_populated(sets[7]))
        self.assertFalse(ambient.carries_harmonics(sets[7]))

    def test_the_populated_set_is_still_read_as_harmonics(self):
        sets = ambient.sets_from_buffer(ambient_buffer())
        self.assertTrue(ambient.carries_harmonics(sets[0]))

    def test_an_empty_set_is_neither_populated_nor_harmonic(self):
        sets = ambient.sets_from_buffer(ambient_buffer())
        self.assertFalse(ambient.is_populated(sets[3]))
        self.assertFalse(ambient.carries_harmonics(sets[3]))

    def test_a_short_buffer_is_refused(self):
        with self.assertRaises(ValueError):
            ambient.sets_from_buffer(b'\x00' * 512)



class AmbientBasisTests(unittest.TestCase):
    """The producer's own constants, checked against the textbook normalisations."""

    def test_every_lane_is_named(self):
        self.assertEqual(len(ambient.BASIS), 9)

    def test_lane_zero_carries_the_direct_basis_constant(self):
        import math
        name, expression = ambient.BASIS[0]
        self.assertEqual(name.strip(), 'Y00')
        self.assertAlmostEqual(float(expression), 0.5 * math.sqrt(1 / math.pi), places=6)

    def test_the_polar_axis_is_z(self):
        self.assertIn('d.z * d.z', ambient.BASIS[6][1])

    def test_the_l1_orientation_uses_the_producer_signs(self):
        channel = [0.0, -2.0, 3.0, -5.0, 0, 0, 0, 0, 0]     # L1-1, L10, L11
        self.assertEqual(ambient.l1_orientation(channel), (5.0, 2.0, 3.0))

    def test_a_sky_above_the_camera_reads_as_positive_y(self):
        data = ambient_buffer()
        sets = ambient.sets_from_buffer(data)
        coefficients = ambient.harmonics(sets[0])
        coefficients[0][1] = -0.005414        # the measured red L1-1 of the lantern capture
        x, y, z = ambient.l1_orientation(coefficients[0])
        self.assertGreater(y, 0.0)
        self.assertGreater(abs(y), abs(x))
        self.assertGreater(abs(y), abs(z))


NUL = bytes([0])


def container(*names, trailing=True):
    """A stand-in DXIL container: NUL-terminated strings amid binary noise."""
    blob = bytes([0, 1]) + b'DXBC'
    for name in names:
        blob += NUL + name.encode('ascii')
    return blob + (NUL if trailing else b'') + b'tail'


class ShaderMapTests(unittest.TestCase):
    """Entry names are pulled out of DXIL containers as NUL-terminated strings."""

    def test_a_suffixed_entry_name_is_found(self):
        self.assertEqual(shadermap.entry_names(container('EvaluateDiffuseRadianceCS')),
                         ['EvaluateDiffuseRadianceCS'])

    def test_a_prefixed_entry_name_is_found(self):
        # csPrecomputeAmbient and its family end in no stage suffix at all.
        self.assertEqual(shadermap.entry_names(container('csPrecomputeAmbient')),
                         ['csPrecomputeAmbient'])

    def test_names_are_deduplicated_but_keep_their_order(self):
        self.assertEqual(shadermap.entry_names(container('BravoCS', 'AlphaCS', 'BravoCS')),
                         ['BravoCS', 'AlphaCS'])

    def test_an_unterminated_candidate_is_not_reported(self):
        self.assertEqual(shadermap.entry_names(b'RenderDiffuseCS'), [])

    def test_a_container_without_names_yields_nothing(self):
        self.assertEqual(shadermap.entry_names(container()), [])

    def test_only_pipeline_state_reads_are_collected(self):
        directory = tempfile.mkdtemp()
        make_export(directory)
        self.assertEqual(list(shadermap.pipeline_blocks(reader, directory)), [])


if __name__ == '__main__':
    unittest.main()
