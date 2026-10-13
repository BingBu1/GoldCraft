"""Small resource fixtures; no game process, downloads or production writes."""
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

from ServerPackageMaps import map_resources, select_maps

ROOT = Path(__file__).resolve().parents[1]


class Maps(unittest.TestCase):
    def setUp(self):
        directory = ROOT / 'analysis/server-package/map-fixtures'
        directory.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=directory)
        self.base = Path(self.temp.name)
        self.game, self.valve = self.base / 'cstrike', self.base / 'valve'
        (self.game / 'maps').mkdir(parents=True)
        self.valve.mkdir()

    def tearDown(self):
        self.temp.cleanup()

    def bsp(self, name='cs_demo', *, texture=False):
        entities = b'{"classname" "worldspawn" "wad" "Z:\\compiler\\used.wad;Z:\\missing-unused.wad"}\0'
        textures = (struct.pack('<ii', 1, 8) + b'wall\0'.ljust(16, b'\0') +
                    struct.pack('<6I', 16, 16, 0, 0, 0, 0)) if texture else struct.pack('<i', 0)
        lumps = [(124, len(entities)), (0, 0), (124 + len(entities), len(textures))] + [(0, 0)] * 12
        data = struct.pack('<i', 30) + b''.join(struct.pack('<ii', *x) for x in lumps) + entities + textures
        path = self.game / 'maps' / (name + '.bsp')
        path.write_bytes(data)
        return path

    def resources(self, bsp):
        return map_resources(bsp, self.game, self.valve, lambda path: path)

    def test_all_maps_default_order_and_lowercase_dimension(self):
        a, b = self.bsp('MapA'), self.bsp('MapB')
        self.assertEqual(select_maps(self.game, ['*'], 'mapb'), [b, a])
        _, record = self.resources(b)
        self.assertEqual(record['dimension'], f'goldcraft:mapb_{zlib.crc32(b.read_bytes()):08x}')

    def test_bad_selection(self):
        self.bsp()
        for names, default in [([], 'cs_demo'), (['../cs_demo'], 'cs_demo'),
                               (['cs_demo', 'CS_DEMO'], 'cs_demo'), (['missing'], 'missing'),
                               (['cs_demo'], 'absent')]:
            with self.assertRaises(ValueError):
                select_maps(self.game, names, default)

    def test_missing_actual_texture_rejected_but_unused_wad_allowed(self):
        self.resources(self.bsp())
        with self.assertRaisesRegex(ValueError, 'missing WAD textures'):
            self.resources(self.bsp(texture=True))

    def test_needed_wad_included(self):
        path = self.game / 'used.wad'
        path.write_bytes(b'WAD3' + struct.pack('<ii', 1, 12) + b'\0' * 16 + b'wall\0'.ljust(16, b'\0'))
        resources, _ = self.resources(self.bsp(texture=True))
        self.assertEqual(resources['cs/cstrike/used.wad'], path)

    def test_resource_and_waypoint_closure(self):
        bsp = self.bsp()
        (self.game / 'sound').mkdir()
        asset = self.game / 'sound/custom.wav'
        asset.write_bytes(b'fixture')
        bsp.with_suffix('.res').write_text('"sound/custom.wav" // comment\n', encoding='utf-8')
        wpt = self.game / 'addons/sypb/wptdefault/cs_demo.pwf'
        wpt.parent.mkdir(parents=True)
        wpt.write_bytes(b'fixture')
        resources, record = self.resources(bsp)
        self.assertIn('cs/cstrike/sound/custom.wav', resources)
        self.assertTrue(record['botWaypoints'])

    def test_missing_or_unsafe_resource_rejected(self):
        bsp = self.bsp()
        for name in ('../private.txt', 'C:/private.txt', '/private.txt', 'helper.exe', 'sound/missing.wav'):
            bsp.with_suffix('.res').write_text(name, encoding='utf-8')
            with self.assertRaises((ValueError, FileNotFoundError)):
                self.resources(bsp)

    def test_invalid_bsp_bounds(self):
        bsp = self.bsp()
        data = bytearray(bsp.read_bytes())
        struct.pack_into('<ii', data, 4, len(data) + 1, 5)
        bsp.write_bytes(data)
        with self.assertRaises(ValueError):
            self.resources(bsp)


if __name__ == '__main__':
    unittest.main()
