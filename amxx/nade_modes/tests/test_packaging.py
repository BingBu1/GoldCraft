"""Package/source preservation tests; never writes to the actual server."""
from pathlib import Path
import contextlib
import importlib.util
import io
import json
import shutil
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]


def module(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "tools" / (name + ".py"))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


class NadeModesPackaging(unittest.TestCase):
    def setUp(self):
        directory = ROOT / "build/nademodes-package-tests"
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=directory)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.prepare = module("Prepare-NadeModes")
        self.deploy = module("Deploy-NadeModes")
        build = json.loads((ROOT / "dist/nademodes/manifest.json").read_text(encoding="utf-8"))
        paths = {"patches/nademodes-reapi.json", "patches/nademodes-reapi.patch", "notices/GPLv3.txt",
                 "dist/nademodes/manifest.json"}
        paths.update("references/NadeModes/" + entry["name"] for entry in self.prepare.ATTACHMENTS)
        paths.update(entry["source"] for entry in build["runtime"])
        paths.update(entry["path"] for entry in build["sources"])
        for relative in paths:
            target = self.root / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / relative, target)
        self.prepare.ROOT = self.deploy.ROOT = self.root
        self.prepare.WORK = self.root / "amxx/nade_modes"
        self.prepare.PATCH = self.root / "patches/nademodes-reapi.patch"
        self.prepare.MANIFEST = self.root / "patches/nademodes-reapi.json"
        self.deploy.RUNTIME = self.root / "sandbox/cs-server/Half-Life/cstrike/addons/amxmodx"
        self.runtime = self.deploy.RUNTIME
        (self.runtime / "configs").mkdir(parents=True)
        self.plugins = b"\xef\xbb\xbf; existing plugins\r\nadmin.amxx\r\ngoldcraft.amxx\r\n"
        (self.runtime / "configs/plugins.ini").write_bytes(self.plugins)
        self.original = self.snapshot(self.prepare.WORK)

    @staticmethod
    def snapshot(root):
        return {p.relative_to(root).as_posix(): p.read_bytes() for p in root.rglob("*") if p.is_file()}

    def prepare_sources(self, check=False):
        return self.prepare.prepare(self.root / "references/NadeModes", check)

    def deploy_files(self, check=False):
        with contextlib.redirect_stdout(io.StringIO()):
            return self.deploy.deploy(check_only=check)

    def test_official_patch_replay_and_check_only(self):
        self.assertEqual(self.prepare_sources(True)["replayed"], 2)
        self.assertEqual(self.snapshot(self.prepare.WORK), self.original)
        self.prepare_sources()
        self.assertEqual((self.prepare.WORK / "LICENSE.txt").read_bytes(), (self.root / "notices/GPLv3.txt").read_bytes())
        for entry in json.loads(self.prepare.MANIFEST.read_text())["files"]:
            self.assertEqual(self.prepare.digest(self.prepare.normalized((self.prepare.WORK / entry["path"]).read_bytes())),
                             entry["resultSha256Normalized"])

    def test_custom_source_is_preserved_before_any_write(self):
        source = self.prepare.WORK / "nademodes.sma"
        source.write_bytes(source.read_bytes() + b"\n// local customization\n")
        before = self.snapshot(self.prepare.WORK)
        with self.assertRaisesRegex(ValueError, "Preserving local edits"):
            self.prepare_sources()
        self.assertEqual(self.snapshot(self.prepare.WORK), before)

    def test_custom_license_is_preserved_before_source_write(self):
        (self.prepare.WORK / "LICENSE.txt").write_text("Local license note", encoding="utf-8")
        before = self.snapshot(self.prepare.WORK)
        with self.assertRaisesRegex(ValueError, "license"):
            self.prepare_sources()
        self.assertEqual(self.snapshot(self.prepare.WORK), before)

    def test_altered_upstream_or_patch_is_rejected(self):
        source = self.root / "references/NadeModes/nademodes.sma"
        original = source.read_bytes()
        source.write_bytes(original + b"\n")
        with self.assertRaisesRegex(ValueError, "attachment hash"):
            self.prepare_sources()
        source.write_bytes(original)
        self.prepare.PATCH.write_bytes(self.prepare.PATCH.read_bytes() + b"\n")
        with self.assertRaisesRegex(ValueError, "patch/manifest"):
            self.prepare_sources()
        self.assertEqual(self.snapshot(self.prepare.WORK), self.original)

    def test_deployment_preserves_configuration_and_order(self):
        config = self.runtime / "configs/nade_modes.cfg"
        custom = "// 保留设置\r\nnademodes_bot_support 0\r\n".encode("utf-8")
        config.write_bytes(custom)
        before = self.snapshot(self.runtime)
        self.assertTrue(self.deploy_files(True)["checkOnly"])
        self.assertEqual(self.snapshot(self.runtime), before)
        installed = self.deploy_files()
        self.assertEqual(config.read_bytes(), custom)
        self.assertEqual((self.runtime / "configs/plugins.ini").read_bytes(),
                         self.plugins[:3] + b"nademodes.amxx\r\n" + self.plugins[3:])
        self.assertFalse(any(self.runtime.rglob("*.sma")))
        self.assertFalse(any(self.runtime.rglob("*test.amxx")))
        self.assertEqual(len(installed["installed"]), 4)
        self.assertEqual(self.deploy_files()["changed"], [])

    def test_changed_artifact_and_bad_destination_do_not_touch_server(self):
        artifact = self.root / "build/amxx/plugins/nademodes.amxx"
        original = artifact.read_bytes()
        artifact.write_bytes(original + b"bad")
        before = self.snapshot(self.runtime)
        with self.assertRaisesRegex(ValueError, "Rebuild changed resources"):
            self.deploy_files()
        artifact.write_bytes(original)
        path = self.root / "dist/nademodes/manifest.json"
        spec = json.loads(path.read_text())
        spec["runtime"][0]["destination"] = "../../outside.amxx"
        path.write_text(json.dumps(spec))
        with self.assertRaisesRegex(ValueError, "runtime manifest"):
            self.deploy_files()
        self.assertEqual(self.snapshot(self.runtime), before)

    def test_custom_translation_is_not_overwritten(self):
        self.deploy_files()
        translation = self.runtime / "data/lang/nademodes_goldcraft.txt"
        translation.write_bytes(translation.read_bytes() + b"\n; local translator edit\n")
        before = self.snapshot(self.runtime)
        with self.assertRaisesRegex(ValueError, "customized translation"):
            self.deploy_files()
        self.assertEqual(self.snapshot(self.runtime), before)

    def test_shared_header_change_requires_rebuild(self):
        before = self.snapshot(self.runtime)
        header = self.root / "amxx/goldcraft/include/goldcraft_menus.inc"
        header.write_bytes(header.read_bytes() + b"\n// new menu behavior\n")
        with self.assertRaisesRegex(ValueError, "Rebuild changed sources"):
            self.deploy_files()
        self.assertEqual(self.snapshot(self.runtime), before)

    def test_secondary_plugin_registration_is_preserved_for_review(self):
        (self.runtime / "configs/plugins-other.ini").write_bytes(b"nademodes.amxx\n")
        before = self.snapshot(self.runtime)
        with self.assertRaisesRegex(ValueError, "secondary plugin list"):
            self.deploy_files()
        self.assertEqual(self.snapshot(self.runtime), before)

    def test_stock_backup_is_not_an_active_plugin_list(self):
        backup = self.runtime / "configs/plugins.stock.ini"
        backup.write_bytes(b"nademodes.amxx\n")
        self.deploy_files()
        self.assertEqual(backup.read_bytes(), b"nademodes.amxx\n")
        self.assertIn(b"nademodes.amxx", (self.runtime / "configs/plugins.ini").read_bytes())


if __name__ == "__main__":
    unittest.main()
