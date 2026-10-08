"""Export only the source adaptations listed in sources.lock.json."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
lock = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8"))
new_renderer_files = ("include/Interface/IMetaRendererScene.h", "src/gl_external_scene.h",
                      "src/gl_gbuffer.cpp", "src/gl_studio_shadow.cpp", "tests/studio_shadow_gl_tests.cpp",
                      "src/gl_studio_player.cpp", "src/studio_diagnostics.h", "src/studio_diagnostics.cpp",
                      "tests/studio_player_tests.cpp", "src/gl_shadow_cull.h", "src/gl_shadow_cull.cpp",
                      "tests/shadow_cull_tests.cpp", "src/studio_bone_cache.cpp", "tests/studio_bone_cache_tests.cpp")
for name, entry in lock["sources"].items():
    if "patch" not in entry:
        continue
    source = ROOT / entry["path"]
    actual = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=source, text=True).strip()
    if actual != entry["commit"]:
        raise RuntimeError(f"Refusing a patch against an unpinned {name} revision")
    patch = subprocess.check_output(["git", "diff", "--no-ext-diff", "HEAD", "--"], cwd=source)
    if name == "Renderer":
        for file in new_renderer_files:
            result = subprocess.run(["git", "diff", "--no-index", "--", "/dev/null", file], cwd=source, capture_output=True)
            if result.returncode != 1:
                raise RuntimeError(f"Could not export the new Renderer interface: {file}")
            patch += result.stdout
    if name == "BulletPhysics":
        for file in ("tests/studio_lifecycle_tests.cpp",):
            result = subprocess.run(["git", "diff", "--no-index", "--", "/dev/null", file], cwd=source, capture_output=True)
            if result.returncode != 1:
                raise RuntimeError(f"Could not export the new BulletPhysics regression: {file}")
            patch += result.stdout
    if name == "VGUI2Extension":
        for file in ("src/TeamMenuMapPageHook.h", "tests/team_menu_abi_tests.cpp"):
            result = subprocess.run(["git", "diff", "--no-index", "--", "/dev/null", file], cwd=source, capture_output=True)
            if result.returncode != 1:
                raise RuntimeError(f"Could not export the new VGUI client ABI source: {file}")
            patch += result.stdout
    target = ROOT / entry["patch"]
    if not target.resolve().is_relative_to(ROOT / "patches"):
        raise RuntimeError("Patch destination outside the workspace patch directory")
    target.write_bytes(patch)
    print(f"{name}: {target.relative_to(ROOT)}, {len(patch)} bytes")
