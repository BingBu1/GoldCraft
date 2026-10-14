"""Small resource fixtures; no game process, downloads or production writes."""
from pathlib import Path
import hashlib
import importlib.util
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
from ServerPackageResources import collect_resources, resource_path

ROOT = Path(__file__).resolve().parents[1]


class ConsistencyResources(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('package_builder', ROOT / 'tools/Build-ServerPackage.py')
        cls.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.builder)

    def setUp(self):
        directory = ROOT / 'analysis/server-package/resource-fixtures'
        directory.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in self.builder.CONSISTENCY_ASSETS:
            path = self.root / 'cstrike' / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(b'fixture asset')

    def test_scope_and_nested_tga_survive_filter_without_runtime_logs(self):
        nested = self.root / 'valve/sprites/custom'
        nested.mkdir(parents=True)
        (nested / 'effect.TGA').write_bytes(b'custom texture')
        (nested / 'private.log').write_bytes(b'not an asset')
        plan = self.builder.game_assets(self.root)
        for name in ('scope_arc', 'scope_arc_nw', 'scope_arc_ne', 'scope_arc_sw'):
            self.assertEqual(plan[f'cs/cstrike/sprites/{name}.tga'].read_bytes(), b'fixture asset')
        self.assertIn('cs/valve/sprites/custom/effect.TGA', plan)
        self.assertNotIn('cs/valve/sprites/custom/private.log', plan)

    def test_missing_server_digest_input_rejects_package(self):
        (self.root / 'cstrike/sprites/scope_arc_sw.tga').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing or empty consistency resource: .*scope_arc_sw.tga'):
            self.builder.game_assets(self.root)

    def test_empty_digest_input_rejects_package(self):
        (self.root / 'cstrike/sprites/black_smoke1.spr').write_bytes(b'')
        with self.assertRaisesRegex(ValueError, 'Missing or empty consistency resource: .*black_smoke1.spr'):
            self.builder.game_assets(self.root)

    def write_asset(self, name, data=b'asset'):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def test_complete_resource_trees_and_future_formats(self):
        assets = ('cstrike/gfx/uis/logo.png', 'cstrike/renderer/shader/example.h',
                  'cstrike/renderer/shader/example.glsl', 'cstrike/sprites/lighting.hdr',
                  'cstrike/captionmod/lang.csv', 'cstrike/captionmod/font.ttf',
                  'cstrike/overviews/cs_demo.bmp', 'cstrike/futuremod/effects/unknown.newformat',
                  'valve/media/intro.webm', 'valve/resource/dialog.res', 'cstrike/custom.wad', 'valve/pak0.pak')
        for name in assets:
            self.write_asset(name)
        plan = self.builder.game_assets(self.root)
        for name in assets:
            self.assertIn('cs/' + name, plan)

    def test_downloads_fallback_and_primary_precedence(self):
        self.write_asset('cstrike_downloads/sprites/scope_arc.tga', b'different')
        self.write_asset('cstrike_downloads/models/remote.mdl')
        self.write_asset('cstrike_downloads/downloaded.wad')
        self.write_asset('valve_downloads/models/not-mounted.mdl')
        plan = self.builder.game_assets(self.root)
        self.assertEqual(plan['cs/cstrike/sprites/scope_arc.tga'].read_bytes(), b'fixture asset')
        self.assertIn('cs/cstrike/models/remote.mdl', plan)
        self.assertIn('cs/cstrike/downloaded.wad', plan)
        self.assertFalse(any('not-mounted' in name or '_downloads/' in name for name in plan))

    def test_private_generated_and_build_inputs_excluded(self):
        names = ('cstrike/config.cfg', 'cstrike/screen.bmp', 'cstrike/cached.wad',
                 'cstrike/tempdecal.wad', 'cstrike/models/model.blend', 'cstrike/gfx/.env',
                 'cstrike/logs/server.txt', 'cstrike/save/world.dat', 'cstrike/cache/file.bin',
                 'cstrike/addons/configs/sql.cfg', 'cstrike/dlls/mp.dll',
                 'cstrike/renderer/cache/shader.bin', 'cstrike/models/backup/a.mdl',
                 'cstrike/models/helper.ps1', 'cstrike/sound/example.wav.ztmp')
        for name in names:
            self.write_asset(name)
        plan = self.builder.game_assets(self.root)
        for name in names:
            self.assertNotIn('cs/' + name, plan)

    @unittest.skipUnless(os.name == 'nt', 'Windows junction protection')
    def test_reparse_directory_rejected_before_traversal(self):
        target = self.root / 'outside'
        target.mkdir()
        link = self.root / 'cstrike/gfx'
        # Both literal paths are inside this disposable workspace fixture.
        result = subprocess.run(['powershell.exe', '-NoProfile', '-Command',
            "New-Item -ItemType Junction -Path '" + str(link) + "' -Target '" + str(target) + "' | Out-Null"],
            capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        try:
            with self.assertRaisesRegex(ValueError, 'Reparse resource directory'):
                self.builder.game_assets(self.root)
        finally:
            os.rmdir(link)

    def test_old_or_incapable_core_rejected(self):
        path = self.root / 'core.jar'
        for marker in (None, {'csAdmissionVersion': 1, 'offlineCsAdmission': False},
                       {'csAdmissionVersion': 1, 'offlineCsAdmission': 'true'}):
            with zipfile.ZipFile(path, 'w') as jar:
                jar.writestr('dev/goldcraft/net/CsAdmission.class', b'fixture')
                if marker is not None:
                    jar.writestr('META-INF/goldcraft-capabilities.json', json.dumps(marker))
            with self.assertRaisesRegex(ValueError, 'offline CS admission'):
                self.builder.check_core(path)

    def test_unsafe_resource_paths_rejected(self):
        for name in ('../x.wav', '/x.wav', 'C:/x.wav', 'sound/../x.wav',
                     'sound//x.wav', 'sound/x.wav:private', 'config/secrets.json'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                resource_path(name)


@unittest.skipUnless(os.name == 'nt', 'Windows dual-server verifier')
class ConsoleFeedback(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec = importlib.util.spec_from_file_location('package_verifier', ROOT / 'tools/Test-ServerPackage.py')
        verifier = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(verifier)
        cls.feedback = staticmethod(verifier.server_say_feedback)

    def test_complete_vanilla_server_announcement(self):
        # Prefix copied from the actual 1.21.1/NeoForge live07 server log.
        prefix = '[1310\u67082026 18:30:16.714] [Server thread/INFO] [net.minecraft.server.MinecraftServer/]: '
        line = prefix + '[Server] GC_PACKAGE_CLEARED_fixture_1'
        self.assertEqual(self.feedback(line + '\r\n', 'GC_PACKAGE_CLEARED_fixture_1'), line)

    def test_console_ack_and_echo_do_not_prove_execution(self):
        marker = 'GC_PACKAGE_CLEARED_fixture_1'
        for log in ('command acknowledged: ' + marker,
                    'execute if block 0 80 0 air run say ' + marker,
                    '[Server thread/INFO] [dev.goldcraft/]: [Server] ' + marker):
            with self.subTest(log=log):
                self.assertIsNone(self.feedback(log, marker))

    def test_unsigned_dedicated_console_announcement(self):
        # Exact announcement from archived77-multimaps01; console say is unsigned.
        marker = 'GC_PACKAGE_LOADED_gc_package_reset_1c0023a006104dddbccbc5c1a6653aef_0'
        line = ('[1310\u67082026 20:45:13.960] [Server thread/INFO] '
                '[net.minecraft.server.MinecraftServer/]: [Not Secure] [Server] ' + marker)
        self.assertEqual(self.feedback(line + '\n', marker), line)
        for logger in ('net.minecraft.server.MinecraftServer/', 'minecraft/MinecraftServer'):
            line = f'[20:45:13] [Server thread/INFO] [{logger}]: [Not Secure] [Server] {marker}'
            self.assertEqual(self.feedback(line, marker), line)

    def test_unsigned_prefix_does_not_accept_echoes_or_partial_tokens(self):
        marker = 'GC_PACKAGE_CLEARED_fixture_1'
        prefix = '[20:45:13] [Server thread/INFO] [net.minecraft.server.MinecraftServer/]: '
        for text in ('[Not Secure] execute run say ' + marker,
                     '[Not Secure] [Server] ' + marker + '_extra',
                     '[Not Secure] [Server] ' + marker + ' failed',
                     '[Not Secure] [Not Secure] [Server] ' + marker,
                     '[anything] [Server] ' + marker):
            with self.subTest(text=text):
                self.assertIsNone(self.feedback(prefix + text, marker))
        line = prefix + '[Not Secure] [Server] ' + marker
        for old, new in (('Server thread/INFO', 'Server thread/WARN'),
                         ('Server thread/INFO', 'Render thread/INFO'),
                         ('net.minecraft.server.MinecraftServer/', 'goldcraft/')):
            self.assertIsNone(self.feedback(line.replace(old, new), marker))

    def test_partial_or_different_token_rejected(self):
        prefix = '[18:30:16] [Server thread/INFO] [net.minecraft.server.MinecraftServer/]: [Server] '
        marker = 'GC_PACKAGE_CLEARED_fixture_1'
        for value in (marker[:-1], marker + '_extra', marker + ' failed'):
            with self.subTest(value=value):
                self.assertIsNone(self.feedback(prefix + value, marker))

    def test_warning_and_wrong_thread_rejected(self):
        marker = 'GC_PACKAGE_CLEARED_fixture_1'
        for source in ('Server thread/WARN', 'Render thread/INFO'):
            log = f'[18:30:16] [{source}] [net.minecraft.server.MinecraftServer/]: [Server] {marker}'
            self.assertIsNone(self.feedback(log, marker))


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

    def test_downloaded_map_wad_and_new_resource_format(self):
        original = self.bsp(texture=True)
        downloads = self.base / 'cstrike_downloads'
        (downloads / 'maps').mkdir(parents=True)
        bsp = downloads / 'maps/cs_remote.bsp'
        bsp.write_bytes(original.read_bytes())
        (downloads / 'used.wad').write_bytes(b'WAD3' + struct.pack('<ii', 1, 12) + b'\0' * 16 + b'wall\0'.ljust(16, b'\0'))
        (downloads / 'custom').mkdir()
        (downloads / 'custom/asset.xyz').write_bytes(b'asset')
        bsp.with_suffix('.res').write_text('custom/asset.xyz\n', encoding='utf-8')
        self.assertEqual(select_maps(self.game, ['cs_remote'], 'cs_remote'), [bsp])
        resources, _ = self.resources(bsp)
        self.assertIn('cs/cstrike/used.wad', resources)
        self.assertIn('cs/cstrike/custom/asset.xyz', resources)

    def test_valve_res_fallback_is_parsed(self):
        bsp = self.bsp()
        (self.valve / 'maps').mkdir()
        (self.valve / 'maps/cs_demo.res').write_text('custom.asset\n', encoding='utf-8')
        (self.valve / 'custom.asset').write_bytes(b'asset')
        resources, _ = self.resources(bsp)
        self.assertIn('cs/valve/custom.asset', resources)

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
            jar.writestr('META-INF/goldcraft-capabilities.json', json.dumps(
                {'csAdmissionVersion': 1, 'offlineCsAdmission': True}))
        (mods.parent / 'server.properties').write_text(
            'online-mode=false\nenforce-secure-profile=false\nwhite-list=false\nenforce-whitelist=false\nenable-rcon=false\nmax-players=32\n')
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

    def test_online_mode_remains_optional_and_offline_requires_no_signed_profile(self):
        props = self.base / 'minecraft/server.properties'
        original = props.read_text()
        props.write_text(original.replace('online-mode=false', 'online-mode=true'))
        result = self.check(self.records)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        props.write_text(original.replace('enforce-secure-profile=false', 'enforce-secure-profile=true'))
        result = self.check(self.records)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('enforce-secure-profile=false', result.stdout)

    def test_startup_rejects_old_core_before_launch(self):
        with zipfile.ZipFile(self.base / 'minecraft/mods/goldcraft-neoforge-0.1.0-dev.jar', 'w') as jar:
            jar.writestr('dev/goldcraft/net/CsAdmission.class', b'old fixture')
        result = self.check(self.records)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('lacks offline CS admission', result.stdout)


if __name__ == '__main__':
    unittest.main()
