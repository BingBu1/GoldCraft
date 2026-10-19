"""Extract locally installed, hash-pinned NightFall shaders; never redistribute them.

Only eleven allowlisted fragment programs and the test-reference vertex are copied. The source-only native
header contains identities, not copyrighted shader contents. Run outside games;
mod/resource changes require native replay reset and JVM restart.
"""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]


def checked_destination(value: Path) -> Path:
    value = value.absolute()
    # Every component is checked before resolve: junctions must not route a
    # workspace-looking path into the protected original or another runtime.
    for component in (value, *value.parents):
        if component.is_symlink() or bool(getattr(component, "is_junction", lambda: False)()):
            raise ValueError(f"Refusing linked destination: {component}")
    resolved = value.resolve()
    if not any(resolved.is_relative_to(ROOT / folder) for folder in ("sandbox", "analysis", "build")):
        raise ValueError("Output must be inside workspace sandbox, analysis or build")
    return resolved


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--jar", type=Path, default=ROOT / "sandbox/neoforge-cs-client-b/mods/NightFall-1.21.1-Neoforge-3.4.0.jar")
    parser.add_argument("--output", type=Path, default=ROOT / "sandbox/cs-client-b/Half-Life/cstrike/metahook/shaders/goldcraft/vix-post")
    parser.add_argument("--check", action="store_true", help="Verify inputs and existing destination without writing")
    args = parser.parse_args()
    header = (ROOT / "native/client/post_effect_assets.hpp").read_text(encoding="utf-8")
    pinned = re.search(r'nightfall_sha256\[\]="([a-f0-9]{64})"', header).group(1)
    assets = re.findall(r'\{"([a-z_]+\.(?:fsh|vsh))","([a-f0-9]{64})"\}', header)
    if len(assets) != 12 or len({name for name, _ in assets}) != 12:
        raise ValueError("Unexpected native shader allowlist")
    actual = hashlib.sha256(args.jar.read_bytes()).hexdigest()
    if actual != pinned:
        raise ValueError(f"NightFall identity mismatch: {actual}")
    output = checked_destination(args.output)
    if output.exists() and output.is_file():
        raise ValueError("Output must be a directory, not a file")
    payloads = {}
    with zipfile.ZipFile(args.jar) as jar:
        for name, expected in assets:
            data = jar.read("assets/efn/shaders/program/" + name)
            if not 0 < len(data) <= 16384 or hashlib.sha256(data).hexdigest() != expected:
                raise ValueError(f"Installed shader identity mismatch: {name}")
            payloads[name] = data
    changed = []
    for name, data in payloads.items():
        target = checked_destination(output / name)
        if target.exists() and target.is_dir():
            raise ValueError(f"Refusing directory where shader file is expected: {target}")
        if not target.is_file() or target.read_bytes() != data:
            changed.append(name)
    if not args.check:
        output.mkdir(parents=True, exist_ok=True)
        # Recheck after directory creation: another process must not turn an
        # expected workspace directory into a junction between validation and
        # the first write.
        output = checked_destination(output)
        for name in changed:
            target = output / name
            temporary = output / (name + ".goldcraft-tmp")
            if temporary.exists() or temporary.is_symlink():
                raise ValueError(f"Refusing to replace preexisting temporary: {temporary}")
            with temporary.open("xb") as stream:
                stream.write(payloads[name])
            temporary.replace(target)
    print(json.dumps({"jarSha256": actual, "assets": dict(assets), "output": str(output),
                      "changed": changed, "checkOnly": args.check}, indent=2))
    if args.check and changed:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
