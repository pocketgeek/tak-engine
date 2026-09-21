import unittest

from balance_inputs import unit_properties


class UnitPropertiesTest(unittest.TestCase):
    def test_weapon_fields_do_not_replace_motion_inputs(self):
        fields=unit_properties('''[UNITINFO] { turnrate=2300; brakerate=10; }
                                  [WEAPON2] { turnrate=180; range=400; }''')
        self.assertEqual(fields,{'turnrate':'2300','brakerate':'10'})

    def test_nested_fields_and_comments_are_not_unit_inputs(self):
        fields=unit_properties('''// [UNITINFO] { turnrate=0; }
            [UnitInfo] { turnrate=500; [unused] { turnrate=12; }
                         maxvelocity=1.5; }''')
        self.assertEqual(fields,{'turnrate':'500','maxvelocity':'1.5'})

    def test_missing_or_unterminated_section_is_rejected(self):
        for source in ('[WEAPON1] { turnrate=5; }','[UNITINFO] { turnrate=5;'):
            with self.assertRaises(ValueError): unit_properties(source)


if __name__=='__main__': unittest.main()
