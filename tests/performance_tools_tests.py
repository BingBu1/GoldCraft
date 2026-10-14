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


class ParameterStreamGuards(unittest.TestCase):
    def test_stream_counts_and_per_frame_mode(self):
        state = {'rendererDrawParameterStreamAvailable': True, 'rendererDrawParameterStream': 1}
        frame = dict(drawParameterStream=1, drawParameterUploads=9, drawParameterBytes=720,
                     drawParameterFallbacks=2, drawParameterBusyFallbacks=1, drawParameterSyncFailures=0)
        def stable(rows, snapshots=None, active=1):
            return monitor.stable_studio_feature(snapshots or [{'native':state}], rows, active, active, 'DrawParameterStream')
        self.assertEqual(monitor.feature_key('DrawParameterStream'), 'drawParameterStream')
        self.assertTrue(stable([frame]))
        for key in frame:
            row=dict(frame); del row[key]
            self.assertFalse(stable([row]), key)
        for key,value in [('drawParameterBytes',8),('drawParameterBusyFallbacks',3),
                          ('drawParameterSyncFailures',1),('drawParameterUploads',True),('drawParameterUploads',-1)]:
            self.assertFalse(stable([dict(frame,**{key:value})]),key)
        self.assertFalse(stable([frame,dict(frame,drawParameterStream=0)]))
        self.assertFalse(stable([frame],[{'native':{}}]))
        zero=dict.fromkeys(monitor.PARAMETER_COUNTS,0);zero['drawParameterStream']=0
        off=[{'native':dict(state,rendererDrawParameterStream=0)}]
        self.assertTrue(stable([zero],off,0))
        self.assertFalse(stable([dict(zero,drawParameterFallbacks=1)],off,0))

    def test_stream_old_runtime_cannot_satisfy_request(self):
        self.assertTrue(monitor.stable_studio_feature([{'native':{}}],[{}],None,None,'DrawParameterStream'))
        self.assertFalse(monitor.stable_studio_feature([{'native':{}}],[{}],None,1,'DrawParameterStream'))

    def test_stream_manifest_must_retain_original(self):
        data={'schema':1,'run':'light-perf-123','player':{'dimension':'goldcraft:cs_assault_12345678'},
              'original':{'drawParameterStream':0},'drawParameterStreamModes':[0,1],
              'position':[0,0,0],'anchor':{'viewAngles':[0,0,0]},'blocks':[]}
        lights.validate_recovery(data)
        for value in (True,-1,2,'0'):
            with self.assertRaises(ValueError): lights.validate_recovery(dict(data,original={'drawParameterStream':value}))
        with self.assertRaises(ValueError): lights.validate_recovery(dict(data,original={}))

    def test_stream_restore_preserves_other_modes_and_rejects_new_process(self):
        exercise=object.__new__(lights.Exercise); actions=[]
        exercise.monitor=monitor;exercise.renderer=SimpleNamespace(command=actions.append)
        identity={'pid':123,'birth100ns':456}
        exercise.data={'original':{'caps':lights.NORMAL_CAPS,'staticShadows':True,'drawParameterStream':0},
                       'processes':{'client':identity}}
        state=dict(lights.NORMAL_CAPS,benchmarkFramerate=False,sceneStaticShadowsEnabled=True,
                   rendererDrawParameterStreamAvailable=True,rendererDrawParameterStream=0)
        with patch.object(lights,'process_identity',return_value=identity), patch.object(lights,'fresh',return_value=state):
            exercise.restore_options()
        self.assertEqual(actions,['benchmark_framerate 0','scene_static_shadows 1','render_draw_parameter_stream 0'])
        self.assertTrue(exercise.data['optionsRestored']);actions.clear()
        with patch.object(lights,'process_identity',return_value={'pid':123,'birth100ns':789}):
            with self.assertRaises(RuntimeError):exercise.restore_options()
        self.assertEqual(actions,[])


class ShadowPlanGuards(unittest.TestCase):
    def test_plan_requires_complete_counts_and_executor(self):
        state = {'rendererStudioShadowPlanAvailable': True, 'rendererStudioShadowPlan': 1}
        frame = dict.fromkeys(monitor.PLAN_COUNTS, 0)
        frame.update(studioShadowPlan=1,studioDepthExecutor=1,studioShadowPlanReuses=10)
        def check(row): return monitor.stable_studio_feature([{'native':state}],[row],1,1,'ShadowPlan')
        self.assertTrue(check(frame))
        for key in monitor.PLAN_COUNTS:
            row=dict(frame);del row[key];self.assertFalse(check(row))
        self.assertFalse(check(dict(frame,studioDepthExecutor=0)))
        self.assertFalse(check(dict(frame,studioShadowPlan=0)))
        self.assertFalse(check(dict(frame,studioShadowPlanReuses=True)))
        self.assertFalse(monitor.stable_studio_feature([{'native':{}}],[{}],None,1,'ShadowPlan'))
        off = dict.fromkeys(monitor.PLAN_COUNTS, 0)
        off.update(studioShadowPlan=0, studioDepthExecutor=0)
        self.assertTrue(monitor.stable_studio_feature(
            [{'native':dict(state,rendererStudioShadowPlan=0)}], [off], 0, None, 'ShadowPlan'))
        self.assertFalse(monitor.stable_studio_feature(
            [{'native':dict(state,rendererStudioShadowPlan=0)}], [off], 0, 0, 'ShadowPlan'))

    def test_plan_restore_and_recovery(self):
        data={'schema':1,'run':'light-perf-123','player':{'dimension':'goldcraft:cs_assault_12345678'},
              'original':{'studioShadowPlan':0},'studioShadowPlanModes':[0,1],
              'position':[0,0,0],'anchor':{'viewAngles':[0,0,0]},'blocks':[]}
        lights.validate_recovery(data)
        with self.assertRaises(ValueError): lights.validate_recovery(dict(data,original={}))
        exercise=object.__new__(lights.Exercise); actions=[]
        exercise.monitor=monitor;exercise.renderer=SimpleNamespace(command=actions.append)
        identity={'pid':123,'birth100ns':456}
        exercise.data={'original':dict(data['original'],caps=lights.NORMAL_CAPS,staticShadows=True),
                       'processes':{'client':identity}}
        state=dict(lights.NORMAL_CAPS,benchmarkFramerate=False,sceneStaticShadowsEnabled=True,
                   rendererStudioShadowPlanAvailable=True,rendererStudioShadowPlan=0)
        with patch.object(lights,'process_identity',return_value=identity),patch.object(lights,'fresh',return_value=state):
            exercise.restore_options()
        self.assertEqual(actions,['benchmark_framerate 0','scene_static_shadows 1','render_studio_shadow_plan 0'])


if __name__ == '__main__': unittest.main()
