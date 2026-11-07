"""Exercise the public-source gate against a real, minimal Git index."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


PROJECT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("public_source", PROJECT / "tools/Check-PublicSource.py")
GATE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GATE)


class IndexTests(unittest.TestCase):
    def setUp(self):
        output = PROJECT / "build/publication-tests"
        output.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=output)
        self.root = Path(self.temporary.name)
        self.previous_root, self.previous_manifest = GATE.ROOT, GATE.MANIFEST
        GATE.ROOT, GATE.MANIFEST = self.root, self.root / "publication.json"
        self.git("init", "-q")
        self.write("publication.json", json.dumps({"format": 1,
                   "files": ["publication.json", "README.md"], "trees": {"native": [".cpp"]}}))
        self.write("README.md", "Reviewed source.\n")
        self.git("add", "publication.json", "README.md")

    def tearDown(self):
        GATE.ROOT, GATE.MANIFEST = self.previous_root, self.previous_manifest
        self.temporary.cleanup()

    def git(self, *arguments, data=None):
        return subprocess.check_output(["git", *arguments], cwd=self.root, input=data)

    def write(self, name, text):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def scan(self):
        previous = sys.argv
        try:
            sys.argv = ["Check-PublicSource.py", "--staged"]
            with contextlib.redirect_stdout(io.StringIO()):
                return GATE.main()
        finally:
            sys.argv = previous

    def test_unstaged_sources_and_manifest_do_not_enter_publication(self):
        self.write("native/pending.cpp", "unreviewed source")
        self.write("publication.json", "invalid unreviewed manifest")
        self.assertEqual(self.scan(), 0)
        self.assertEqual(GATE.candidates(GATE.index_paths()), ["README.md", "publication.json"])

    def test_tree_admits_reviewed_staged_file(self):
        self.write("native/reviewed.cpp", "int value = 1;\n")
        self.git("add", "native/reviewed.cpp")
        self.assertEqual(self.scan(), 0)

    def test_secret_check_reads_index_even_when_worktree_is_cleaned(self):
        self.write("native/reviewed.cpp", 'const char* key = "ghp_' + "a" * 36 + '";\n')
        self.git("add", "native/reviewed.cpp")
        self.write("native/reviewed.cpp", "int value = 1;\n")
        self.assertEqual(self.scan(), 1)

    def test_unstaged_deletion_does_not_change_reviewed_index(self):
        (self.root / "README.md").unlink()
        self.assertEqual(self.scan(), 0)

    def test_missing_explicit_file_is_rejected(self):
        self.git("rm", "--cached", "README.md")
        with self.assertRaisesRegex(ValueError, "Git index differs from allowlist"):
            self.scan()

    def test_unlisted_and_private_paths_are_rejected(self):
        self.write("build/private.cpp", "int value = 1;\n")
        self.git("add", "build/private.cpp")
        with self.assertRaisesRegex(ValueError, "Git index differs from allowlist"):
            self.scan()

    def test_symlink_mode_cannot_masquerade_as_source(self):
        blob = self.git("hash-object", "-w", "--stdin", data=b"../../private.cpp").decode().strip()
        self.git("update-index", "--add", "--cacheinfo", f"120000,{blob},native/link.cpp")
        with self.assertRaisesRegex(ValueError, "non-regular index entry"):
            self.scan()


if __name__ == "__main__":
    unittest.main()
