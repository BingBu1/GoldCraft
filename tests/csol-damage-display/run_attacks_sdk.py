# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze and run attack-number regressions against a verified complete SDK.

Real Session, monster/wall damage and ordinary player health commit execute.
Engine boundaries are controlled; no DLL is produced or deployed.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]
DELTA = (
    "native/csol/server/damage_notice.h", "native/csol/server/damage_notice.cpp",
    "native/csol/server/scenario/core/scenario_messages.h", "native/csol/server/scenario/core/scenario_messages.cpp",
    "native/csol/server/scenario/monsters/scenario_monster_pool.cpp", "native/csol/server/scenario/core/scenario_breakable.cpp",
    "native/server/native_armor_commit.inc", "external/ReGameDLL_CS/regamedll/dlls/client.cpp",
    "native/csol/server/scenario/monsters/scenario_monster_ordinary.h",
)
MODES = ("monster", "wall", "player", "reset", "retire", "wall-restart")
FIXTURE_BASE = ROOT / "build/wall-machine-integration-20261106/independent-session-01/sdk-05/source"
RESOURCE_BASE = ROOT / "build/wall-machine-integration-20261106/independent-session-01/sdk-05/runs/initial/resources"



def read(path):
    return json.loads(Path(path).read_text("utf-8-sig"))


def key(path):
    return os.path.normcase(os.path.abspath(path)).replace("\\", "/").lower()


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def same(actual, expected):
    return actual["bytes"] == expected["bytes"] and actual["sha256"] == expected["sha256"]


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", "utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--candidate", type=Path, required=True, help="Reviewed candidate rooted at the deployed frozen source")
    parser.add_argument("--resources", type=Path, default=RESOURCE_BASE, help="Matching immutable map/HUD resource input; run output must be separate")
    parser.add_argument("--expect-old-failures", action="store_true", help="Reuse old damage producers with new transport; require all three missing-notice controls to fail")
    parser.add_argument("--compile-only", action="store_true", help="Check frozen sources and actual includes without linking or running")
    args = parser.parse_args()
    base, out = args.baseline.resolve(), args.output.resolve()
    assert out.is_relative_to(ROOT / "build") and not out.exists(), "use a fresh workspace build directory"
    for path in (out, *out.parents):
        assert not path.is_symlink() and not path.is_junction(), path
    result = read(base / "build-result.json")
    assert result["success"] and result["inputsUnchanged"]
    assert read(base / "dependency-audit.json")["success"]
    units = read(base / "complete-object-provenance.json")["units"]
    effective = {r["relative"].replace("\\", "/"): r["frozen"] for r in read(base / "effective-source-manifest.json")["files"]}
    known = {key(r["path"]): r for r in read(base / "inputs.json")["files"]}
    observed = {}

    def verify(row):
        assert same(identity(row["path"]), row), row["path"]
        observed[key(row["path"])] = row
        return row

    for unit in units:
        verify(unit["object"])
        verify(identity(unit["command"][0]))
    for row in effective.values():
        known[key(row["path"])] = verify(row)
    out.mkdir(parents=True)
    overlay = read(base / "source-overlay.json")
    aliases = {key(r["name"]): r for r in overlay["roots"]}
    for rel, row in effective.items():
        path = out / "source" / rel
        aliases[key(path)] = dict(type="file", name=str(path), **{"external-contents": row["path"]})
    sources = {}

    def freeze(rel, source):
        source = Path(source)
        before = identity(source)
        path = out / "source" / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(source.read_bytes())
        assert path.stat().st_nlink == 1 and same(identity(source), before)
        row = verify(identity(path))
        sources[rel] = dict(origin=before, frozen=row)
        known[key(path)] = row
        for virtual in (ROOT / rel, path):
            aliases[key(virtual)] = dict(type="file", name=str(virtual), **{"external-contents": str(path)})
        return path

    selected = DELTA[:4] if args.expect_old_failures else DELTA
    if args.candidate:
        for rel in selected:
            path = freeze(rel, args.candidate / rel)
            for row in aliases.values():
                if key(row["name"]).endswith("/" + rel.lower()):
                    row["external-contents"] = str(path)
    fixture_rel = "tests/csol-damage-display/attacks_sdk.cpp"
    pending = [fixture_rel]
    while pending:
        rel = pending.pop()
        if rel in sources:
            continue
        source = FIXTURE_BASE / rel if (FIXTURE_BASE / rel).is_file() else ROOT / rel
        path = freeze(rel, source)
        for token in re.findall(r'^\s*#include\s*"([^"]+)"', path.read_text("utf-8-sig"), re.M):
            child = ((ROOT / rel).parent / token).resolve()
            if token.startswith("tests/"):
                child = (ROOT / token).resolve()
            if child.is_relative_to(ROOT / "tests") and (child.is_file() or (FIXTURE_BASE / child.relative_to(ROOT)).is_file()):
                pending.append(child.relative_to(ROOT).as_posix())
    abi = known[key(ROOT / "external/ReGameDLL_CS/regamedll/engine/eiface.h")]
    verify(abi)
    body = Path(abi["path"]).read_text("utf-8-sig").split("typedef struct enginefuncs_s", 1)[1].split("} enginefuncs_t;", 1)[0]
    body = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
    names = re.findall(r"\(\s*\*\s*(pfn\w+)\s*\)", body)
    assert len(names) == len(set(names)) and names[0] == "pfnPrecacheModel" and names[-1] == "pfnEngCheckParm"
    generated = {
        "engine_edge_names.inc": "inline constexpr const char* engineEdgeNames[] = {\n" + "".join('    "' + n + '",\n' for n in names) + "};\n",
        "engine_edge_defaults.inc": "".join(f"g_engfuncs.{n}=Unsupported<{i},decltype(g_engfuncs.{n})>::Call;\n" for i, n in enumerate(names)),
    }
    for name, content in generated.items():
        path = out / "source/tests/csol-scenario-rules-session" / name
        path.write_text(content, "utf-8")
        actual = verify(identity(path))
        known[key(path)] = actual
        rel = path.relative_to(out / "source").as_posix()
        sources[rel] = dict(origin=sources.get(rel, {}).get("origin"), frozen=actual,
            generatedFrom=abi, generator="matched enginefuncs_s declaration order")
    # The console fixture adds these two standard headers to the SDK's
    # existing vector/memory dependencies. Pin their actual toolchain roots.
    for row in list(known.values()):
        p = Path(row["path"])
        if p.name == "vector" and not p.is_relative_to(ROOT):
            for name in ("cstdarg", "iostream"):
                header = p.parent / name
                known.setdefault(key(header), verify(identity(header)))
    overlay["roots"] = list(aliases.values())
    save(out / "overlay.json", overlay)
    verify(identity(out / "overlay.json"))
    actual_aliases = {key(r["name"]): r["external-contents"] for r in overlay["roots"]}
    template = next(u["command"] for u in units if Path(u["source"]["path"]).name == "scenario_monster_pool.cpp")
    flags, skip = [], False
    for token in template:
        if skip:
            skip = False
            continue
        if token == "/clang:-ivfsoverlay":
            skip = True
            continue
        if token.lower().startswith(("/fo", "/fd")) or token.lower().endswith(".cpp"):
            continue
        flags.append(token)
    flags += ["/I" + str(out / "source"), "/I" + str(ROOT), "/clang:-ivfsoverlay", "/clang:" + str(out / "overlay.json")]
    jobs, replacements, extra_objects = [], {}, []
    if args.candidate:
        compile_paths = list(selected)
        if not args.expect_old_failures:
            # A friend does not change layout, but rebuild every actual header
            # consumer instead of silently mixing differing class definitions.
            changed_dependencies = [p for p in selected if not p.endswith(".cpp")]
            for unit in units:
                if any(key(dep).endswith("/" + rel.lower()) for dep in unit["dependencies"] for rel in changed_dependencies):
                    source_key = key(unit["source"]["path"])
                    matches = [rel for rel in effective if source_key.endswith("/" + rel.lower())]
                    assert len(matches) == 1, unit["source"]
                    compile_paths.append(matches[0])
            compile_paths.append("external/ReGameDLL_CS/regamedll/dlls/player.cpp")
        for rel in dict.fromkeys(compile_paths):
            if not rel.endswith(".cpp"):
                continue
            stem = Path(rel).stem
            obj = out / (stem + ".obj")
            if not (out / "source" / rel).exists(): freeze(rel, effective[rel]["path"])
            jobs.append(("compile-" + stem, flags + [str(out / "source" / rel), "/Fo" + str(obj)]))
            original = next((u["object"]["path"] for u in units
                if key(u["source"]["path"]).endswith("/" + rel.lower())), None)
            if original: replacements[key(original)] = str(obj)
            else: extra_objects.append(str(obj))
    fixture_obj = out / "fixture.obj"
    jobs.append(("fixture", flags + [str(out / "source" / fixture_rel), "/Fo" + str(fixture_obj)]))
    link = next(j["command"] for j in read(base / "build-commands.json") if j["kind"] == "link")
    remove = ("/out:", "/pdb:", "/implib:", "/def:", "/ltcgout:", "/manifest", "/natvis:", "--intdir=", "/threads:", "/opt:lldltojobs=", "/lldltocache:")
    link = [replacements.get(key(x), x) for x in link if x.lower() != "/dll" and not x.lower().startswith(remove)]
    link += ["/SUBSYSTEM:CONSOLE", "/OUT:" + str(out / "attacks.exe"), "/PDB:" + str(out / "attacks.pdb"), "/threads:1", "/opt:lldltojobs=1", "/lldltocache:" + str(out / "lto-cache"), str(fixture_obj)] + extra_objects
    for token in link:
        path = Path(token)
        if path.is_file() and path.suffix.lower() in (".obj", ".lib", ".res"):
            verify(identity(path))
    jobs.append(("link", link))
    modes = MODES
    resource_root = args.resources.resolve()
    # Session shutdown writes its owned outbox below the game directory.
    # Freeze the small explicit test input separately from another run's files.
    run_resources = out / "resources"
    for resource in resource_root.rglob("*"):
        if not resource.is_file():
            continue
        relative = resource.relative_to(resource_root)
        if relative.as_posix().startswith("csol/scenario/outbox/"):
            continue
        assert not resource.is_symlink() and not resource.is_junction()
        original = identity(resource)
        destination = run_resources / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(resource.read_bytes())
        assert same(identity(destination), original)
        verify(identity(destination))
    jobs += [(mode, [str(out / "attacks.exe"), mode, str(run_resources)]) for mode in modes]
    if args.compile_only:
        jobs = [job for job in jobs if job[0] != "link" and job[0] not in modes]
    save(out / "plan.json", dict(baseline=identity(base / "build-result.json"), sources=sources, jobs=jobs,
        inputs=list(observed.values()), expectedOldFailures=args.expect_old_failures,
        compileOnly=args.compile_only,
        boundary="Actual complete Session monster/wall damage and ordinary player commit; controlled engine transport/world, no rendering/live gameplay."))
    environment = os.environ | read(base / "environment.json")
    steps, success = [], True
    try:
        for name, command in jobs:
            executable = command
            if name not in modes:
                rsp = out / (name + ".rsp")
                rsp.write_text(subprocess.list2cmdline(command[1:]), "utf-8")
                executable = [command[0], "@" + str(rsp)]
            started = time.monotonic()
            done = subprocess.run(executable, cwd=ROOT, env=environment, capture_output=True, timeout=900)
            log = out / (name + ".log")
            log.write_bytes(done.stdout + done.stderr)
            text = log.read_text("utf-8-sig", errors="replace")
            includes = []
            for path in sorted(set(re.findall(r"^Note: including file:\s*(.+)$", text, re.M))):
                real = actual_aliases.get(key(path.strip()), path.strip())
                if key(real) not in known:
                    candidate_header=Path(real)
                    if not candidate_header.is_relative_to(ROOT) and candidate_header.parent == Path(next(r["path"] for r in known.values() if Path(r["path"]).name=="vector" and not Path(r["path"]).is_relative_to(ROOT))).parent:
                        known[key(real)]=identity(real)
                    else: raise AssertionError("unrecorded include: " + real)
                includes.append(verify(known[key(real)]))
            if includes:
                save(out / (name + "-includes.json"), includes)
            expected = 1 if args.expect_old_failures and name in ("monster", "wall", "player") else 0
            accepted = done.returncode == expected
            if expected == 1:
                accepted &= bool(re.search(r"failures=[1-9]\d*", text)) and "EXCEPTION" not in text
            success &= accepted
            steps.append(dict(name=name, exit=done.returncode, expectedExit=expected, accepted=accepted,
                seconds=time.monotonic()-started, log=identity(log), actualIncludes=len(includes)))
            save(out / "result.json", dict(success=False, steps=steps, deployed=False))
            print(name, done.returncode, text[-2000:] if name in modes or done.returncode else "", flush=True)
            if not accepted and name not in modes:
                break
    finally:
        changed = [r["path"] for r in observed.values() if not same(identity(r["path"]), r)]
        save(out / "result.json", dict(success=success and len(steps)==len(jobs) and not changed,
            steps=steps, observedInputs=list(observed.values()), changedInputs=changed,
            fullDLL=False, deployed=False, liveGameplay=False))
    return 0 if success and len(steps)==len(jobs) and not changed else 1


if __name__ == "__main__":
    raise SystemExit(main())
