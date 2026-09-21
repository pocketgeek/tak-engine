"""Synthetic memory tests; no retail executable or capture is loaded."""
import struct
import unittest

from emureload import CapturedProcess
from unicorn import Uc, UC_ARCH_X86, UC_MODE_32


class AllocationTests(unittest.TestCase):
    def setUp(self):
        self.p = CapturedProcess.__new__(CapturedProcess)
        self.p.uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.p.pages = set()
        self.p.allocations = {}
        self.p.brk = 0x60000000

    def test_mapping_overlapping_chunks_preserves_existing_bytes(self):
        self.p.put(0x101000, b'kept')
        self.p.ensure(0x100000, 0x4000)
        self.assertEqual(bytes(self.p.uc.mem_read(0x101000, 4)), b'kept')
        self.p.put(0x103fff, b'cross page')
        self.assertEqual(bytes(self.p.uc.mem_read(0x103fff, 10)), b'cross page')

    def test_realloc_preserves_known_contents_on_growth_and_shrink(self):
        self.p.put(0x10000, struct.pack('<II', 0, 8))
        _, first = self.p.reallocate(self.p.uc, 0x10000)
        self.p.put(first, b'12345678')
        self.p.put(0x10000, struct.pack('<II', first, 32))
        _, second = self.p.reallocate(self.p.uc, 0x10000)
        self.assertEqual(bytes(self.p.uc.mem_read(second, 8)), b'12345678')
        self.p.put(0x10000, struct.pack('<II', second, 3))
        _, third = self.p.reallocate(self.p.uc, 0x10000)
        self.assertEqual(bytes(self.p.uc.mem_read(third, 3)), b'123')

    def test_unknown_extent_is_not_guessed(self):
        self.p.put(0x10000, struct.pack('<II', 0x12340000, 16))
        with self.assertRaisesRegex(RuntimeError, 'unknown original realloc extent'):
            self.p.reallocate(self.p.uc, 0x10000)

    def test_zero_size_realloc_does_not_read_old_storage(self):
        self.p.put(0x10000, struct.pack('<II', 0x12340000, 0))
        self.assertEqual(self.p.reallocate(self.p.uc, 0x10000), (0, 0))


if __name__ == '__main__':
    unittest.main()
