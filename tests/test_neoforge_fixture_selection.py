"""Keep compatibility variants isolated and pass their actual inputs to FML validation."""
import copy
import importlib.util
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
import Modpack

spec = importlib.util.spec_from_file_location("neoforge_compatibility", ROOT / "tools/Exercise-NeoForgeCompatibility.py")
compatibility = importlib.util.module_from_spec(spec)
spec.loader.exec_module(compatibility)


class FixtureSelectionTest(unittest.TestCase):
    def setUp(self):
        directory = Modpack.safe(ROOT / "sandbox/fixture-selection-tests")
        directory.mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="case-", dir=directory)
        self.root = Modpack.safe(Path(self.temporary.name))
        self.source = self.root / "master"
        (self.source / "mods").mkdir(parents=True)
        for file in Modpack.core_jars().values():
            shutil.copy2(file, self.source / "mods" / file.name)
        Modpack.write_json(self.source / "instance.json", {"runtime": {
            "minecraft": Modpack.PLATFORM["minecraft"], "neoForged": Modpack.PLATFORM["loader"]}})
        self.rules = self.root / "rules.json"
        Modpack.write_json(self.rules, {"format": 1, "sideOverrides": {}, "sharedConfigs": []})

    def tearDown(self):
        Modpack.safe(self.root)
        self.temporary.cleanup()

    def jar(self, relative, ids, version="1.0.0", required=None):
        path = Modpack.safe(self.root / relative)
        path.parent.mkdir(parents=True, exist_ok=True)
        metadata = 'modLoader="javafml"\nloaderVersion="[4,)"\nlicense="test fixture"\n'
        for mod_id in ids:
            metadata += f'[[mods]]\nmodId="{mod_id}"\nversion="{version}"\n'
        for mod_id, constraint in (required or {}).items():
            metadata += (f'[[dependencies.{ids[0]}]]\nmodId="{mod_id}"\ntype="required"\n'
                         f'versionRange="{constraint}"\nside="BOTH"\n')
        with ZipFile(path, "w") as jar:
            jar.writestr("META-INF/neoforge.mods.toml", metadata)
        return path

    def snapshot(self):
        return Modpack.snapshot(self.source, self.rules)

    def source_hashes(self):
        return {path.relative_to(self.source).as_posix(): Modpack.sha(path)
                for path in self.source.rglob("*") if path.is_file()}

    def test_unknown_and_core_exclusions_fail_before_selecting(self):
        for mod_id in ("missing_mod", "goldcraft"):
            with self.subTest(mod_id=mod_id), self.assertRaises(ValueError):
                compatibility.select_fixture_pack(self.snapshot(), [mod_id], [])

    def test_alias_exclusion_removes_its_whole_jar_without_editing_master(self):
        self.jar("master/mods/optional.jar", ["optional_mod", "optional_alias"])
        original = self.snapshot()
        before, hashes = copy.deepcopy(original), self.source_hashes()
        selected = compatibility.select_fixture_pack(original, ["optional_alias"], [])
        self.assertEqual([node["id"] for node in selected["nodes"]], ["goldcraft"])
        self.assertNotEqual(selected["fingerprint"], original["fingerprint"])
        self.assertEqual(original, before)
        self.assertEqual(self.source_hashes(), hashes)

    def test_replacement_preserves_side_rule_and_original_files(self):
        self.jar("master/mods/client-old.jar", ["client_example"])
        Modpack.write_json(self.rules, {"format": 1, "sideOverrides": {"client_example": "client"}, "sharedConfigs": []})
        replacement = self.jar("candidate/client-new.jar", ["client_example"], "2.0.0")
        original = self.snapshot()
        before, hashes = copy.deepcopy(original), self.source_hashes()
        selected = compatibility.select_fixture_pack(original, [], [], [replacement])
        node = next(node for node in selected["nodes"] if node["id"] == "client_example")
        self.assertEqual((node["name"], node["version"], node["sides"]), ("client-new.jar", "2.0.0", ["client"]))
        self.assertEqual(node["sha256"], Modpack.sha(replacement))
        self.assertEqual(original, before)
        self.assertEqual(self.source_hashes(), hashes)

    def test_duplicate_alias_filename_and_excluded_id_are_rejected(self):
        self.jar("master/mods/optional.jar", ["optional_mod", "optional_alias"])
        duplicate = self.jar("candidate/duplicate.jar", ["extra_mod", "optional_alias"])
        collision = self.jar("candidate/optional.jar", ["extra_mod"])
        excluded = self.jar("candidate/excluded.jar", ["optional_mod"])
        for excluded_ids, path in (([], duplicate), ([], collision), (["optional_mod"], excluded)):
            with self.subTest(path=path.name), self.assertRaises(ValueError):
                compatibility.select_fixture_pack(self.snapshot(), excluded_ids, [path])

    def test_missing_replacement_and_outside_workspace_input_are_rejected(self):
        replacement = self.jar("candidate/unknown.jar", ["unknown_mod"])
        with self.assertRaisesRegex(ValueError, "not in the selected pack"):
            compatibility.select_fixture_pack(self.snapshot(), [], [], [replacement])
        with self.assertRaises(Modpack.PackError):
            compatibility.select_fixture_pack(self.snapshot(), [], [ROOT.parent / "outside.jar"])

    def test_real_fml_checks_dependencies_added_by_extra_and_replacement(self):
        self.jar("master/mods/optional.jar", ["optional_mod"])
        dependent = self.jar("candidate/needs-library.jar", ["optional_mod"], "2.0.0", {"fixture_library": "[2.0,)"})
        library = self.jar("candidate/library.jar", ["fixture_library"], "2.0.0")
        selected = compatibility.select_fixture_pack(self.snapshot(), [], [], [dependent])
        with self.assertRaisesRegex(Modpack.PackError, "NeoForge dependency"):
            Modpack.validate(selected)
        selected = compatibility.select_fixture_pack(self.snapshot(), [], [library], [dependent])
        resolved = Modpack.validate(selected)
        for side in ("client", "server"):
            versions = {node["id"]: node["version"] for node in resolved[side]["resolved"]}
            self.assertEqual(versions["optional_mod"], "2.0.0")
            self.assertEqual(versions["fixture_library"], "2.0.0")


if __name__ == "__main__":
    unittest.main()
