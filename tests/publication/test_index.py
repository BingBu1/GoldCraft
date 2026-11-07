"""Exercise the public-source gate against a real, minimal Git index."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


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

    def add_blob(self, name, data):
        oid = self.git("hash-object", "-w", "--stdin", data=data).strip()
        # Index-only paths also exercise names that Windows cannot create.
        self.git("update-index", "-z", "--index-info",
                 data=b"100644 " + oid + b"\t" + name.encode("utf-8") + b"\0")
        return oid

    def test_batch_preserves_duplicate_empty_unicode_and_newline_paths(self):
        self.git("config", "core.protectNTFS", "false")
        data = "int 中文 = 3;\n".encode("utf-8")
        paths = {"native/z.cpp": data, "native/a.cpp": data,
                 "native/空\n--batch.cpp": b"", "native/tab\tname.cpp": b""}
        for name, content in paths.items():
            self.add_blob(name, content)
        original = subprocess.Popen
        with mock.patch.object(GATE.subprocess, "Popen", wraps=original) as processes:
            self.assertEqual(self.scan(), 0)
        commands = [call.args[0] for call in processes.call_args_list]
        self.assertEqual(sum(args[1:] == ["cat-file", "--batch"] for args in commands), 1)
        self.assertFalse(any("show" in args for args in commands))
        report = json.loads((self.root / "build/publication/git-index-scan.json").read_text())
        records = {entry["path"]: entry for entry in report["files"]}
        self.assertEqual(list(records), sorted(records))
        for name, content in paths.items():
            self.assertEqual(records[name]["bytes"], len(content))
            self.assertEqual(records[name]["sha256"], hashlib.sha256(content).hexdigest())

    def test_missing_and_nonblob_objects_are_rejected(self):
        tree = self.git("write-tree").strip()
        for oid in (b"f" * 40, tree):
            with self.subTest(oid=oid):
                with self.assertRaisesRegex(ValueError, "Missing, non-blob"):
                    GATE.index_blobs([oid])

    def test_large_blob_is_rejected_without_unbounded_pipe_allocation(self):
        oid = self.add_blob("native/large.cpp", b" " * 2_000_001)
        with self.assertRaisesRegex(ValueError, "unexpectedly large"):
            GATE.index_blobs([oid])

    def test_batch_rejects_wrong_order_size_content_and_trailing_output(self):
        data = b"source\n"
        oid = self.git("hash-object", "-w", "--stdin", data=data).strip()
        other = self.git("hash-object", "-w", "--stdin", data=b"other").strip()
        header = oid + b" blob 7\n"
        responses = [other + b" blob 7\n" + data + b"\n",
                     oid + b" blob -1\n", oid + b" blob 07\n",
                     header + data, header + b"tamper\n\n",
                     header + data + b"\nextra"]
        for response in responses:
            with self.subTest(response=response):
                process = mock.MagicMock()
                process.__enter__.return_value = process
                process.stdin = io.BytesIO()
                process.stdout = io.BytesIO(response)
                process.wait.return_value = 0
                with mock.patch.object(GATE.subprocess, "Popen", return_value=process):
                    with self.assertRaises(ValueError):
                        GATE.index_blobs([oid])

    def test_batch_deduplicates_requests_and_validates_ids_before_starting(self):
        oid = self.git("hash-object", "-w", "--stdin", data=b"").strip()
        process = mock.MagicMock()
        process.__enter__.return_value = process
        process.stdout = io.BytesIO(oid + b" blob 0\n\n")
        process.wait.return_value = 0
        with mock.patch.object(GATE.subprocess, "Popen", return_value=process) as start:
            self.assertEqual(GATE.index_blobs([oid, oid]), {oid: b""})
            process.stdin.write.assert_called_once_with(oid + b"\n")
            start.assert_called_once()
        with mock.patch.object(GATE.subprocess, "Popen") as start:
            with self.assertRaisesRegex(ValueError, "Invalid Git object ID"):
                GATE.index_blobs([oid + b"\nHEAD"])
            start.assert_not_called()

    def test_index_change_while_batch_is_reading_is_rejected(self):
        original = subprocess.Popen
        changed = False
        owner = self

        class Reader:
            def __init__(self, stream):
                self.stream = stream

            def __getattr__(self, name):
                return getattr(self.stream, name)

            def read(self, size=-1):
                nonlocal changed
                data = self.stream.read(size)
                if not changed:
                    changed = True
                    owner.write("README.md", "Changed while scanning.\n")
                    owner.git("add", "README.md")
                return data

        def start(arguments, **kwargs):
            process = original(arguments, **kwargs)
            if arguments[1:] == ["cat-file", "--batch"]:
                process.stdout = Reader(process.stdout)
            return process

        with mock.patch.object(GATE.subprocess, "Popen", side_effect=start):
            with self.assertRaisesRegex(ValueError, "Git index changed"):
                self.scan()
        self.assertTrue(changed)
        self.assertFalse((self.root / "build/publication/git-index-scan.json").exists())

    def test_index_change_then_restore_is_not_hidden_by_same_entries(self):
        original = GATE.check_blob
        initial = GATE.index_entries()
        changed = False

        def check(*arguments):
            nonlocal changed
            if not changed:
                changed = True
                self.git("update-index", "--chmod=+x", "README.md")
                self.git("update-index", "--chmod=-x", "README.md")
                self.assertEqual(initial, GATE.index_entries())
            return original(*arguments)

        with mock.patch.object(GATE, "check_blob", side_effect=check):
            with self.assertRaisesRegex(ValueError, "Git index changed"):
                self.scan()

    def test_split_index_and_sha256_repository(self):
        self.git("update-index", "--split-index")
        self.assertEqual(len(GATE.index_storage()), 2)
        self.assertEqual(self.scan(), 0)
        other = self.root / "sha256"
        other.mkdir()
        subprocess.check_output(["git", "init", "-q", "--object-format=sha256", str(other)])
        GATE.ROOT, GATE.MANIFEST = other, other / "publication.json"
        previous_root, self.root = self.root, other
        try:
            self.write("publication.json", json.dumps({"format": 1,
                       "files": ["publication.json", "empty.cpp"], "trees": {}}))
            self.write("empty.cpp", "")
            self.git("add", "publication.json", "empty.cpp")
            self.assertEqual(self.scan(), 0)
        finally:
            self.root = previous_root


if __name__ == "__main__":
    unittest.main()
