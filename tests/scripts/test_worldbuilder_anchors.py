import importlib.util
import struct
import unittest
from pathlib import Path

spec = importlib.util.spec_from_file_location('wbanchors', Path(__file__).resolve().parents[2] / 'scripts/Inspect-WorldBuilderAnchors.py')
wb = importlib.util.module_from_spec(spec)
spec.loader.exec_module(wb)


class WorldBuilderAnchorsTests(unittest.TestCase):
    def section(self, raw=0, size=8, rva=0x1000, flags=0x60000000):
        return dict(raw=raw, size=size, rva=rva, flags=flags, virtual_size=size)

    def test_literal_calls(self):
        text = 'static void ResolveProbeCollectorVtable() {\n FindPatternCount("48 ?? 90", &n);\n}\nResolveSig("setEnable", "40 57", &r);'
        rows = wb.patterns(text)
        self.assertEqual([(r['name'], r['line']) for r in rows], [('ResolveProbeCollectorVtable', 2), ('setEnable', 4)])

    def test_rename_does_not_matter(self):
        self.assertEqual(wb.find_matches(b'\x48\x0a\x90', [self.section(size=3)], '48 ?? 90'), [0x1000])

    def test_overlap_is_ambiguous(self):
        self.assertEqual(wb.find_matches(b'\x90'*3, [self.section(size=3)], '90 90'), [0x1000, 0x1001])

    def test_data_not_executable(self):
        self.assertEqual(wb.find_matches(b'\x90'*3, [self.section(size=3, flags=0x40000000)], '90 90'), [])

    def test_section_boundary(self):
        self.assertEqual(wb.find_matches(b'\x90'*4, [self.section(size=2), self.section(raw=2, size=2)], '90 90 90'), [])

    def test_unbacked_rva(self):
        self.assertIsNone(wb.read_rva(b'12345678', [self.section()], 0x1007, 2))

    def test_empty_pattern(self):
        for p in ('', '?? ?'):
            with self.assertRaises(ValueError):
                wb.find_matches(b'12345678', [self.section()], p)

    def test_mask_prefix_and_last_position(self):
        self.assertEqual(wb.find_matches(b'\x11\x48\x90\x0a', [self.section(size=4)], '?? 48 90 ??'), [0x1000])

    def test_nested_call_is_not_function_name(self):
        text = 'static DWORD WINAPI InitThread(LPVOID) {\n if (MH_Initialize() != 0) { return 0; }\n FindPattern("48 ?? 90");\n}'
        self.assertEqual(wb.patterns(text)[0]['name'], 'InitThread')

    def test_collector_shape(self):
        base = 0x140000000
        slots = [base+0x1000]*12
        data = b'\x90'*8 + struct.pack('<12Q', *slots)
        table = [self.section(), self.section(raw=8, size=96, rva=0x2000, flags=0x40000000)]
        self.assertEqual(wb.collector_vtables(data, table, base, 0x1000), [0x2000])
        slots[9] += 1
        self.assertEqual(wb.collector_vtables(data[:8]+struct.pack('<12Q', *slots), table, base, 0x1000), [])


if __name__ == '__main__':
    unittest.main()
