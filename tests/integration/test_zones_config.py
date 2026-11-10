"""Configuration rendering tests; no device, network or private data."""
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools'))
from zones_config import load_zones, render


class ZoneConfigTests(unittest.TestCase):
    def test_missing_file_uses_generic_defaults(self):
        with tempfile.TemporaryDirectory() as folder:
            result = load_zones(Path(folder) / 'missing.yaml')
        self.assertEqual(result[0]['name'], 'Zona 1')
        self.assertEqual(result[0]['group'], 'Sin asignar')
        self.assertEqual([x['zone'] for x in result], list(range(1, 11)))

    def test_overrides_and_fallback(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'zones.yaml'
            path.write_text('visible_zones: [3, 9]\nzones:\n  9:\n    name: Entrada\n    group: Casa\n    type: door\n', encoding='utf-8')
            result = load_zones(path)
            self.assertEqual(result[0]['name'], 'Zona 3')
            self.assertEqual(result[1]['name'], 'Zona 9')
            self.assertEqual(result[1]['display_name'], 'Entrada')
            self.assertEqual(result[1]['device_class'], 'door')
            self.assertIn('Entrada', render('binary_sensor:\n@@ZONE_ENTITIES@@\n', path))

    def test_rejects_invalid_and_duplicate_numbers(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'zones.yaml'
            for text in ('visible_zones: [1, 1]', 'visible_zones: [65]',
                         'visible_zones: [1]\nzones:\n  2: {name: Hidden}',
                         'visible_zones: [1]\nzones:\n  1: {type: broken}'):
                path.write_text(text, encoding='utf-8')
                with self.assertRaises(ValueError):
                    load_zones(path)

if __name__ == '__main__':
    unittest.main()
