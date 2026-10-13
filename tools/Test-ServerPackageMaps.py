"""Small resource fixtures; no game process, downloads or production writes."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import unittest
import zlib
import zipfile

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


@unittest.skipUnless(os.name == 'nt', 'Windows PowerShell package startup')
class PackageStartup(unittest.TestCase):
    def setUp(self):
        directory = ROOT / 'analysis/server-package/map-fixtures'
        directory.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=directory)
        self.base = Path(self.temp.name)
        scripts = self.base / 'scripts'
        scripts.mkdir()
        for name in ('Common.ps1', 'Start-Servers.ps1'):
            shutil.copyfile(ROOT / 'tools/server-package/scripts' / name, scripts / name)
        settings = json.loads((ROOT / 'tools/server-package/server-settings.json').read_text())
        ports = set()
        while len(ports) < 4:
            with socket.socket() as sock:
                sock.bind(('127.0.0.1', 0))
                ports.add(sock.getsockname()[1])
        settings.update(zip(('csPort', 'steamPort', 'minecraftPort', 'bridgePort'), sorted(ports)))
        settings.update(map='cs_demo', botTotalPlayers=0, bindAddress='127.0.0.1')
        (self.base / 'server-settings.json').write_text(json.dumps(settings))
        mods = self.base / 'minecraft/mods'
        mods.mkdir(parents=True)
        with zipfile.ZipFile(mods / 'goldcraft-neoforge-0.1.0-dev.jar', 'w') as jar:
            jar.writestr('dev/goldcraft/net/CsAdmission.class', b'preflight fixture; never executed')
        (mods.parent / 'server.properties').write_text(
            'online-mode=true\nwhite-list=false\nenforce-whitelist=false\nenable-rcon=false\nmax-players=32\n')
        self.records = []
        for name in ('cs_demo', 'cs_second'):
            bsp = self.base / 'cs/cstrike/maps' / (name + '.bsp')
            bsp.parent.mkdir(parents=True, exist_ok=True)
            bsp.write_bytes(name.encode())
            dimension = f'{name}_{zlib.crc32(bsp.read_bytes()):08x}'
            dim = self.dimension_file(dimension)
            dim.parent.mkdir(parents=True, exist_ok=True)
            dim.write_text('{}')
            self.records.append({'map': name, 'dimension': 'goldcraft:' + dimension,
                'bspSha256': hashlib.sha256(bsp.read_bytes()).hexdigest()})

    def tearDown(self):
        self.temp.cleanup()

    def dimension_file(self, name):
        return self.base / 'minecraft/goldcraft-worlds/datapacks/goldcraft_host_maps/data/goldcraft/dimension' / (name + '.json')

    def check(self, records):
        (self.base / 'maps.json').write_text(json.dumps(records))
        shell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        return subprocess.run([str(shell), '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
            str(self.base / 'scripts/Start-Servers.ps1'), '-CheckOnly'], cwd=self.base,
            capture_output=True, text=True, timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)

    def test_one_and_multiple_maps_pass_actual_startup_preflight(self):
        for count in (1, 2):
            with self.subTest(maps=count):
                result = self.check(self.records[:count])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('no server was launched', result.stdout)
                self.assertFalse((self.base / '.state/cs.json').exists())
                self.assertFalse((self.base / '.state/minecraft.json').exists())

    def test_later_map_dimension_and_bsp_mismatch_are_rejected(self):
        second = self.records[1]
        dim = self.dimension_file(second['dimension'].split(':')[1])
        dim.unlink()
        result = self.check(self.records)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Map cs_second differs', result.stdout)
        dim.write_text('{}')
        (self.base / 'cs/cstrike/maps/cs_second.bsp').write_bytes(b'changed BSP')
        result = self.check(self.records)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Map cs_second differs', result.stdout)


if __name__ == '__main__':
    unittest.main()
