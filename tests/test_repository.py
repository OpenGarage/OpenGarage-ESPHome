"""Small publication-tree checks; full hardware/regression tests live locally."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]


class RepositoryChecks(unittest.TestCase):
    def test_document_links(self):
        for path in [ROOT / 'README.md', ROOT / 'VENDOR.md', *ROOT.glob('docs/*.md')]:
            for target in re.findall(r'\]\(([^)]+)\)', path.read_text()):
                if '://' in target or target.startswith('#'):
                    continue
                with self.subTest(file=path.name, target=target):
                    self.assertTrue((path.parent / target.split('#', 1)[0]).is_file())

    def test_package_includes(self):
        for path in [*ROOT.glob('firmware/*.yaml'), *ROOT.glob('packages/*.yaml')]:
            for target in re.findall(r'!include\s+(\S+)', path.read_text()):
                with self.subTest(file=path.name, target=target):
                    self.assertTrue((path.parent / target).is_file())

    def test_single_generic_entrypoint(self):
        self.assertEqual([p.name for p in ROOT.glob('firmware/*.yaml')], ['opengarage-generic.yaml'])
        text = (ROOT / 'firmware/opengarage-generic.yaml').read_text()
        self.assertNotIn('!secret', text)
        self.assertIn('client_id: provisioned', text)

