"""Synthetic tests; no retail text or assets embedded."""
import unittest
from crusades_territories import parse_definition, summarize

HEADER = ('{[DarienMap<S:command=world_attribs><S:name=Test>'
          '<I:height=30><I:width=20><I:parcels=1><I:borders=0>]}')
PARCEL = ('{[DarienMap<S:command=parcel><S:name=Example><I:chatareaid=42>'
          '<S:description=Private prose\r\non another line.><I:xfireanchor=1>'
          '<I:yfireanchor=2><I:xtextanchor=3><I:ytextanchor=4>'
          '<S:nativerace=TestRace><I:nativeicon=0><S:terrain=TestTerrain>]}')


class TerritoryTests(unittest.TestCase):
    def parse(self, text):
        return parse_definition(text.encode('cp1252'))

    def test_multiline_and_crlf(self):
        world, parcels = self.parse(HEADER + '\r\n' + PARCEL + '\r\n')
        self.assertEqual(world['width'], 20)
        self.assertIn('\r\n', parcels[0]['description'])

    def test_summary_omits_prose_and_names(self):
        result = summarize((HEADER + PARCEL).encode())
        self.assertNotIn('Private prose', str(result))
        self.assertNotIn('Example', str(result))
        self.assertEqual(result['multiline_descriptions'], 1)
        self.assertEqual(result['observed_parcels'], 1)
        self.assertEqual(result['border_edge_records'], 0)

    def test_cp1252(self):
        _, parcels = self.parse(HEADER + PARCEL.replace('Example', 'Ex\u00e9mple'))
        self.assertEqual(parcels[0]['name'], 'Ex\u00e9mple')

    def test_duplicate_fields(self):
        with self.assertRaises(ValueError):
            self.parse(HEADER + PARCEL.replace('<I:nativeicon=0>', '<I:nativeicon=0><I:nativeicon=1>'))

    def test_garbage_not_silently_skipped(self):
        for value in ('garbage' + HEADER + PARCEL, HEADER + 'garbage' + PARCEL,
                      HEADER + PARCEL + 'garbage', HEADER + PARCEL.replace('<I:nativeicon=0>', 'garbage')):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.parse(value)

    def test_integer_validation(self):
        for value in ('1.5', '', ' 1', '1x'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.parse((HEADER + PARCEL).replace('<I:width=20>', f'<I:width={value}>'))

    def test_types_and_schema(self):
        for old, new in (('<I:width=20>', '<S:width=20>'), ('command=parcel', 'command=edge'),
                         ('<I:nativeicon=0>', ''), ('<I:nativeicon=0>', '<I:extra=0>')):
            with self.subTest(old=old, new=new), self.assertRaises(ValueError):
                self.parse((HEADER + PARCEL).replace(old, new))

    def test_structure(self):
        for value in ('', PARCEL, PARCEL + HEADER, HEADER + PARCEL + HEADER, HEADER):
            with self.subTest(value=value), self.assertRaises(ValueError):
                self.parse(value)

    def test_duplicates(self):
        header = HEADER.replace('parcels=1', 'parcels=2')
        for second in (PARCEL.replace('Example', 'Different'), PARCEL.replace('chatareaid=42', 'chatareaid=43')):
            with self.subTest(second=second), self.assertRaises(ValueError):
                self.parse(header + PARCEL + second)

    def test_invalid_dimensions(self):
        for old, new in (('width=20', 'width=0'), ('height=30', 'height=-1'), ('borders=0', 'borders=-1')):
            with self.subTest(old=old), self.assertRaises(ValueError):
                self.parse((HEADER + PARCEL).replace(old, new))


if __name__ == '__main__':
    unittest.main()
