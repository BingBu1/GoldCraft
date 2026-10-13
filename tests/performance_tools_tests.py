"""Offline acceptance/rollback guards; no game or performance measurements."""
import importlib.util
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT/'tools'/f'{name}.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


monitor = load('Monitor-LivePerformance')
lights = load('Exercise-LightPerformance')


class Guards(unittest.TestCase):
    def test_legacy_names_remain_compatible(self):
        for feature in ('FaceCull', 'DepthExecutor', 'BonePaletteCache'):
            self.assertEqual(monitor.feature_name(feature), 'Studio'+feature)
            self.assertEqual(monitor.feature_key(feature), 'studio'+feature)
        self.assertEqual(monitor.feature_key('WorldMaterialBatch'), 'worldMaterialBatch')

    def test_partial_or_mixed_capture_is_rejected(self):
        state = {'rendererWorldMaterialBatchAvailable': True, 'rendererWorldMaterialBatch': 1}
        frame = {'worldMaterialBatch': 1, 'worldMaterialChains': 12,
                 'worldMaterialSubmissions': 2, 'worldMaterialBatches': 2}
        stable = lambda samples, frames, active=1: monitor.stable_studio_feature(
            samples, frames, active, 1, 'WorldMaterialBatch')
        self.assertTrue(stable([{'native':state}], [frame]))
        self.assertFalse(stable([{'native':state}], [frame, dict(frame,worldMaterialBatch=0)]))
        self.assertFalse(stable([{'native':{}}], [frame]))
        for key in frame:
            broken = dict(frame); del broken[key]
            self.assertFalse(stable([{'native':state}], [broken]))
        for key, value in [('worldMaterialChains',13), ('worldMaterialSubmissions',1),
                           ('worldMaterialBatches',3), ('worldMaterialBatches',True)]:
            self.assertFalse(stable([{'native':state}], [dict(frame,**{key:value})]))
        self.assertFalse(monitor.stable_studio_feature([{'native':dict(state,rendererWorldMaterialBatch=0)}],
            [dict(frame,worldMaterialBatch=0)],0,0,'WorldMaterialBatch'))

    def test_old_runtime_only_accepted_without_request(self):
        self.assertTrue(monitor.stable_studio_feature([{'native':{}}],[{}],None,None,'WorldMaterialBatch'))
        self.assertFalse(monitor.stable_studio_feature([{'native':{}}],[{}],None,1,'WorldMaterialBatch'))

    def test_recovery_requires_original_value(self):
        data = {'schema':1,'run':'light-perf-123','player':{'dimension':'goldcraft:cs_assault_12345678'},
                'original':{'worldMaterialBatch':0},'worldMaterialBatchModes':[0,1],
                'position':[0,0,0],'anchor':{'viewAngles':[0,0,0]},'blocks':[]}
        lights.validate_recovery(data)
        for value in (True, -1, 2, '0'):
            with self.assertRaises(ValueError):
                lights.validate_recovery(dict(data,original={'worldMaterialBatch':value}))
        with self.assertRaises(ValueError): lights.validate_recovery(dict(data,original={}))

    def test_restore_and_restarted_client_guard(self):
        exercise = object.__new__(lights.Exercise)
        actions = []
        exercise.monitor = monitor
        exercise.renderer = SimpleNamespace(command=actions.append)
        exercise.data = {'original':{'caps':lights.NORMAL_CAPS,'staticShadows':True,'worldMaterialBatch':0},
                         'processes':{'client':{'pid':123,'birth100ns':456}}}
        state = dict(lights.NORMAL_CAPS,benchmarkFramerate=False,sceneStaticShadowsEnabled=True,
                     rendererWorldMaterialBatchAvailable=True,rendererWorldMaterialBatch=0)
        with patch.object(lights,'process_identity',return_value={'pid':123,'birth100ns':456}), \
             patch.object(lights,'fresh',return_value=state):
            exercise.restore_options()
        self.assertEqual(actions,['benchmark_framerate 0','scene_static_shadows 1','render_world_material_batch 0'])
        self.assertTrue(exercise.data['optionsRestored'])
        actions.clear()
        with patch.object(lights,'process_identity',return_value={'pid':123,'birth100ns':789}):
            with self.assertRaises(RuntimeError): exercise.restore_options()
        self.assertEqual(actions,[])


if __name__ == '__main__': unittest.main()
