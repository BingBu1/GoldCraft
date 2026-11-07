#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run with python tests/csol-character-repack/test_repack.py.

Exercises the real staging entry point and file hashes with temporary inputs.
Only the packer subprocess is simulated; no game resources or executables run.
"""
from __future__ import annotations

import contextlib
import copy
import hashlib
import io
import json
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT / "tools/Repack-CSOLCharacterModels.py"


def model(labels):
    data = bytearray(244 + 176 * len(labels))
    data[:4] = b"IDST"
    struct.pack_into("<i", data, 4, 10)
    struct.pack_into("<i", data, 72, len(data))
    struct.pack_into("<ii", data, 164, len(labels), 244)
    for index, label in enumerate(labels):
        encoded = label.encode("ascii")
        data[244 + index * 176:244 + index * 176 + len(encoded)] = encoded
    return bytes(data)


class RepackTests(unittest.TestCase):
    def setUp(self):
        self.original_tool = TOOL.read_bytes()
        self.main = runpy.run_path(str(TOOL))["main"]
        self.globals = self.main.__globals__
        # Runtime scratch stays in the workspace; this directory contains no
        # sources or real assets and TemporaryDirectory removes it after each case.
        scratch = ROOT / "build/csol-character-repack-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        for parent in (scratch, *scratch.parents):
            if parent.is_symlink() or parent.is_junction():
                raise ValueError(f"Test output traverses link: {parent}")
        self.temp = tempfile.TemporaryDirectory(prefix="case-", dir=scratch)
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.globals["ROOT"] = self.root
        (self.root / "build").mkdir()
        self.catalog = self.root / "native/csol/config/characters.json"
        self.catalog.parent.mkdir(parents=True)
        self.catalog.write_text(json.dumps({"characters": [
            {"model": "a", "sourceModel": "models/player/a/a.mdl"},
            {"model": "b", "sourceModel": "models/player/b/b.mdl"},
        ]}), encoding="utf-8")
        source = self.root / "original/models/player/a/a.mdl"
        source.parent.mkdir(parents=True)
        source.write_bytes(model(["idle"]))
        sequences = [source.with_name(f"a{i}.seq") for i in (1, 2)]
        for path, content in zip(sequences, (b"walk", b"shoot")):
            path.write_bytes(content)
        baseline = self.root / "installed/models/player/a/a.mdl"
        baseline.parent.mkdir(parents=True)
        baseline.write_bytes(model(["idle", "walk", "shoot"]))
        self.packer = self.root / "fake-pack.exe"
        self.packer.write_bytes(b"Not executable; never launched")
        identity = self.globals["identity"]
        self.row = {"alias": "a", "source": identity(source),
                    "sequences": [identity(p) for p in sequences],
                    "derived": identity(baseline)}
        self.inputs = [self.catalog, self.packer, source, baseline, *sequences]
        self.input_hashes = {p: hashlib.sha256(p.read_bytes()).digest() for p in self.inputs}
        self.calls = []
        self.output = self.root / "build/output"

    def tearDown(self):
        self.assertEqual(TOOL.read_bytes(), self.original_tool)
        for path, digest in self.input_hashes.items():
            self.assertEqual(hashlib.sha256(path.read_bytes()).digest(), digest, str(path))

    def invoke(self, case, run=False, aliases=()):
        rows = [copy.deepcopy(self.row)]
        catalog = self.globals["identity"](self.catalog)
        if case == "wrong-catalog":
            catalog["sha256"] = "0" * 64
        elif case == "duplicate-selection":
            rows *= 2
        elif case == "reversed-order":
            rows[0]["sequences"].reverse()
        elif case in ("wrong-source", "wrong-derived"):
            other = self.root / "unrelated.mdl"
            other.write_bytes(model(["idle"]))
            field = "source" if case == "wrong-source" else "derived"
            rows[0][field] = self.globals["identity"](other)
        elif case == "existing-output":
            self.output.mkdir()
        elif case == "outside-output":
            self.output = self.root / "outside"
        corpus = self.root / "corpus.json"
        corpus.write_text(json.dumps({"catalog": catalog, "rows": rows}), encoding="utf-8")

        def pack(command, **kwargs):
            self.calls.append(command)
            self.assertEqual(command[0], str(self.packer))
            self.assertEqual(command[1], "--extended")
            self.assertEqual(command[3:], [self.row["source"]["path"],
                                          *(r["path"] for r in self.row["sequences"])])
            target = Path(command[2])
            self.assertEqual(target, self.output / "models/player/a/a.mdl")
            target.parent.mkdir(parents=True)
            labels = ["idle", "shoot", "walk"] if case == "changed-label" else ["idle", "walk", "shoot"]
            target.write_bytes(model(labels))
            if case == "timeout":
                raise subprocess.TimeoutExpired(command, 120, output=b"partial stdout", stderr=b"partial stderr")
            return types.SimpleNamespace(returncode=7 if case == "packer-failed" else 0,
                                         stdout=b"controlled stdout", stderr=b"controlled stderr")

        fake = types.SimpleNamespace(run=pack, CREATE_NO_WINDOW=0,
                                     TimeoutExpired=subprocess.TimeoutExpired)
        argv = [str(TOOL), "--corpus", str(corpus), "--packer", str(self.packer),
                "--output", str(self.output)]
        for alias in aliases:
            argv += ["--alias", alias]
        if run:
            argv.append("--run")
        with patch.dict(self.globals, {"subprocess": fake}), patch.object(sys, "argv", argv), \
                contextlib.redirect_stdout(io.StringIO()):
            return self.main()

    def rejection(self, case, message, aliases=()):
        with self.assertRaisesRegex(ValueError, message):
            self.invoke(case, aliases=aliases)
        self.assertEqual(self.calls, [])

    def retained_failure(self, case, message, log):
        self.assertEqual(self.invoke(case, run=True), 1)
        self.assertEqual(len(self.calls), 1)
        result = json.loads((self.output / "result.json").read_text("utf-8"))
        self.assertFalse(result["success"])
        self.assertFalse(result["deployed"])
        self.assertIn(message, result["error"])
        self.assertEqual((self.output / "a.log").read_bytes(), log)
        self.assertTrue((self.output / "plan.json").is_file())
        self.assertTrue((self.output / "models/player/a/a.mdl").is_file())

    def test_valid(self):
        self.assertEqual(self.invoke("valid", run=True), 0)
        result = json.loads((self.output / "result.json").read_text("utf-8"))
        self.assertTrue(result["success"])
        self.assertTrue(result["inputsUnchanged"])
        self.assertFalse(result["deployed"])
        self.assertEqual([m["alias"] for m in result["models"]], ["a"])
        self.assertEqual(result["models"][0]["sequences"], 3)
        target = self.output / "models/player/a/a.mdl"
        self.assertEqual(target.read_bytes()[8:72], b"a.mdl".ljust(64, b"\0"))
        self.assertEqual(result["models"][0]["output"]["sha256"], hashlib.sha256(target.read_bytes()).hexdigest())

    def test_wrong_catalog(self):
        self.rejection("wrong-catalog", "different character catalog")

    def test_duplicate_selection(self):
        self.rejection("duplicate-selection", "missing or duplicate", aliases=("a", "b"))

    def test_reversed_order(self):
        self.rejection("reversed-order", "Sequence order/source mismatch")

    def test_wrong_source(self):
        self.rejection("wrong-source", "does not belong to alias")

    def test_wrong_derived(self):
        self.rejection("wrong-derived", "does not belong to alias")

    def test_changed_label(self):
        self.retained_failure("changed-label", "Sequence semantics/order differ",
                              b"controlled stdoutcontrolled stderr")

    def test_packer_failed(self):
        self.retained_failure("packer-failed", "Packer rejected", b"controlled stdoutcontrolled stderr")

    def test_existing_output(self):
        self.rejection("existing-output", "new staging directory")

    def test_outside_output(self):
        self.rejection("outside-output", "new staging directory")

    def test_timeout_partial_output(self):
        self.retained_failure("timeout", "timed out", b"partial stdoutpartial stderr")


if __name__ == "__main__":
    unittest.main(verbosity=2)
