"""Exercise mounted HUD leaves and fail-closed, identity-bound source overrides."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/'tools'))
from csol_resource_sources import ResourceSources


class SourceTests(unittest.TestCase):
    def setUp(self):
        (ROOT/'build').mkdir(exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='resource-source-test-', dir=ROOT/'build')
        self.root = Path(self.temp.name)
        self.common = self.root/'common'
        self.base = self.write('common/sprites/640hud6.spr', b'old reviewed base')
        self.candidate = self.write('reviewed/sprites/640hud6.spr', b'correct reviewed atlas')

    def tearDown(self):
        self.temp.cleanup()

    def write(self, name, data):
        path = self.root/name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def manifest(self, **changes):
        row = dict(relative='sprites/640hud6.spr', source=str(self.candidate),
                   bytes=self.candidate.stat().st_size, sha256=hashlib.sha256(self.candidate.read_bytes()).hexdigest(),
                   expectedBaseBytes=self.base.stat().st_size, expectedBaseSha256=hashlib.sha256(self.base.read_bytes()).hexdigest())
        row.update(changes)
        return self.write('overrides.json', (json.dumps({'version':1,'overrides':[row]})+'\n').encode())

    def test_mount_order_for_hud_leaf(self):
        self.write('cstrike_chn/sprites/640hud6.spr', b'Chinese atlas')
        self.write('valve/sprites/640hud6.spr', b'Valve fallback')
        sources = ResourceSources(self.common)
        self.assertEqual(sources.read('sprites/640hud6.spr'), b'Chinese atlas')
        self.write('locale_chn/sprites/640hud6.spr', b'locale override')
        self.assertEqual(ResourceSources(self.common).read('sprites/640hud6.spr'), b'locale override')

    def test_model_graph_is_not_remounted(self):
        self.write('common/models/v_test.mdl', b'collector model')
        self.write('locale_chn/models/v_test.mdl', b'different graph')
        self.assertEqual(ResourceSources(self.common).read('models/v_test.mdl'), b'collector model')

    def test_bound_override_records_both_sources(self):
        sources = ResourceSources(self.common, self.manifest())
        self.assertEqual(sources.read('sprites/640hud6.spr'), self.candidate.read_bytes())
        sources.finish()
        row = sources.records['sprites/640hud6.spr']
        self.assertTrue(row['override'])
        self.assertEqual(row['base']['path'], str(self.base))
        self.assertEqual(row['path'], str(self.candidate))

    def test_changed_current_base_rejected(self):
        path = self.manifest()
        self.base.write_bytes(b'new unreviewed base')
        with self.assertRaisesRegex(ValueError, 'current-base identity changed'):
            ResourceSources(self.common, path).read('sprites/640hud6.spr')

    def test_changed_candidate_rejected(self):
        path = self.manifest()
        self.candidate.write_bytes(b'new unreviewed atlas')
        with self.assertRaisesRegex(ValueError, 'candidate identity changed'):
            ResourceSources(self.common, path).read('sprites/640hud6.spr')

    def test_wrong_relative_path_not_consumed(self):
        sources = ResourceSources(self.common, self.manifest(relative='sprites/640hud7.spr'))
        self.assertEqual(sources.read('sprites/640hud6.spr'), self.base.read_bytes())
        with self.assertRaisesRegex(ValueError, 'not consumed'):
            sources.finish()

    def test_model_override_rejected(self):
        with self.assertRaisesRegex(ValueError, 'independent HUD atlas'):
            ResourceSources(self.common, self.manifest(relative='models/v_test.mdl'))

    def test_missing_base_identity_rejected(self):
        with self.assertRaisesRegex(ValueError, 'current-base identities'):
            ResourceSources(self.common, self.manifest(expectedBaseSha256=None))

    def test_source_changed_between_reads_rejected(self):
        sources = ResourceSources(self.common)
        sources.read('sprites/640hud6.spr')
        self.base.write_bytes(b'changed during preparation')
        with self.assertRaisesRegex(ValueError, 'changed during preparation'):
            sources.read('sprites/640hud6.spr')

    def test_path_escape_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Invalid source resource'):
            ResourceSources(self.common).read('../outside.spr')


if __name__ == '__main__':
    unittest.main()
