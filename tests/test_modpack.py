"""Real NeoForge dependency solving and reversible three-target Mod deployment."""
import importlib.util
import io
import json
from pathlib import Path
import shutil
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("goldcraft_modpack", ROOT / "tools/Modpack.py")
pack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pack)


def jar_bytes(mod_id, version="1.0.0", environment="*", depends=None, breaks=None, provides=None, dependencies=None):
    buffer = io.BytesIO()
    text = 'modLoader="javafml"\nloaderVersion="[4,)"\nlicense="test fixture"\n'
    for identity in [mod_id, *(provides or [])]:
        text += f'[[mods]]\nmodId="{identity}"\nversion="{version}"\n'
    requirements = [{"modId": key, "versionRange": value, "type": "required", "side": "BOTH"}
                    for key, value in (depends if depends is not None else {"minecraft": "[1.21]", "neoforge": "[21.0.167]"}).items()]
    requirements += [{"modId": key, "versionRange": value, "type": "incompatible", "side": "BOTH"}
                     for key, value in (breaks or {}).items()]
    requirements += dependencies or []
    for dep in requirements:
        text += f'[[dependencies.{mod_id}]]\n'
        text += ''.join(f'{key}={json.dumps(value)}\n' for key,value in dep.items())
    with ZipFile(buffer, "w") as jar:
        jar.writestr("META-INF/neoforge.mods.toml", text)
    return buffer.getvalue()


class ModpackTest(unittest.TestCase):
    def setUp(self):
        directory = ROOT / "sandbox/modpack-tests"
        pack.safe(directory).mkdir(parents=True, exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(prefix="case-", dir=directory)
        self.root = pack.safe(Path(self.temporary.name))
        self.source = self.root / "source"
        (self.source / "mods").mkdir(parents=True)
        pack.write_json(self.source / "instance.json", {"runtime": {"minecraft": "1.21", "neoForged": "21.0.167"}})
        for file in pack.core_jars().values():
            shutil.copy2(file, self.source / "mods" / file.name)
        self.rules = self.root / "rules.json"
        pack.write_json(self.rules, {"format": 1, "sideOverrides": {}, "sharedConfigs": []})
        self.targets = {"a": ("client", self.root / "a"), "b": ("client", self.root / "b"), "server": ("server", self.root / "server")}
        self.state = self.root / "deployed.json"

    def tearDown(self):
        # The only recursively removed tree is this freshly-created, validated sandbox fixture.
        pack.safe(self.root)
        self.temporary.cleanup()

    def add(self, name, **options):
        path = self.source / "mods" / f"{name}.jar"
        side = options.pop("environment", "*")
        pack.atomic_bytes(path, jar_bytes(name, **options))
        if side != "*":
            rules = pack.read_json(self.rules)
            rules["sideOverrides"][name] = side
            pack.write_json(self.rules, rules)
        return path

    def prepare(self, refresh_core=False):
        snapshot = pack.snapshot(self.source, self.rules, refresh_core=refresh_core)
        pack.validate(snapshot)
        return snapshot, pack.deployment_plan(snapshot, self.targets, self.state)

    def deploy(self):
        snapshot, plan = self.prepare()
        return pack.apply(snapshot, plan, self.state, check_processes=False)

    def test_physical_sides_and_equal_client_hashes(self):
        self.add("shared_example")
        self.add("client_example", environment="client")
        self.add("server_example", environment="server")
        self.deploy()
        for name in ("a", "b"):
            self.assertEqual({f.name for f in (self.root / name / "mods").glob("*.jar")}, {"shared_example.jar", "client_example.jar"})
        self.assertEqual({f.name for f in (self.root / "server/mods").glob("*.jar")}, {"shared_example.jar", "server_example.jar"})
        self.assertEqual(pack.sha(self.root / "a/mods/shared_example.jar"), pack.sha(self.root / "b/mods/shared_example.jar"))

    def test_actual_loader_rejects_wrong_minecraft_version(self):
        self.add("newer_game", depends={"minecraft": "[1.21.1,)"})
        with self.assertRaisesRegex(pack.PackError, "NeoForge dependency"):
            self.prepare()
        self.assertFalse(self.state.exists())

    def test_actual_loader_rejects_missing_or_wrong_dependency(self):
        self.add("library_mod", version="1.0.0")
        self.add("dependent_mod", depends={"library_mod": "[2.0.0,)"})
        with self.assertRaisesRegex(pack.PackError, "NeoForge dependency"):
            self.prepare()

    def test_actual_loader_honors_breaks(self):
        self.add("first_mod", breaks={"second_mod": "[0,)"})
        self.add("second_mod")
        with self.assertRaisesRegex(pack.PackError, "NeoForge dependency"):
            self.prepare()

    def test_duplicate_ids_rejected_before_target_changes(self):
        self.add("double_mod")
        pack.atomic_bytes(self.source / "mods/another-version.jar", jar_bytes("double_mod", version="2.0.0"))
        with self.assertRaisesRegex(pack.PackError, "Duplicate"):
            self.prepare()

    def test_jarjar_library_and_second_mod_id_resolve(self):
        nested = jar_bytes("nested_library", provides=["library_alias"])
        buffer = io.BytesIO()
        with ZipFile(io.BytesIO(jar_bytes("parent_mod", depends={"library_alias": "[1.0,)"}))) as original, ZipFile(buffer, "w") as jar:
            jar.writestr("META-INF/neoforge.mods.toml", original.read("META-INF/neoforge.mods.toml"))
            jar.writestr("META-INF/jarjar/metadata.json", json.dumps({"jars": [{
                "identifier": {"group": "dev.goldcraft.test", "artifact": "nested_library"},
                "version": {"range": "[1.0,)", "artifactVersion": "1.0.0"},
                "path": "META-INF/jarjar/library.jar", "isObfuscated": False}]}))
            jar.writestr("META-INF/jarjar/library.jar", nested)
        pack.atomic_bytes(self.source / "mods/parent.jar", buffer.getvalue())
        snapshot, plan = self.prepare()
        self.assertEqual(len([c for c in plan["changes"] if c["path"].endswith("parent.jar")]), 3)
        resolved = pack.validate(snapshot)
        self.assertIn("nested_library", [m["id"] for m in resolved["server"]["resolved"]])
        self.assertIn("library_alias", [m["id"] for m in resolved["server"]["resolved"]])

    def test_server_rule_for_a_mod_with_generic_metadata(self):
        self.add("server_utility")
        pack.write_json(self.rules, {"format": 1, "sideOverrides": {"server_utility": "server"}, "sharedConfigs": []})
        self.deploy()
        self.assertTrue((self.root / "server/mods/server_utility.jar").exists())
        self.assertFalse((self.root / "a/mods/server_utility.jar").exists())

    def test_dependency_side_does_not_classify_owning_mod(self):
        self.add("shared_owner", depends={}, dependencies=[
            {"modId": "client_library", "versionRange": "[1,)", "type": "required", "side": "CLIENT"}])
        self.add("client_library", environment="client")
        snapshot, plan = self.prepare()
        owner = next(n for n in snapshot["nodes"] if n["id"] == "shared_owner")
        self.assertEqual(set(owner["sides"]), {"client", "server"})
        self.assertIn("mods/shared_owner.jar", plan["targets"]["server"]["files"])
        self.assertNotIn("mods/client_library.jar", plan["targets"]["server"]["files"])

    def test_conflicting_sides_for_two_mods_in_one_jar(self):
        self.add("first_identity", provides=["second_identity"])
        pack.write_json(self.rules, {"format": 1, "sideOverrides": {"first_identity": "client", "second_identity": "server"}, "sharedConfigs": []})
        with self.assertRaisesRegex(pack.PackError, "Conflicting side"):
            self.prepare()

    def test_fabric_only_mod_is_rejected(self):
        content = io.BytesIO()
        with ZipFile(content, "w") as jar:
            jar.writestr("fabric.mod.json", '{"schemaVersion":1,"id":"fabric_only","version":"1"}')
        pack.atomic_bytes(self.source / "mods/fabric-only.jar", content.getvalue())
        with self.assertRaisesRegex(pack.PackError, "NeoForge edition"):
            self.prepare()

    def test_runtime_guard_distinguishes_loaders_and_blocks_unknown_consumers(self):
        self.assertTrue(pack.runtime_uses_loader(f'java -cp "{ROOT}/neoforge/build/classes" main'))
        self.assertFalse(pack.runtime_uses_loader(f'java -cp "{ROOT}/fabric/build/classes" main'))
        self.assertFalse(pack.runtime_uses_loader('java org.gradle.launcher.daemon.bootstrap.GradleDaemon 8.10.2'))
        self.assertTrue(pack.runtime_uses_loader('java -jar unknown.jar'))
        self.assertTrue(pack.runtime_uses_loader(None))

    def test_runtime_guard_reads_managed_argument_files(self):
        argument_file = self.root / "server.args"
        argument_file.write_text(f'"{ROOT}/fabric/build/classes"', encoding="utf-8")
        self.assertFalse(pack.runtime_uses_loader(f'java @"{argument_file}"'))
        argument_file.write_text(f'"{ROOT}/neoforge/build/classes"', encoding="utf-8")
        self.assertTrue(pack.runtime_uses_loader(f'java @"{argument_file}"'))

    def test_update_then_disable_removes_stale_versions_with_backup(self):
        original = self.add("update_mod")
        self.deploy()
        old = pack.sha(original)
        pack.atomic_bytes(original, jar_bytes("update_mod", version="2.0.0"))
        updated = self.deploy()
        self.assertEqual(pack.sha(Path(updated["backup"]) / "previous/a/mods/update_mod.jar"), old)
        original.rename(original.with_suffix(".jar.disabled"))
        removed = self.deploy()
        self.assertEqual(removed["changedFiles"], 3)
        self.assertFalse((self.root / "a/mods/update_mod.jar").exists())
        self.assertTrue((Path(removed["backup"]) / "previous/server/mods/update_mod.jar").exists())

    def test_local_edit_is_not_overwritten_and_other_targets_are_not_updated(self):
        original = self.add("owned_mod")
        self.deploy()
        previous = pack.sha(self.root / "a/mods/owned_mod.jar")
        pack.atomic_bytes(self.root / "b/mods/owned_mod.jar", b"local edit")
        pack.atomic_bytes(original, jar_bytes("owned_mod", version="2.0.0"))
        with self.assertRaisesRegex(pack.PackError, "Locally changed"):
            self.prepare()
        self.assertEqual(pack.sha(self.root / "a/mods/owned_mod.jar"), previous)

    def test_download_change_between_plan_and_apply_prevents_partial_install(self):
        mod = self.add("changing_mod")
        snapshot, plan = self.prepare()
        pack.atomic_bytes(mod, jar_bytes("changing_mod", version="2.0.0"))
        with self.assertRaisesRegex(pack.PackError, "source mod changed"):
            pack.apply(snapshot, plan, self.state, check_processes=False)
        self.assertFalse((self.root / "a/mods/changing_mod.jar").exists())

    def test_live_runtime_requires_restart(self):
        self.add("guarded_mod")
        snapshot, plan = self.prepare()
        with patch.object(pack, "process_inventory", return_value=[{"ProcessId": 123}]), self.assertRaisesRegex(pack.PackError, "stopped Minecraft"):
            pack.apply(snapshot, plan, self.state)
        self.assertFalse(self.state.exists())

    def test_no_change_does_not_replace_jars_or_require_restart(self):
        self.add("stable_mod")
        self.deploy()
        timestamp = (self.root / "a/mods/stable_mod.jar").stat().st_mtime_ns
        snapshot, plan = self.prepare()
        self.assertFalse(plan["restartRequired"])
        with patch.object(pack, "process_inventory", return_value=[{"ProcessId": 123}]):
            self.assertEqual(pack.apply(snapshot, plan, self.state)["outcome"], "unchanged")
        self.assertEqual((self.root / "a/mods/stable_mod.jar").stat().st_mtime_ns, timestamp)

    def test_only_explicit_shared_mod_config_is_copied(self):
        pack.atomic_bytes(self.source / "config/example.json", b'{"enabled":true}')
        pack.atomic_bytes(self.source / "options.txt", b"private key bindings")
        pack.write_json(self.rules, {"format": 1, "sideOverrides": {}, "sharedConfigs": ["config/example.json"]})
        self.deploy()
        self.assertTrue((self.root / "server/config/example.json").exists())
        self.assertFalse((self.root / "a/options.txt").exists())

    def test_uninstall_does_not_require_editing_old_side_rule(self):
        mod = self.add("server_utility")
        pack.write_json(self.rules, {"format": 1, "sideOverrides": {"server_utility": "server"}, "sharedConfigs": []})
        self.deploy()
        mod.unlink()
        self.deploy()
        self.assertFalse((self.root / "server/mods/server_utility.jar").exists())

    def test_late_copy_failure_restores_prior_clients_and_manifest(self):
        mod = self.add("transaction_mod")
        self.deploy()
        old_state = self.state.read_bytes()
        old_jar = (self.root / "a/mods/transaction_mod.jar").read_bytes()
        pack.atomic_bytes(mod, jar_bytes("transaction_mod", version="2.0.0"))
        snapshot, plan = self.prepare()
        real_write = pack.atomic_bytes
        def broken_write(path, content):
            if path == self.root / "b/mods/transaction_mod.jar":
                raise OSError("simulated locked destination")
            real_write(path, content)
        with patch.object(pack, "atomic_bytes", side_effect=broken_write), self.assertRaisesRegex(OSError, "locked"):
            pack.apply(snapshot, plan, self.state, check_processes=False)
        self.assertEqual((self.root / "a/mods/transaction_mod.jar").read_bytes(), old_jar)
        self.assertEqual(self.state.read_bytes(), old_state)

    def test_saved_manifest_cannot_escape_its_target(self):
        pack.write_json(self.state, {"targets": {"a": {"files": {"mods/../../outside.jar": {"sha256": "0" * 64}}}}})
        with self.assertRaisesRegex(pack.PackError, "Invalid managed relative path"):
            self.prepare()

    def core_candidate(self, **metadata):
        cores = pack.core_jars()
        built = self.root / "new-build" / cores["goldcraft"].name
        pack.atomic_bytes(built, jar_bytes("goldcraft", version="0.2.0-dev", **metadata))
        return {**cores, "goldcraft": built}

    def test_new_core_is_preflighted_without_writes_then_committed_only_when_stopped(self):
        self.deploy()
        cores = self.core_candidate()
        source_jar = self.source / "mods" / cores["goldcraft"].name
        before = source_jar.read_bytes()
        with patch.object(pack, "core_jars", return_value=cores):
            snapshot, plan = self.prepare(refresh_core=True)
            self.assertEqual(source_jar.read_bytes(), before)
            self.assertTrue(plan["restartRequired"])
            with patch.object(pack, "process_inventory", return_value=[{"ProcessId": 123}]), self.assertRaisesRegex(pack.PackError, "stopped Minecraft"):
                pack.apply(snapshot, plan, self.state)
            self.assertEqual(source_jar.read_bytes(), before)
            with patch.object(pack, "process_inventory", return_value=[]):
                result = pack.apply(snapshot, plan, self.state)
            self.assertEqual(source_jar.read_bytes(), cores["goldcraft"].read_bytes())
            self.assertEqual((Path(result["backup"]) / "previous/mod-source/mods" / source_jar.name).read_bytes(), before)
            self.assertFalse(self.prepare()[1]["restartRequired"])
            for _, directory in self.targets.values():
                self.assertFalse((directory / "mods" / source_jar.name).exists())

    def test_incompatible_core_candidate_never_replaces_the_source(self):
        self.deploy()
        cores = self.core_candidate(depends={"minecraft": "[1.22,)"})
        source_jar = self.source / "mods" / cores["goldcraft"].name
        before = source_jar.read_bytes()
        old_state = self.state.read_bytes()
        with patch.object(pack, "core_jars", return_value=cores), self.assertRaisesRegex(pack.PackError, "NeoForge dependency"):
            self.prepare(refresh_core=True)
        self.assertEqual(source_jar.read_bytes(), before)
        self.assertEqual(self.state.read_bytes(), old_state)

    def test_core_refresh_preserves_an_unrecognized_local_core_edit(self):
        self.deploy()
        cores = self.core_candidate()
        source_jar = self.source / "mods" / cores["goldcraft"].name
        edited = jar_bytes("goldcraft", version="1.0.0-custom-local")
        pack.atomic_bytes(source_jar, edited)
        with patch.object(pack, "core_jars", return_value=cores), self.assertRaisesRegex(pack.PackError, "last managed build"):
            self.prepare(refresh_core=True)
        self.assertEqual(source_jar.read_bytes(), edited)

    def test_core_and_all_runtime_mods_roll_back_when_manifest_commit_fails(self):
        mod = self.add("transaction_mod")
        self.deploy()
        old_state = self.state.read_bytes()
        old_jar = mod.read_bytes()
        cores = self.core_candidate()
        source_jar = self.source / "mods" / cores["goldcraft"].name
        old_core = source_jar.read_bytes()
        pack.atomic_bytes(mod, jar_bytes("transaction_mod", version="2.0.0"))
        with patch.object(pack, "core_jars", return_value=cores):
            snapshot, plan = self.prepare(refresh_core=True)
            real_write = pack.write_json
            def fail_manifest(path, value):
                if path == self.state:
                    raise OSError("simulated manifest lock")
                real_write(path, value)
            with patch.object(pack, "write_json", side_effect=fail_manifest), self.assertRaisesRegex(OSError, "manifest lock"):
                pack.apply(snapshot, plan, self.state, check_processes=False)
        self.assertEqual(source_jar.read_bytes(), old_core)
        self.assertEqual(self.state.read_bytes(), old_state)
        for _, directory in self.targets.values():
            self.assertEqual((directory / "mods/transaction_mod.jar").read_bytes(), old_jar)


if __name__ == "__main__":
    unittest.main(verbosity=2)
