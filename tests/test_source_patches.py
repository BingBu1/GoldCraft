"""Exercise published patch replay against real scratch Git repositories."""
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from Modpack import safe


def module(filename):
    spec = importlib.util.spec_from_file_location(filename, ROOT / "tools" / (filename + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


exporter = module("Export-SourcePatches")
verifier = module("Verify-SourcePatches")


class SourcePatchTests(unittest.TestCase):
    def setUp(self):
        base = safe(ROOT / "build/source-patch-tests", ROOT / "build")
        base.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=base)
        self.root = safe(Path(self.temp.name), base)
        self.source = self.root / "external/addon"
        self.source.mkdir(parents=True)
        (self.root / "patches").mkdir()
        self.git("init", "-q")
        self.git("config", "core.autocrlf", "false")
        (self.source / "changed.txt").write_text("before\n", encoding="utf-8")
        (self.source / "removed.txt").write_text("obsolete\n", encoding="utf-8")
        self.git("add", ".")
        self.git("-c", "user.name=Patch test", "-c", "user.email=patch-test@example.invalid", "commit", "-qm", "fixture")
        self.entry = {"commit": self.git("rev-parse", "HEAD").strip(), "path": "external/addon",
                      "patch": "patches/addon.patch", "newFiles": ["added.txt"]}
        (self.root / "sources.lock.json").write_text(json.dumps({"sources": {"Addon": self.entry}}), encoding="utf-8")
        (self.source / "changed.txt").write_text("after\n", encoding="utf-8")
        (self.source / "removed.txt").unlink()
        (self.source / "added.txt").write_text("new content\n", encoding="utf-8")
        self.previous_root = verifier.ROOT
        verifier.ROOT = self.root

    def tearDown(self):
        verifier.ROOT = self.previous_root
        safe(self.root, ROOT / "build/source-patch-tests")
        self.temp.cleanup()

    def git(self, *args):
        return subprocess.check_output(["git", *args], cwd=self.source, text=True)

    def export(self, entry=None):
        exporter.export(self.root, "Addon", entry or self.entry)

    def test_replay_add_change_delete_and_preserve_source(self):
        self.export()
        before = {p.name: p.read_bytes() for p in self.source.glob("*.txt")}
        verifier.main()
        report = json.loads((self.root / "analysis/publication/source-patches.json").read_text())[0]
        self.assertEqual((report["files"], report["added"], report["deleted"]), (3, 1, 1))
        self.assertEqual(before, {p.name: p.read_bytes() for p in self.source.glob("*.txt")})

    def test_reject_old_file_left_in_working_tree(self):
        self.export()
        (self.source / "removed.txt").write_text("obsolete\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "Deleted source still exists"):
            verifier.main()

    def test_reject_new_source_changed_after_export(self):
        self.export()
        (self.source / "added.txt").write_text("unpublished change\n", encoding="utf-8")
        with self.assertRaisesRegex(ValueError, "does not reproduce current source"):
            verifier.main()

    def test_reject_patch_missing_declared_new_source(self):
        self.export({**self.entry, "newFiles": []})
        with self.assertRaisesRegex(ValueError, "omits a declared new source"):
            verifier.main()

    def test_reject_wrong_revision_without_overwriting_patch(self):
        self.export()
        patch = self.root / self.entry["patch"]
        previous = patch.read_bytes()
        with self.assertRaisesRegex(RuntimeError, "unpinned"):
            self.export({**self.entry, "commit": "0" * 40})
        self.assertEqual(previous, patch.read_bytes())


if __name__ == "__main__":
    unittest.main()
