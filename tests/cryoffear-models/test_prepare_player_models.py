"""Real-asset preparation checks. Supply a private hash-pinned inventory.

COF_PLAYER_INVENTORY=analysis/.../police-models.json python -B <this file>
The original assets are never included in the repository.
"""
import copy
import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('prepare_cof_players', ROOT / 'tools/Prepare-CryOfFearPlayerModels.py')
PREPARE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PREPARE)


class RealPlayerModels(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        inventory = os.environ.get('COF_PLAYER_INVENTORY')
        if not inventory:
            raise RuntimeError('COF_PLAYER_INVENTORY is required; missing evidence is not a passed test')
        cls.inventory = json.loads(Path(inventory).read_text(encoding='utf-8-sig'))
        cls.source = Path(cls.inventory[0]['path']).parents[2]
        cls.players = [ROOT / f'sandbox/cs-server/Half-Life/cstrike/models/player/{name}/{name}.mdl'
                       for name in ('urban', 'csol95', 'csol168')]
        cls.build = ROOT / 'build' / 'cof-player-models-20261111'
        cls.build.mkdir(parents=True, exist_ok=True)

    def test_real_assets_remain_byte_identical_with_stock_and_cso_anchors(self):
        result = PREPARE.prepare(self.source, self.inventory, self.build / 'prepared', self.players)
        self.assertEqual(len(self.inventory), result['originalResources'])
        self.assertEqual(len(result['players']), 3)
        for entry, output in zip(self.inventory, result['resources']):
            self.assertEqual(Path(entry['path']).read_bytes(), (self.build / 'prepared' / output['path']).read_bytes())
        for player in result['players']:
            self.assertTrue(any(name.startswith('ref_aim_') for name in player['upperBodyActions']))
        for player in result['players'][1:]:
            for suffix in ('katana', 'skullaxe', 'skull8', 'carbine'):
                self.assertIn('ref_aim_' + suffix, player['upperBodyActions'])

    def test_changed_hash_rejected_before_any_asset_write(self):
        entries = copy.deepcopy(self.inventory)
        entries[-1]['sha256'] = '0' * 64
        with tempfile.TemporaryDirectory(dir=self.build) as directory:
            output = Path(directory) / 'assets'
            with self.assertRaisesRegex(ValueError, 'identity changed'):
                PREPARE.prepare(self.source, entries, output, [])
            self.assertFalse(output.exists())

    def test_empty_inventory_is_not_success(self):
        with self.assertRaisesRegex(ValueError, 'nonempty'):
            PREPARE.prepare(self.source, [], None, [])

    def test_world_model_is_not_accepted_as_player_model(self):
        data = bytearray(Path(self.inventory[0]['path']).read_bytes())
        model = PREPARE.Studio(data)
        anchor = next(i for i, bone in enumerate(model.bones) if bone[0] == PREPARE.ANCHOR)
        at = model.i(144) + anchor * 112
        data[at:at+32] = b'Unverified hand'.ljust(32, bytes([0]))
        with self.assertRaisesRegex(ValueError, 'right-hand anchor'):
            PREPARE.Studio(data).player_weapon()

    def test_geometry_not_following_hand_is_rejected(self):
        data = bytearray(Path(self.inventory[0]['path']).read_bytes())
        model = PREPARE.Studio(data)
        body = model.i(208)
        mesh_model = model.i(body + 72)
        data[model.i(mesh_model + 84)] = 0
        with self.assertRaisesRegex(ValueError, 'does not follow'):
            PREPARE.Studio(data).player_weapon()

    def test_outside_build_destination_rejected(self):
        with self.assertRaisesRegex(ValueError, 'build/'):
            PREPARE.prepare(self.source, self.inventory, ROOT / 'sandbox' / 'unexpected-player-output', [])

    def test_duplicate_output_rejected_before_write(self):
        entries = [self.inventory[0], self.inventory[0]]
        with tempfile.TemporaryDirectory(dir=self.build) as directory:
            output = Path(directory) / 'assets'
            with self.assertRaisesRegex(ValueError, 'Duplicate output'):
                PREPARE.prepare(self.source, entries, output, [])
            self.assertFalse(output.exists())

    def test_existing_unrelated_resource_is_preserved(self):
        with tempfile.TemporaryDirectory(dir=self.build) as directory:
            output = Path(directory)
            source = Path(self.inventory[0]['path'])
            destination = output / source.parent.name / source.name
            destination.parent.mkdir()
            destination.write_bytes(b'unrelated-owner')
            with self.assertRaisesRegex(ValueError, 'overwrite'):
                PREPARE.prepare(self.source, self.inventory[:1], output, [])
            self.assertEqual(destination.read_bytes(), b'unrelated-owner')


if __name__ == '__main__':
    unittest.main(verbosity=2)
