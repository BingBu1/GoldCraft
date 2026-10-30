#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the production candidate manifest path policy without deployment."""
import importlib.util
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('csol_candidate', ROOT/'tools/Build-CSOLCandidate.py')
CANDIDATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(CANDIDATE)


class ResourcePaths(unittest.TestCase):
    def test_music_paths(self):
        for name in ('bgm/Scenario_Ready.mp3', 'bgm/Scenario_Normal.mp3',
                     'bgm/stage2/Scenario Rush.mp3', 'BGM/Scenario_Rush.MP3'):
            with self.subTest(name=name):
                self.assertEqual(CANDIDATE.relative_resource(name), Path(name))

    def test_music_directory_does_not_admit_other_file_types(self):
        for name in ('bgm', 'bgm/server.cfg', 'bgm/payload.dll', 'bgm/payload.exe',
                     'bgm/music.wav', 'bgm/music.ogg', 'bgm/music.mp3.exe',
                     'bgm/music.mp3.json', 'bgm/stage2/no_extension'):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    CANDIDATE.relative_resource(name)

    def test_existing_destination_boundaries(self):
        for name in ('', 'bgm/../server.mp3', 'bgm/../../sound/file.mp3',
                     '../bgm/track.mp3', '/bgm/track.mp3', 'C:/bgm/track.mp3',
                     'C:bgm/track.mp3', '//host/share/bgm/track.mp3',
                     'bgm\\track.mp3', 'bgm/track.mp3:payload',
                     'bgm2/track.mp3', 'addons/track.mp3', 'server.cfg'):
            with self.subTest(name=name):
                with self.assertRaises(ValueError):
                    CANDIDATE.relative_resource(name)

    def test_existing_resource_roots_remain_available(self):
        for name in ('models/v_beamgunle.mdl', 'sprites/beamgunle_gauge_fx.webm',
                     'sound/weapons/fire.wav', 'materials/weapon/base.tga',
                     'resource/UI/Scenario.txt', 'resource/font/df_gb_y9.ttf', 'maps/zs_deadend.bsp',
                     'events/cannon.sc', 'gfx/tattoo/tattoo.cso'):
            with self.subTest(name=name):
                self.assertEqual(CANDIDATE.relative_resource(name), Path(name))


if __name__ == '__main__':
    unittest.main(verbosity=2)
