"""Packaging checks; no compiler, game or protected installation access."""
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import unittest
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


if __name__ == '__main__':
    unittest.main()
