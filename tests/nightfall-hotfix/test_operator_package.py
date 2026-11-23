"""Regression cases for cumulative updates built from existing CS releases."""
from pathlib import Path
import runpy
from tempfile import TemporaryDirectory
import unittest

ROOT = Path(__file__).resolve().parents[2]
include_file = runpy.run_path(str(ROOT / "tools/nightfall-hotfix/operator_update.py"))["include_file"]


class HostOwnershipTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        (ROOT / "build").mkdir(exist_ok=True)

    def test_inherited_host_is_removed_from_the_next_payload(self):
        with TemporaryDirectory(dir=ROOT / "build") as folder:
            package = Path(folder) / "package"
            staged = package / "payload/cs/hlds.exe"
            staged.parent.mkdir(parents=True)
            original = Path(folder) / "hlds.exe"
            original.write_bytes(b"operator's existing host")
            staged.hardlink_to(original)
            files = {"cs/hlds.exe": {}, "cs/swds.dll": {"bytes": 42}}
            required = {}
            include_file(package, files, required, "cs/HLDS.exe", original, "")
            self.assertFalse(staged.exists())
            self.assertEqual(original.read_bytes(), b"operator's existing host")
            self.assertEqual(files, {"cs/swds.dll": {"bytes": 42}})
            self.assertEqual(required, {})

    def test_legacy_baseline_pin_does_not_require_new_host_bytes(self):
        with TemporaryDirectory(dir=ROOT / "build") as folder:
            package = Path(folder)
            required = {"CS/hlds.exe": {}, "cs/filesystem_stdio.dll": {"bytes": 17}}
            files = {}
            include_file(package, files, required, "cs/hlds.exe", package / "absent", "")
            self.assertEqual(files, {})
            self.assertEqual(required, {"cs/filesystem_stdio.dll": {"bytes": 17}})
            self.assertFalse((package / "payload").exists())


if __name__ == "__main__":
    unittest.main()
