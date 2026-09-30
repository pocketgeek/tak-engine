"""Synthetic archived data exercises VFS/TDF balance comparison; no game assets."""
import csv
import io
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
AUDIT = ROOT / 'build/crusades_balance_audit'
HPI = ROOT / 'build/hpitool'

@unittest.skipUnless(AUDIT.exists() and HPI.exists(), 'Build crusades_balance_audit and hpitool first')
class BalanceAuditTests(unittest.TestCase):
    def run_audit(self, files):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            source = root / 'input'
            source.mkdir()
            for path, text in files.items():
                p = source / path
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_text(text.replace(";", ";\n"))
            data = root / 'data'
            data.mkdir()
            subprocess.run([str(HPI), 'pack', str(source), str(data/'data.hpi')],
                           check=True, capture_output=True)
            return subprocess.run([str(AUDIT), str(data)], text=True, capture_output=True)

    def test_nested_diff_and_prose_redaction(self):
        result = self.run_audit({
            'units/example.fbi': '[UNITINFO]{ObjectName=example;Name=Secret old name;MaxDamage=10;}[WEAPON1]{[DAMAGE]{Default=5;Fort=2;}}',
            'unitscb/example.fbi': '[UNITINFO]{ObjectName=example;Name=Secret new name;MaxDamage=20;}[WEAPON1]{[DAMAGE]{Default=7;}}',
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn('Secret', result.stdout)
        rows = {r['field']: r for r in csv.DictReader(io.StringIO(result.stdout), delimiter='\t')}
        self.assertEqual(rows['unitinfo#1/maxdamage']['crusades'], '20')
        self.assertEqual(rows['weapon1#1/damage#1/fort']['crusades'], '<absent>')
        self.assertEqual(rows['weapon1#1/damage#1/default']['standard'], '5')
        self.assertNotIn('unitinfo#1/objectname', rows)

    def test_menu_replacement_and_unit_fallback(self):
        result = self.run_audit({
            'units/baseonly.fbi': '[UNITINFO]{ObjectName=baseonly;MaxDamage=10;}',
            'canbuild/builder/old.tdf': '',
            'canbuildcb/builder/new.tdf': '',
            'canbuild/other/retained.tdf': '[MENU]{Priority=1;}',
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = list(csv.DictReader(io.StringIO(result.stdout), delimiter='\t'))
        self.assertEqual({(r['path'],r['kind']) for r in rows}, {
            ('canbuild/builder/old.tdf','removed-menu-entry'),
            ('canbuild/builder/new.tdf','added-definition')})

    def test_repeated_sections_remain_distinct(self):
        result = self.run_audit({
            'units/test.fbi': '[EFFECT]{Value=1;}[EFFECT]{Value=2;}',
            'unitscb/test.fbi': '[EFFECT]{Value=3;}[EFFECT]{Value=2;}',
        })
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = list(csv.DictReader(io.StringIO(result.stdout), delimiter='\t'))
        self.assertEqual(len(rows),1)
        self.assertEqual(rows[0]['field'],'effect#1/value')

    def test_absent_overlay_fails(self):
        result = self.run_audit({'units/test.fbi': '[UNITINFO]{MaxDamage=1;}'})
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('No Crusades definitions', result.stderr)

if __name__ == '__main__':
    unittest.main()
