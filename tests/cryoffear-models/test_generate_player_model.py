# SPDX-License-Identifier: GPL-3.0-or-later
"""Replay three real V models through explicitly unreviewed test calibration.

Set COF_MODEL_INVENTORY and COF_PLAYER_INVENTORY to private evidence JSONs.
Generated *.candidate.mdl files are geometry fixtures, never approved grips.
"""
import copy
import hashlib
import importlib.util
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from cryoffear_studio import Studio, inverse, multiply, transform
SPEC = importlib.util.spec_from_file_location('generate_cof_player', ROOT / 'tools/Generate-CryOfFearPlayerModel.py')
GENERATOR = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GENERATOR)


class RealGeometry(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        inputs = os.environ.get('COF_MODEL_INVENTORY'), os.environ.get('COF_PLAYER_INVENTORY')
        if not all(inputs):
            raise RuntimeError('Both private model inventories are required; missing evidence is not success')
        inventory = json.loads(Path(inputs[0]).read_text(encoding='utf-8-sig'))['models']
        p_inventory = json.loads(Path(inputs[1]).read_text(encoding='utf-8-sig'))
        template_entry = next(row for row in p_inventory if Path(row['path']).name == 'p_glock.mdl')
        cls.template = Studio.read(template_entry['path'])
        cls.template_path = template_entry['path']
        if cls.template.sha256 != template_entry['sha256']:
            raise ValueError('Template evidence identity changed')
        cls.output_root = ROOT / 'build/cof-player-generation-20261111'
        cls.output_root.mkdir(parents=True, exist_ok=True)
        cls.output = Path(tempfile.mkdtemp(prefix='geometry-', dir=cls.output_root))
        print('artifact_directory=' + str(cls.output))
        cls.proof = cls.output / 'test-calibration-evidence.txt'
        cls.proof.write_text('TEST ONLY: identity wrist-coordinate fixture. No reviewed CS grip or real-game acceptance. Never deploy.\n', encoding='utf-8')
        cls.inputs = {}
        cls.source_paths = {}
        texture_names = {
            'axe': {'skin.bmp'},
            'famas': {'famas1.bmp', 'famas2.bmp', 'bullet.bmp'},
            # Two actual magazine triangles use glove.bmp. Bone allowlisting
            # must retain those while excluding all hand/glove triangles.
            'mp5': {'glove.bmp', 'null.bmp', 'Buttstock.bmp', 'Receiver.bmp', 'Barrel.bmp', 'barrelR.bmp'},
        }
        vertex_bones = {'axe': ['axe'], 'famas': ['fam_frame', 'fam_trigger', 'fam_charginghandle', 'fam_muzzle', 'fam_bolt', 'fam_mag', 'fam_bullets', 'fam_bulletgay'],
                        'mp5': ['bone_weapon', 'bone_bolt', 'bone_mag']}
        tp = cls.template.sample(0, 0, 0)
        target = cls.template.bone_index('SHOTGUN')
        hand = cls.template.bone_index('Bip01 R Hand')
        # A declared test-only semantic identity. This is not a discovered
        # or approved mapping between the two games' wrist coordinate axes.
        fixture_matrix = multiply(inverse(tp[target]), tp[hand])
        for name in ('axe', 'famas', 'mp5'):
            entry = next(row for row in inventory if Path(row['path']).name == f'v_{name}.mdl')
            source = Studio.read(entry['path'])
            if source.sha256 != entry['sha256']:
                raise ValueError('Source evidence identity changed')
            sequence = next(i for i in range(source.i(164)) if source.string(source.i(168) + 176*i, 32) == 'idle')
            calibration = dict(schema=1, status='candidate', attachments='all',
                evidence=dict(path=str(cls.proof), sha256=hashlib.sha256(cls.proof.read_bytes()).hexdigest()),
                source=dict(sha256=source.sha256, bone='r_wr', sequence=dict(index=sequence, label='idle', frame=0, blend=0)),
                target=dict(sha256=cls.template.sha256, bone='SHOTGUN', handBone='Bip01 R Hand',
                            sequence=dict(index=0, label='idle', frame=0, blend=0)),
                sourceAnchorToTargetBone=fixture_matrix,
                textures=[dict(name=t['name'], sha256=t['sha256']) for t in source.textures if t['name'] in texture_names[name]],
                sourceVertexBones=vertex_bones[name])
            cls.inputs[name] = source, calibration
            cls.source_paths[name] = entry['path']

    def test_three_real_models_preserve_selected_triangles_normals_uv_pixels_and_attachments(self):
        metrics = []
        for name, (source, calibration) in self.inputs.items():
            with self.subTest(model=name):
                output, receipt = GENERATOR.generate(source, self.template, calibration, candidate=True)
                rebuilt = Studio(output)
                self.assertTrue(receipt['candidate'])
                self.assertFalse(receipt['deliverable'])
                self.assertEqual(rebuilt.i(164), 1)
                self.assertEqual(rebuilt.i(rebuilt.i(168) + 56), 1)
                self.assertGreater(receipt['selectedTriangles'], 0)
                self.assertGreater(receipt['excludedTriangles'], 0)
                self.assertEqual(len(rebuilt.attachments), len(source.attachments))
                source_pose = source.sample(calibration['source']['sequence']['index'], 0, 0)
                source_anchor = source.bone_index('r_wr')
                transforms = [multiply(calibration['sourceAnchorToTargetBone'], multiply(inverse(source_pose[source_anchor]), p)) for p in source_pose]
                target = rebuilt.bone_index('SHOTGUN')
                allowed = set(calibration['sourceVertexBones'])
                white = {(t['name'], t['sha256']) for t in calibration['textures']}
                expected = []
                for body in source.bodyparts:
                    for model in body['models']:
                        for mesh in model['meshes']:
                            textures = [source.textures[skin[mesh['skinref']]] for skin in source.skins]
                            if not all((t['name'], t['sha256']) in white for t in textures):
                                continue
                            for triangle in mesh['triangles']:
                                if any(source.bones[model['verticesBones'][c[0]]]['name'] not in allowed for c in triangle):
                                    continue
                                expected.append((textures[0]['sha256'], tuple((transform(transforms[model['verticesBones'][v]], model['vertices'][v]),
                                    transform(transforms[model['normalsBones'][n]], model['normals'][n], normal=True), s, t) for v, n, s, t in triangle)))
                actual = []
                for body in rebuilt.bodyparts:
                    for model in body['models']:
                        self.assertTrue(all(i == target for i in model['verticesBones'] + model['normalsBones']))
                        normal_start = 0
                        for mesh in model['meshes']:
                            self.assertTrue(all(normal_start <= c[1] < normal_start + mesh['normalCount']
                                                for triangle in mesh['triangles'] for c in triangle))
                            normal_start += mesh['normalCount']
                            texture = rebuilt.textures[rebuilt.skins[0][mesh['skinref']]]
                            for triangle in mesh['triangles']:
                                actual.append((texture['sha256'], tuple((model['vertices'][v], model['normals'][n], s, t) for v, n, s, t in triangle)))
                        self.assertEqual(normal_start, len(model['normals']))
                self.assertEqual(len(expected), len(actual))
                max_position_error = max_normal_error = 0
                for before, after in zip(expected, actual):
                    self.assertEqual(before[0], after[0])
                    for a, b in zip(before[1], after[1]):
                        self.assertEqual(a[2:], b[2:])
                        max_position_error = max(max_position_error, math.dist(a[0], b[0]))
                        max_normal_error = max(max_normal_error, math.dist(a[1], b[1]))
                self.assertLess(max_position_error, 0.00001)
                self.assertLess(max_normal_error, 0.000001)
                max_attachment_error = 0
                for original, new in zip(source.attachments, rebuilt.attachments):
                    self.assertEqual((new['name'], new['type'], new['bone']), (original['name'], original['type'], target))
                    t = transforms[original['bone']]
                    max_attachment_error = max(max_attachment_error, math.dist(transform(t, original['origin']), new['origin']))
                    for a, b in zip(original['vectors'], new['vectors']):
                        self.assertLess(math.dist(transform(t, a, normal=True), b), 0.000001)
                self.assertLess(max_attachment_error, 0.00001)
                # Every retained indexed image/palette and its flags survives.
                for texture in rebuilt.textures:
                    original = next(t for t in source.textures if t['name'] == texture['name'] and t['sha256'] == texture['sha256'])
                    self.assertEqual((texture['pixels'], texture['flags']), (original['pixels'], original['flags']))
                if name == 'mp5':
                    glove = next(i for i, t in enumerate(rebuilt.textures) if t['name'] == 'glove.bmp')
                    glove_triangles = sum(len(m['triangles']) for b in rebuilt.bodyparts for v in b['models'] for m in v['meshes'] if rebuilt.skins[0][m['skinref']] == glove)
                    self.assertEqual(glove_triangles, 2)
                (self.output / f'{name}.candidate.mdl').write_bytes(output)
                (self.output / f'{name}.calibration.candidate.json').write_text(json.dumps(calibration, indent=2), encoding='utf-8')
                (self.output / f'{name}.receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
                metrics.append(dict(model=name, triangles=len(actual), bytes=len(output), positionError=max_position_error,
                                    normalError=max_normal_error, attachmentError=max_attachment_error, sha256=receipt['outputSha256']))
        (self.output / 'geometry-validation.json').write_text(json.dumps(metrics, indent=2), encoding='utf-8')

    def reject(self, mutate, error):
        source, original = self.inputs['axe']
        calibration = copy.deepcopy(original)
        mutate(calibration)
        with self.assertRaisesRegex(ValueError, error):
            GENERATOR.generate(source, self.template, calibration, candidate=True)

    def test_unreviewed_calibration_cannot_become_delivery_resource(self):
        source, calibration = self.inputs['axe']
        with self.assertRaisesRegex(ValueError, 'only generate a candidate'):
            GENERATOR.generate(source, self.template, calibration)

    def test_explicit_nonzero_reload_frame_is_baked_then_frozen(self):
        source, original = self.inputs['famas']
        calibration = copy.deepcopy(original)
        sequence = next(i for i in range(source.i(164)) if source.string(source.i(168) + 176*i, 32) == 'reload')
        # Original famas reload[5], frame35 has a measured 31.866-unit
        # magazine-origin displacement relative to the right wrist. Frame55
        # has already reseated it and is not a useful animation counterexample.
        calibration['source']['sequence'].update(index=sequence, label='reload', frame=35)
        output, _ = GENERATOR.generate(source, self.template, calibration, candidate=True)
        idle, _ = GENERATOR.generate(source, self.template, original, candidate=True)
        self.assertNotEqual(output, idle)
        baked, original_idle = Studio(output), Studio(idle)
        self.assertEqual([t['sha256'] for t in baked.textures], [t['sha256'] for t in original_idle.textures])
        self.assertEqual(baked.i(baked.i(168) + 56), 1)
        positions = [v for b in baked.bodyparts for m in b['models'] for v in m['vertices']]
        idle_positions = [v for b in original_idle.bodyparts for m in b['models'] for v in m['vertices']]
        self.assertGreater(max(math.dist(a, b) for a, b in zip(positions, idle_positions)), 1)

    def test_wrong_source_sha(self):
        self.reject(lambda c: c['source'].update(sha256='0'*64), 'SHA-256')

    def test_wrong_texture_sha(self):
        self.reject(lambda c: c['textures'][0].update(sha256='0'*64), 'Texture identity')

    def test_wrong_frame_label_and_bone(self):
        self.reject(lambda c: c['source']['sequence'].update(label='draw'), 'index and label')
        self.reject(lambda c: c['source']['sequence'].update(frame=999999), 'frame or blend')
        self.reject(lambda c: c['source'].update(bone='unproved wrist'), 'Missing or ambiguous bone')

    def test_nonrigid_and_reflected_calibration_rejected(self):
        self.reject(lambda c: c.update(sourceAnchorToTargetBone=[[2, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]), 'scale/shear')
        self.reject(lambda c: c.update(sourceAnchorToTargetBone=[[-1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0]]), 'Mirroring')
        self.reject(lambda c: c['sourceAnchorToTargetBone'][0].__setitem__(3, float('nan')), 'finite')

    def test_no_automatic_material_or_bone_fallback(self):
        self.reject(lambda c: c.update(textures=[]), 'texture allowlist')
        self.reject(lambda c: c.update(sourceVertexBones=[]), 'vertex-bone allowlist')
        self.reject(lambda c: c.update(sourceVertexBones=['r_wr']), 'No approved weapon triangles')

    def test_attachment_omission_and_bad_proof_rejected(self):
        self.reject(lambda c: c.update(attachments='none'), 'all attachments')
        self.reject(lambda c: c['evidence'].update(sha256='0'*64), 'evidence identity')

    def command(self, output, report, calibration, candidate):
        args = [sys.executable, '-B', str(ROOT / 'tools/Generate-CryOfFearPlayerModel.py'),
                '--source-model', self.source_paths['axe'], '--template-model', self.template_path,
                '--calibration', str(calibration), '--output', str(output), '--report', str(report)]
        if candidate:
            args.append('--candidate')
        return subprocess.run(args, capture_output=True, text=True, encoding='utf-8', timeout=30)

    def test_actual_cli_writes_replayable_candidate_and_preserves_inputs(self):
        source, calibration = self.inputs['axe']
        config = self.output / 'cli-calibration.json'
        config.write_text(json.dumps(calibration), encoding='utf-8')
        output, report = self.output / 'cli-axe.candidate.mdl', self.output / 'cli-axe.json'
        result = self.command(output, report, config, True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        receipt = json.loads(report.read_text(encoding='utf-8'))
        self.assertTrue(receipt['candidate'])
        self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), receipt['outputSha256'])
        self.assertEqual(hashlib.sha256(Path(self.source_paths['axe']).read_bytes()).hexdigest(), source.sha256)
        self.assertEqual(hashlib.sha256(Path(self.template_path).read_bytes()).hexdigest(), self.template.sha256)
        # A second invocation must refuse an existing artifact rather than
        # truncating it, including a resource another owner might have linked.
        again = self.command(output, report, config, True)
        self.assertNotEqual(again.returncode, 0)
        self.assertEqual(hashlib.sha256(output.read_bytes()).hexdigest(), receipt['outputSha256'])

    def test_cli_rejects_unsafe_target_and_unreviewed_delivery(self):
        _, calibration = self.inputs['axe']
        config = self.output / 'rejected-calibration.json'
        config.write_text(json.dumps(calibration), encoding='utf-8')
        output, report = self.output / 'not-approved.mdl', self.output / 'not-approved.json'
        result = self.command(output, report, config, False)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(output.exists())
        self.assertFalse(report.exists())
        outside = ROOT / 'sandbox/cof-model-tool-must-not-write.candidate.mdl'
        self.assertFalse(outside.exists())
        result = self.command(outside, report, config, True)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(outside.exists())

    def test_truncated_model_invalid_vertex_and_invalid_rle_rejected(self):
        source, calibration = self.inputs['axe']
        with self.assertRaises(ValueError):
            Studio(source.data[:-1])
        data = bytearray(source.data)
        body = source.i(208)
        model = source.i(body + 72)
        data[source.i(model + 84)] = 255
        with self.assertRaisesRegex(ValueError, 'geometry bone'):
            Studio(data)
        data = bytearray(source.data)
        seq = source.i(168) + 176*calibration['source']['sequence']['index']
        animation = source.i(seq + 124)
        offset = next((12*i + 2*axis for i in range(len(source.bones)) for axis in range(6)
                       if struct.unpack_from('<H', data, animation + 12*i + 2*axis)[0]), None)
        self.assertIsNotNone(offset)
        table = animation + (offset // 12)*12
        channel = table + struct.unpack_from('<H', data, animation + offset)[0]
        data[channel:channel + 2] = bytes(2)
        with self.assertRaisesRegex(ValueError, 'animation RLE'):
            Studio(data).sample(calibration['source']['sequence']['index'], 0, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
