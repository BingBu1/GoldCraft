"""Exercise the real pinned builder/compiler; no server or deployment writes.

Run after the first build: python -m unittest discover -s tests -p test_amxx_builder.py -v
Fixtures live in a new amxx/ directory and are removed after every test.
"""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parent.parent
SCRIPT = ROOT / "tools/Build-AMXX.ps1"
OUTPUT = ROOT / "build/amxx/plugins"
COMPILER = ROOT / ".tools/amxx-1.9.0.5303/addons/amxmodx/scripting/amxxpc.exe"
FIXTURE_NAME = "gc_builder_fixture"


def digest(file):
    return hashlib.sha256(file.read_bytes()).hexdigest()


def executable_image(file):
    """amxxpc.cpp's 0x0300 container; compare executable AMX, not path debug data."""
    data = file.read_bytes()
    if struct.unpack_from("<IHB", data) != (0x414D5858, 0x0300, 1):
        raise ValueError("Unexpected AMXX container")
    cell, compressed, image_size, _memory, offset = struct.unpack_from("<BIIII", data, 7)
    image = zlib.decompress(data[offset:offset + compressed])
    if cell != 4 or len(image) != image_size:
        raise ValueError("Invalid 32-bit AMX image")
    size, = struct.unpack_from("<I", image)
    if size > len(image):
        raise ValueError("Truncated AMX image")
    return image[:size]


class AmxxBuilderTest(unittest.TestCase):
    def setUp(self):
        self.assertTrue(COMPILER.is_file(), "Prepare the pinned AMXX compiler first")
        self.temporary = tempfile.TemporaryDirectory(prefix="builder_test_", dir=ROOT / "amxx")
        self.directory = Path(self.temporary.name)
        scratch = ROOT / "build/amxx/builder-tests"
        scratch.mkdir(parents=True, exist_ok=True)
        self.compiled = tempfile.TemporaryDirectory(prefix="direct-", dir=scratch)
        self.output = OUTPUT / (FIXTURE_NAME + ".amxx")
        self.assertFalse(self.output.exists(), "Preserve any existing fixture-named artifact")
        self.addCleanup(self.cleanup_fixture)
        self.source = self.directory / (FIXTURE_NAME + ".sma")
        self.source.write_text('#include <amxmodx>\npublic plugin_init() { register_plugin("Build probe", "1", "GoldCraft"); }\n', encoding="utf-8")

    def cleanup_fixture(self):
        # Resolve and reject links before recursive cleanup of only this new tree.
        for item in (self.directory, *self.directory.rglob("*")):
            if not item.resolve().is_relative_to(ROOT / "amxx") or item.is_symlink() or item.is_junction():
                raise ValueError("Unsafe build-fixture cleanup")
        self.temporary.cleanup()
        compiled = Path(self.compiled.name)
        if not compiled.resolve().is_relative_to(ROOT / "build/amxx/builder-tests") or compiled.is_junction():
            raise ValueError("Unsafe direct-compiler fixture cleanup")
        self.compiled.cleanup()
        if self.output.exists():
            if self.output.is_symlink() or not self.output.resolve().is_relative_to(OUTPUT):
                raise ValueError("Unsafe fixture artifact")
            self.output.unlink()

    def run_build(self, *args, success=True):
        result = subprocess.run(["pwsh", "-NoProfile", "-File", str(SCRIPT), *args], cwd=self.directory,
                                capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=120)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, success, output[-5000:])
        return output

    def test_named_build_keeps_other_artifacts_and_sources(self):
        other = OUTPUT / "goldcraft.amxx"
        before = (digest(other), other.stat().st_mtime_ns, self.source.read_bytes())
        self.run_build("-Plugins", FIXTURE_NAME)
        self.assertTrue(executable_image(self.output))
        self.assertEqual((digest(other), other.stat().st_mtime_ns, self.source.read_bytes()), before)
        built = json.loads((ROOT / "build/amxx/last-build.json").read_text())
        self.assertEqual([entry["name"] for entry in built], [FIXTURE_NAME])

    def test_compile_failure_preserves_complete_last_success(self):
        self.run_build("-Plugins", FIXTURE_NAME)
        last = ROOT / "build/amxx/last-build.json"
        other = OUTPUT / "goldcraft.amxx"
        before = (self.output.read_bytes(), other.stat().st_mtime_ns, last.read_bytes())
        self.source.write_text("#include <amxmodx>\n#error deliberate_compile_failure\n", encoding="utf-8")
        output = self.run_build("-Plugins", f"goldcraft,{FIXTURE_NAME}", success=False)
        self.assertIn("deliberate_compile_failure", output)
        self.assertEqual((self.output.read_bytes(), other.stat().st_mtime_ns, last.read_bytes()), before)
        self.assertFalse(json.loads((ROOT / "build/amxx/last-builder.json").read_text())["ok"])

    def test_duplicate_plugin_and_conflicting_header_are_rejected(self):
        duplicate = self.directory / "goldcraft.sma"
        duplicate.write_bytes(self.source.read_bytes())
        self.assertIn("Duplicate plugin name", self.run_build("-Plugins", "goldcraft", success=False))
        duplicate.unlink()
        include = self.directory / "include/goldcraft.inc"
        include.parent.mkdir()
        include.write_text("// conflicting public API\n", encoding="utf-8")
        self.assertIn("Conflicting public include", self.run_build("-Plugins", FIXTURE_NAME, success=False))

    def test_unknown_plugin_and_implicit_deploy_are_rejected(self):
        self.assertIn("Unknown plugin", self.run_build("-Plugins", "gc_missing_builder_plugin", success=False))
        self.assertIn("Deployment requires explicit", self.run_build("-Deploy", success=False))

    def test_warning_is_visible_and_extra_include_is_used(self):
        extra = self.directory / "private_headers"
        extra.mkdir()
        (extra / "gc_builder_extra.inc").write_text("#define GC_BUILDER_VERSION 7\n", encoding="utf-8")
        self.source.write_text('#include <amxmodx>\n#include <gc_builder_extra>\n'
                               '#if GC_BUILDER_VERSION != 7\n#error wrong_extra_header\n#endif\n'
                               'public plugin_init() { new unused; register_plugin("Build", "1", "GoldCraft"); }\n', encoding="utf-8")
        output = self.run_build("-Plugins", FIXTURE_NAME, "-Includes", extra.relative_to(ROOT).as_posix())
        self.assertIn("warning 203:", output)
        self.assertEqual(json.loads((ROOT / "build/amxx/last-builder.json").read_text())["warnings"], 1)

    def test_outside_include_and_other_build_directory_are_protected(self):
        sibling = ROOT / "build/gc-builder-preservation-probe.txt"
        self.assertFalse(sibling.exists())
        sibling.write_text("keep native and Java build output", encoding="utf-8")
        self.addCleanup(sibling.unlink)
        self.assertIn("Path outside workspace", self.run_build("-Plugins", FIXTURE_NAME, "-Includes", "../outside", success=False))
        self.run_build("-Plugins", FIXTURE_NAME)
        self.assertEqual(sibling.read_text(), "keep native and Java build output")

    def test_production_and_relative_source_include_match_direct_compiler(self):
        plugins = ["goldcraft", "admin", "nademodes", "zp50_core", "goldcraft_buy_menu_fixture"]
        self.run_build("-Plugins", ",".join(plugins))
        for plugin in plugins:
            with self.subTest(plugin=plugin):
                sources = list((ROOT / "amxx").rglob(plugin + ".sma"))
                self.assertEqual(len(sources), 1)
                source = sources[0]
                module = source.relative_to(ROOT / "amxx").parts[0]
                direct = Path(self.compiled.name) / (plugin + ".amxx")
                include_dirs = [ROOT / "amxx" / module / "include", ROOT / "amxx/goldcraft/include",
                                ROOT / ".tools/reapi-5.29.0.358/addons/amxmodx/scripting/include",
                                COMPILER.parent / "include", ROOT / "amxx/zombie_plague/include"]
                result = subprocess.run([str(COMPILER), str(source), "-o" + str(direct),
                                         *("-i" + str(folder) for folder in include_dirs if folder.is_dir())],
                                        cwd=ROOT, capture_output=True, text=True, timeout=60)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(executable_image(direct), executable_image(OUTPUT / direct.name))


if __name__ == "__main__":
    unittest.main()
