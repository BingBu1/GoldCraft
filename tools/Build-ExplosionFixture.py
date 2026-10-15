"""Compile the separate Mojang-named test Mod using the pinned workspace runtime.

Does not deploy, launch servers, download dependencies, or change the production JAR.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
from zipfile import ZipFile, ZIP_DEFLATED

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "tests/neoforge-explosion"
OUTPUT = ROOT / "build/neoforge-explosion-fixture"
JAVA = ROOT / ".tools/java/jdk-21.0.12.1+1/bin/javac.exe"
MINECRAFT = ROOT / ".tools/gradle-cache/caches/fabric-loom/1.21.1/neoforge/21.1.256/minecraft-merged-mojang-at-patched.jar"
LIBRARIES = ROOT / ".tools/neoforge-runtime/21.1.256/libraries"


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    for path in (SOURCE, OUTPUT, JAVA, MINECRAFT, LIBRARIES):
        assert path.resolve().is_relative_to(ROOT.resolve()), path
        for parent in (path, *path.parents):
            if parent == ROOT.parent:
                break
            assert not parent.is_symlink() and not parent.is_junction(), parent
    sources = sorted((SOURCE / "src").rglob("*.java"))
    assert sources and MINECRAFT.is_file()
    # The named, patched MC image must precede the vanilla/processor input JARs.
    dependencies = [MINECRAFT, *sorted(LIBRARIES.rglob("*.jar"))]
    classes = OUTPUT / "classes"
    assert not OUTPUT.exists(), "Use a fresh output directory; preserve prior build evidence"
    classes.mkdir(parents=True)
    command = [str(JAVA), "--release", "21", "-encoding", "UTF-8", "-proc:none",
               "-cp", os.pathsep.join(map(str, dependencies)), "-d", str(classes), *map(str, sources)]
    completed = subprocess.run(command, capture_output=True, timeout=60)
    (OUTPUT / "compile.log").write_bytes(completed.stdout + completed.stderr)
    report = dict(exitCode=completed.returncode, command=command,
                  files=[dict(path=str(p.relative_to(ROOT)), sha256=digest(p))
                         for p in [*sources, SOURCE / "neoforge.mods.toml", *dependencies]])
    (OUTPUT / "build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    completed.check_returncode()
    target = OUTPUT / "goldcraft-explosion-fixture.jar"
    with ZipFile(target, "w", ZIP_DEFLATED) as archive:
        archive.write(SOURCE / "neoforge.mods.toml", "META-INF/neoforge.mods.toml")
        for path in sorted(classes.rglob("*.class")):
            archive.write(path, path.relative_to(classes).as_posix())
    report.update(artifact=str(target.relative_to(ROOT)), sha256=digest(target), bytes=target.stat().st_size)
    (OUTPUT / "build.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: report[k] for k in ("exitCode", "artifact", "sha256", "bytes")}))


if __name__ == "__main__":
    main()
