# SPDX-License-Identifier: GPL-3.0-or-later
"""Check authored third-person geometry against the two original Studio models."""
from importlib import import_module
import json
import math
import os
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from cryoffear_studio import Studio, inverse, multiply, transform

author = import_module("Author-CryOfFearFamasPlayer")


class FamasPlayer(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        inventory = json.loads(Path(os.environ["COF_MODEL_INVENTORY"]).read_text("utf-8-sig"))
        path = next(r["path"] for r in inventory["models"] if Path(r["path"]).name == "w_famas.mdl")
        cls.world = Studio.read(path)
        cls.reference = Studio.read(os.environ.get(
            "COF_CS_PLAYER_MODEL", ROOT / "sandbox/cs-server/Half-Life/cstrike/models/p_famas.mdl"))
        data, cls.receipt = author.build(cls.world, cls.reference)
        cls.result = Studio(data)

    def test_full_geometry_on_stock_hand_branch(self):
        world, stock, model = self.world, self.reference, self.result
        hand, child = model.bone_index("Bip01 R Hand"), model.bone_index("p_famas")
        self.assertEqual(model.bones[child]["parent"], hand)
        self.assertEqual(model.textures, world.textures)
        self.assertEqual(model.skins, world.skins)
        self.assertEqual(self.receipt["excludedTriangles"], 0)
        self.assertEqual([(b["name"], b["parent"]) for b in model.bones],
                         [(b["name"], b["parent"]) for b in stock.bones])
        pose = model.sample(0, 0, 0)
        mount = multiply(inverse(pose[hand]), pose[child])
        for frame in range(stock.i(stock.i(168) + 56)):
            ref_pose = stock.sample(0, frame, 0)
            ref_mount = multiply(inverse(ref_pose[hand]), ref_pose[child])
            for a, b in zip(mount, ref_mount):
                self.assertLess(math.dist(a, b), 1e-7)
        matrix = self.receipt["sourceToReference"]
        triangles = 0
        self.assertEqual(len(model.bodyparts), 2)
        for a, b in zip(world.bodyparts, model.bodyparts, strict=True):
            self.assertEqual(a["name"], b["name"])
            for source, result in zip(a["models"], b["models"], strict=True):
                self.assertEqual(source["name"], result["name"])
                self.assertEqual(len(source["vertices"]), len(result["vertices"]))
                self.assertEqual(set(result["verticesBones"] + result["normalsBones"]), {child})
                for sm, rm in zip(source["meshes"], result["meshes"], strict=True):
                    self.assertEqual(sm["skinref"], rm["skinref"])
                    for old, new in zip(sm["triangles"], rm["triangles"], strict=True):
                        triangles += 1
                        for (v,n,s,t), (vv,nn,ss,tt) in zip(old,new):
                            self.assertEqual((s,t), (ss,tt))
                            self.assertLess(math.dist(transform(matrix, source["vertices"][v]),
                                                      result["vertices"][vv]), 1e-5)
                            self.assertLess(math.dist(transform(matrix, source["normals"][n], normal=True),
                                                      result["normals"][nn]), 1e-6)
        self.assertEqual(triangles, 3451)

    def test_attachment_uses_the_complete_rim_in_the_resident_model(self):
        model = self.result
        self.assertEqual((len(self.world.attachments), len(model.attachments)), (0, 1))
        muzzle, = model.attachments
        self.assertEqual((muzzle["name"], muzzle["bone"]), ("muzzle", model.bone_index("p_famas")))
        self.assertEqual(model.i(216) + 88, model.i(188))
        barrel = self.world.bodyparts[0]["models"][0]
        ring = (836, 835, 861, 828, 824, 823, 864, 816, 808, 807, 865, 840)
        edges = {frozenset((tri[i][0],tri[(i+1)%3][0])) for mesh in barrel["meshes"]
                 for tri in mesh["triangles"] for i in range(3)}
        self.assertTrue(all(frozenset((v,ring[(i+1)%len(ring)])) in edges for i,v in enumerate(ring)))
        center = tuple(sum(barrel["vertices"][v][axis] for v in ring)/len(ring) for axis in range(3))
        self.assertLess(math.dist(muzzle["origin"], transform(self.receipt["sourceToReference"], center)), 1e-5)
        self.assertEqual(model.sha256, "a113f1531c331790ce9e0860013f57ba9f9cfe3d46e025138f1c83a840342635")

    def test_world_model_cannot_stand_in_for_the_cs_rig(self):
        with self.assertRaisesRegex(ValueError, "reviewed identities"):
            author.build(self.world, self.world)


if __name__ == "__main__":
    unittest.main(verbosity=2)
