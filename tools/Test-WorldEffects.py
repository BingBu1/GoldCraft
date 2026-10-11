"""Validate release effect hooks against the pinned game and installed Mod bytecode, without a game launch."""
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    out = root / "analysis/ysm-effects/entrypoint-check"
    out.mkdir(parents=True, exist_ok=True)
    java = root / ".tools/java/jdk-21.0.12.1+1/bin"
    libs = root / ".tools/gradle/gradle-8.10.2/lib"
    classpath = ";".join(map(str, [out, libs / "asm-9.7.jar", libs / "asm-tree-9.7.jar"]))
    game = list((root / ".tools/gradle-cache/caches/fabric-loom/minecraftMaven/net/minecraft/neoforge-21.1.256-minecraft-merged-mojang").rglob("*.jar"))
    if len(game) != 1:
        raise RuntimeError("Expected one pinned 1.21.1/21.1.256 production-mapped game jar")
    mods = root / "sandbox/neoforge-cs-client-b/mods"
    inputs = [game[0], root / "neoforge/build/libs/goldcraft-neoforge-0.1.0-dev.jar",
              mods / "touhoulittlemaid-1.5.3-neoforge+mc1.21.1.jar",
              mods / "epic-fight-21.17.3.1-mc1.21.1-neoforge.jar",
              mods / "NightFall-1.21.1-Neoforge-3.4.0.jar"]
    source = root / "tests/java/EffectEntrypointsProbe.java"
    commands = [[str(java / "javac.exe"), "--release", "21", "-encoding", "UTF-8", "-cp", classpath, "-d", str(out), str(source)],
                [str(java / "java.exe"), "-cp", classpath, "EffectEntrypointsProbe", *map(str, inputs)]]
    runs = []
    for label, command in zip(("compile", "probe"), commands):
        result = subprocess.run(command, cwd=root, capture_output=True, timeout=120)
        (out / (label + ".log")).write_bytes(result.stdout + result.stderr)
        runs.append({"name": label, "exitCode": result.returncode})
        print((result.stdout + result.stderr).decode("utf-8", errors="replace"))
        if result.returncode:
            break
    report = {"runs": runs, "files": {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in [source, *inputs]},
              "scope": "Static release descriptors and installed call graph only; no game/FML/Mixin execution, pixels or input"}
    (out / "result.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    return runs[-1]["exitCode"]


if __name__ == "__main__":
    raise SystemExit(main())
