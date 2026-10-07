"""Deletion boundaries, runtime process protection and actual log retention."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
spec = importlib.util.spec_from_file_location("sandbox_cleanup", ROOT / "tools/Compact-Sandbox.py")
compact = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compact)


class SandboxCleanupTest(unittest.TestCase):
    def setUp(self):
        parent = compact.safe(ROOT / "sandbox/cleanup-tests")
        parent.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="case-", dir=parent)
        self.root = compact.safe(Path(self.temporary.name))
        self.game = self.root / "cs-client-a/Half-Life"
        self.cache = self.game / "cstrike_downloads"
        self.cache.mkdir(parents=True)
        self.baseline = self.root / "analysis/installation/original-baseline.json"
        self.baseline.parent.mkdir(parents=True)
        self.baseline.write_text('{"files": []}', encoding="utf-8")
        self.root_patch = patch.object(compact, "ROOT", self.root)
        self.base_patch = patch.object(compact, "BASE", self.root)
        self.root_patch.start()
        self.base_patch.start()

    def tearDown(self):
        self.root_patch.stop()
        self.base_patch.stop()
        # Only this fresh, verified fixture tree is recursively removed.
        compact.safe(self.root)
        self.temporary.cleanup()
        try:
            compact.safe(self.root.parent).rmdir()
        except OSError:
            pass

    def write(self, path, value=b"test"):
        compact.safe(path).parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(value)
        return path

    def stopped_apply(self, records):
        with patch.object(compact, "process_inventory", return_value=[]):
            return compact.apply(records)

    def test_edited_and_added_files_and_game_data_survive(self):
        original = b"original cache"
        unchanged = self.write(self.cache / "unchanged.dat", original)
        edited = self.write(self.cache / "edited.dat", b"modified cache")
        self.assertEqual(len(original), edited.stat().st_size)
        baseline = [{"path": p.relative_to(self.game).as_posix(), "bytes": len(original),
                     "sha256": hashlib.sha256(original).hexdigest()} for p in (unchanged, edited)]
        self.baseline.write_text(json.dumps({"files": baseline}), encoding="utf-8")
        added = self.write(self.cache / "user-added.dat", original)
        config = self.write(self.game / "cstrike/config.cfg")
        save = self.write(self.root / "neoforge-server/world/level.dat")
        mod = self.write(self.root / "modpack-neoforge/master/mods/user.jar")
        records = compact.plan()
        result = self.stopped_apply(records)
        self.assertEqual(len(result["deleted"]), 1)
        self.assertEqual(len(result["skipped"]), 1)
        self.assertFalse(unchanged.exists())
        for retained in (edited, added, config, save, mod):
            self.assertTrue(retained.is_file(), str(retained))

    def test_change_after_planning_is_preserved(self):
        log = self.write(self.game / "qconsole.log")
        records = compact.plan()
        log.write_bytes(b"new content after plan")
        result = self.stopped_apply(records)
        self.assertEqual(result["removedBytes"], 0)
        self.assertEqual(len(result["skipped"]), 1)
        self.assertTrue(log.exists())

    def test_only_two_latest_runs_and_backups_retained(self):
        logs = self.root / "cs-client-a/logs"
        for n in range(3):
            log = self.write(logs / f"CsClient-20261007-01000{n}.stdout.log")
            backup = self.root / "deployment-backups" / f"backup-{n}"
            self.write(backup / "old.dll")
            os.utime(log, (100 + n, 100 + n))
            os.utime(backup, (100 + n, 100 + n))
        custom = self.write(logs / "CsClient-user-notes.stdout.log")
        result = self.stopped_apply(compact.plan())
        self.assertEqual(len(result["deleted"]), 2)
        self.assertTrue(custom.exists())
        self.assertEqual(len(list(logs.glob("CsClient-2026*.log"))), 2)
        self.assertEqual(len(list((self.root / "deployment-backups").iterdir())), 2)

    def test_record_cannot_escape_selected_sandbox(self):
        outside = self.root.parent / (self.root.name + "-outside.txt")
        self.write(outside)
        try:
            info = outside.stat()
            record = {"path": "../" + outside.name, "bytes": info.st_size, "modifiedNs": info.st_mtime_ns}
            with self.assertRaisesRegex(RuntimeError, "outside the sandbox"):
                self.stopped_apply([record])
            self.assertTrue(outside.exists())
        finally:
            compact.safe(outside).unlink()

    @unittest.skipUnless(os.name == "nt", "Windows directory junction")
    def test_directory_junction_is_rejected(self):
        target = self.root / "retained"
        sentinel = self.write(target / "save.dat")
        link = self.cache / "linked-directory"
        subprocess.run(["cmd.exe", "/c", "mklink", "/J", str(link), str(target)],
                       check=True, capture_output=True, creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            with self.assertRaisesRegex(RuntimeError, "reparse point"):
                compact.plan()
            self.assertTrue(sentinel.is_file())
        finally:
            # Remove only the directory junction itself, never its target tree.
            link.rmdir()

    def test_both_loaders_block_cleanup_but_gradle_does_not(self):
        processes = [
            {"ProcessId": 1, "Name": "java.exe", "CommandLine": "net.fabricmc.loader.Main private-value"},
            {"ProcessId": 2, "Name": "javaw.exe", "CommandLine": "net.neoforged.bootstrap.Main private-value"},
            {"ProcessId": 3, "Name": "java.exe", "CommandLine": "org.gradle.launcher.daemon.Main"},
        ]
        response = subprocess.CompletedProcess([], 0, json.dumps(processes), "")
        with patch.object(compact.subprocess, "run", return_value=response):
            self.assertEqual(compact.process_inventory(), [
                {"ProcessId": 1, "Name": "java.exe"}, {"ProcessId": 2, "Name": "javaw.exe"}])
            with self.assertRaisesRegex(RuntimeError, "CS/ReHLDS and Minecraft"):
                compact.apply([])

    @unittest.skipUnless(os.name == "nt", "Windows native process inventory")
    def test_actual_native_process_in_sandbox_blocks_cleanup(self):
        executable = self.root / "ping.exe"
        shutil.copy2(Path(os.environ["WINDIR"]) / "System32/ping.exe", executable)
        process = subprocess.Popen([str(executable), "-t", "127.0.0.1"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
            creationflags=subprocess.CREATE_NO_WINDOW)
        try:
            self.assertIsNone(process.poll())
            self.assertIn(process.pid, [p["ProcessId"] for p in compact.process_inventory()])
            with self.assertRaisesRegex(RuntimeError, "CS/ReHLDS and Minecraft"):
                compact.apply([])
        finally:
            process.terminate()
            process.wait(timeout=10)

    @unittest.skipUnless(os.name == "nt", "PowerShell launch log retention")
    def test_actual_launcher_retention_preserves_other_roles(self):
        logs = self.root / "cs-client-a/logs"
        for n in range(3):
            for suffix in ("stdout", "stderr"):
                log = self.write(logs / f"CsClient-20261007-01000{n}.{suffix}.log")
                os.utime(log, (100 + n, 100 + n))
        other = self.write(logs / "MinecraftClient-20261007-010000.stdout.log")
        diagnostic = self.write(logs / "goldcraft-client.log")
        def ps_literal(value):
            return "'" + str(value).replace("'", "''") + "'"
        command = (f". {ps_literal(ROOT / 'tools/SandboxLogs.ps1')}; "
                   f"Remove-OldSandboxRunLogs -LogDirectory {ps_literal(logs)} -Role CsClient -ReserveRun")
        result = subprocess.run(["pwsh.exe", "-NoProfile", "-NonInteractive", "-Command", command],
            capture_output=True, text=True, encoding="utf-8", creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "4")
        self.assertEqual(len(list(logs.glob("CsClient-*.log"))), 2)
        self.assertTrue(other.exists())
        self.assertTrue(diagnostic.exists())


if __name__ == "__main__":
    unittest.main(verbosity=2)
