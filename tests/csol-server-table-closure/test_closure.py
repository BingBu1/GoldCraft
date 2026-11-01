"""Packaging checks; no compiler, game or protected installation access."""
from pathlib import Path
import hashlib
import importlib.util
import json
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('server_closure', ROOT / 'tools/csol_server_table_closure.py')
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def row(name, data):
    return dict(path=name, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


class ClosureTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory(dir=ROOT / 'build', prefix='table-closure-test-')
        self.root = Path(self.directory.name)
        self.table = 'cs/cstrike/resource/maplist.csv'
        self.wave = 'cs/cstrike/maps/zbs_wave_zs_deadend.cso'

    def tearDown(self):
        self.directory.cleanup()

    def archive(self, name, resources, legacy=False, required=(), corrupt=None, extra=()):
        path = self.root / name
        records = [row(('payload/' if legacy else '') + key, data) for key, data in resources.items()]
        if legacy:
            records.append(row('Update-Server.ps1', b'installer'))
        records.extend(extra)
        with zipfile.ZipFile(path, 'w') as archive:
            archive.writestr('package/update-manifest.json', json.dumps(dict(files=records, requiredBaseline=list(required))))
            if legacy:
                archive.writestr('package/Update-Server.ps1', b'installer')
            for key, data in resources.items():
                archive.writestr('package/payload/' + key, corrupt if corrupt is not None else data)
        return path

    def closure(self, resources):
        return dict(resources=[dict(resource=name.removeprefix('cs/cstrike/'), groups=['server-tables'],
                                   source=str(self.root / name), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())
                               for name, data in resources.items()])

    def test_legacy_script_is_not_payload(self):
        archive = self.archive('old.zip', {self.table: b'catalog'}, legacy=True)
        entries, _ = module.archive_index(archive)
        self.assertEqual(list(entries), [self.table])

    def test_missing_map_catalog_is_explicit(self):
        archive = self.archive('old.zip', {self.wave: b'12345678'}, legacy=True)
        result = module.audit(self.closure({self.table: b'catalog', self.wave: b'12345678'}), [archive])
        self.assertEqual([r['path'] for r in result['files']], [self.table])
        self.assertEqual(result['differences'][0]['reason'], 'missing')
        self.assertEqual(result['verifiedArchiveResources'], [self.wave])

    def test_update_supersedes_baseline_bytes(self):
        old = self.archive('old.zip', {self.table: b'old'}, legacy=True)
        update = self.archive('new.zip', {self.table: b'new'})
        result = module.audit(self.closure({self.table: b'new'}), [old, update])
        self.assertEqual(result['files'], [])
        self.assertEqual(result['verifiedArchiveResources'], [self.table])

    def test_old_catalog_is_a_change_not_an_omission(self):
        old = self.archive('old.zip', {self.table: b'old'}, legacy=True)
        result = module.audit(self.closure({self.table: b'new'}), [old])
        self.assertEqual(result['filesCount'], 1)
        self.assertEqual(result['differences'][0]['reason'], 'changed')

    def test_missing_required_baseline_rejected(self):
        update = self.archive('new.zip', {}, required=[row(self.table, b'old')])
        with self.assertRaisesRegex(ValueError, 'Required baseline identity'):
            module.audit(self.closure({self.table: b'old'}), [update])

    def test_conflicting_required_baseline_rejected(self):
        old = self.archive('old.zip', {self.table: b'wrong'}, legacy=True)
        update = self.archive('new.zip', {}, required=[row(self.table, b'expected')])
        with self.assertRaisesRegex(ValueError, 'Required baseline identity'):
            module.audit(self.closure({self.table: b'expected'}), [old, update])

    def test_real_archive_bytes_are_verified(self):
        old = self.archive('old.zip', {self.table: b'good'}, legacy=True, corrupt=b'evil')
        with self.assertRaisesRegex(ValueError, 'Archive resource bytes'):
            module.audit(self.closure({self.table: b'good'}), [old])

    def test_declared_but_missing_member_rejected(self):
        old = self.archive('old.zip', {}, extra=[row(self.table, b'good')])
        with self.assertRaisesRegex(ValueError, 'Manifest entry absent'):
            module.archive_index(old)

    def test_case_collision_rejected(self):
        old = self.archive('old.zip', {self.table: b'good'}, extra=[row(self.table.upper(), b'good')])
        with self.assertRaisesRegex(ValueError, 'Duplicate archive destination'):
            module.archive_index(old)

    def test_traversal_and_windows_special_paths_rejected(self):
        for name in ('../maps/a', '/maps/a', 'maps//a', 'C:/a', 'maps\\a', 'maps/a ', 'maps/a.', 'maps/a\n', 'maps/a*', 'maps/a|'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                module.relative(name)

    def test_frozen_source_drift_is_not_silently_accepted(self):
        source = self.root / 'loader.cpp'
        source.write_text('new loader no longer uses the old table', 'utf-8')
        with self.assertRaisesRegex(ValueError, 'Frozen source contract changed'):
            module.literal(self.root, source.name, 'resource/maplist.csv', {})

    def test_missing_music_reader_fails_closed(self):
        with self.assertRaisesRegex(ValueError, 'requires --music-reader'):
            module.music_paths(self.root, self.root, 'zs_deadend', None, {})

    def test_bgm_collects_round_and_trigger_tracks(self):
        source = self.root / 'source'
        for name, content in {
                'server/scenario_music.cpp': 'PrecacheFile("Scenario_Ready.mp3"',
                'server/scenario_bgm_trigger.cpp': 'music->PrecacheFile(m_filename',
                'scenario_music.cpp': 'ResourcePath', 'music_resources.cpp': 'ResourcePath',
                'scenario/round_settings.cpp': 'namespace', 'scenario/config.cpp': 'namespace'}.items():
            file = source / 'native/csol' / name
            file.parent.mkdir(parents=True, exist_ok=True)
            file.write_text(content)
        (self.root / 'maps').mkdir()
        (self.root / 'maps/zbs_test.cso').write_bytes(b'round-data')
        entities = b'{"classname" "trigger_bgm" "filename" "special/boss.mp3"}\0'
        bsp = bytearray(124)
        struct.pack_into('<iii', bsp, 0, 30, 124, len(entities))
        (self.root / 'maps/test.bsp').write_bytes(bsp + entities)
        reader = self.root / 'reader.exe'
        reader.write_bytes(b'identity-only-test-stub')
        calls = []

        def run(argv, **kwargs):
            calls.append((argv[1], kwargs['input']))
            output = b'bgm/Scenario_Ready.mp3\nbgm/Scenario_Rush.mp3\n' if argv[1] == 'round' else b'bgm/special/boss.mp3\n'
            return type('Output', (), {'stdout': output})()
        with patch.object(module.subprocess, 'run', run):
            paths = module.music_paths(source, self.root, 'test', reader, {})
        self.assertEqual(paths, {'bgm/Scenario_Ready.mp3', 'bgm/Scenario_Rush.mp3', 'bgm/special/boss.mp3'})
        self.assertEqual(calls, [('round', b'round-data'), ('path', b'special/boss.mp3')])

    def test_generated_plan_retains_identity(self):
        asset = self.root / 'generated.tga'
        asset.write_bytes(b'rgba-generated')
        plan = self.root / 'plan.json'
        plan.write_text(json.dumps({'files': [dict(destination='resource/levelup.tga',
            source=str(asset), bytes=14, sha256=hashlib.sha256(b'rgba-generated').hexdigest())]}))
        self.assertEqual(list(module.plan_resources(plan))[0][0], 'resource/levelup.tga')
        asset.write_bytes(b'changed-source')
        with self.assertRaisesRegex(ValueError, 'source changed'):
            list(module.plan_resources(plan))

    def test_blocked_plan_is_not_accepted(self):
        plan = self.root / 'plan.json'
        plan.write_text(json.dumps({'blocked': ['missing model'], 'files': []}))
        with self.assertRaisesRegex(ValueError, 'blocked'):
            list(module.plan_resources(plan))

    def test_overlay_resolves_compiled_resource_header(self):
        old = self.root / 'source/native/csol/header.hpp'
        old.parent.mkdir(parents=True)
        old.write_text('old')
        new = self.root / 'patched.hpp'
        new.write_text('new')
        overlay = self.root / 'overlay.json'
        overlay.write_text(json.dumps({'roots': [dict(type='file', name='H:/repo/native/csol/header.hpp',
            **{'external-contents': str(new)})]}))
        tree = module.SourceTree(self.root / 'source', overlay)
        self.assertEqual((tree / 'native/csol/header.hpp').read_text(), 'new')

    def test_reslist_ignores_inline_models(self):
        text = '1. 3 : models/a.mdl 1.23K abcdef01 FATALIFMISSING\n2. 4 : *1 0.00K 00000000 -\n2 Total model\'s\n'
        self.assertEqual(module.runtime_resource_paths(text, 'model'), {'models/a.mdl'})

    def test_reslist_sound_prefix_and_spaces(self):
        text = '1. 3 : ambience/room wind.wav 1.23K 00000000 -\n1 Total sound\'s\n'
        self.assertEqual(module.runtime_resource_paths(text, 'sound'), {'sound/ambience/room wind.wav'})

    def test_truncated_reslist_is_not_complete_coverage(self):
        text = '1. 3 : models/a.mdl 1.23K abcdef01 -\n2 Total model\'s\n'
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            module.runtime_resource_paths(text, 'model')

    def test_missing_bgm_and_upgrade_are_not_hidden_by_prior_table_fix(self):
        old = self.archive('tables.zip', {self.table: b'catalog'})
        expected = {self.table: b'catalog', 'cs/cstrike/bgm/Scenario_Ready.mp3': b'mp3',
                    'cs/cstrike/resource/levelup.tga': b'rgba'}
        result = module.audit(self.closure(expected), [old])
        self.assertEqual({r['path'] for r in result['files']}, set(expected) - {self.table})


if __name__ == '__main__':
    unittest.main()
