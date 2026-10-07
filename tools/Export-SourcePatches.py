"""Export only the source adaptations listed in sources.lock.json."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
lock = json.loads((ROOT / "sources.lock.json").read_text(encoding="utf-8"))
new_renderer_files = ("include/Interface/IMetaRendererScene.h", "src/gl_external_scene.h")
for name in ("MetaHook", "Renderer", "ReGameDLL_CS", "ReHLDS"):
    entry = lock["sources"][name]
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
    target = ROOT / entry["patch"]
    if not target.resolve().is_relative_to(ROOT / "patches"):
        raise RuntimeError("Patch destination outside the workspace patch directory")
    target.write_bytes(patch)
    print(f"{name}: {target.relative_to(ROOT)}, {len(patch)} bytes")
