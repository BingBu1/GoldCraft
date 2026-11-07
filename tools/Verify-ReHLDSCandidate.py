"""Register and verify a direct-working-tree ReHLDS candidate, without building it.

prepare rehashes the paths in an existing input manifest plus explicit additions.
verify checks those inputs, x86 PE/PDB identity, selected PDB source checksums,
and the exact engine object set in the successful MSBuild link command.

This is an identity check, not dependency discovery or gameplay acceptance.
No source snapshots are copied, and no runtime or Git index is modified.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_PDB_SOURCES = (
    "native/engine/model_load_profile.h",
    "native/engine/csol_loader.cpp",
    "native/engine/csol_sequence_guard.cpp",
    "external/ReHLDS/rehlds/engine/model.cpp",
    "external/ReHLDS/rehlds/engine/sv_main.cpp",
)
DISABLED_API_STRINGS = (
    "GoldCraft.SteamApproval001", "gc_resource_sync", "GoldCraft resource sync:",
)


def load(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def workspace_path(value: str | Path, *, exists: bool = True) -> Path:
    path = Path(value)
    if not path.is_absolute():
        path = ROOT / path
    path = path.resolve(strict=exists)
    if not path.is_relative_to(ROOT):
        raise ValueError(f"Expected a workspace path: {path}")
    return path


def identity(path: Path, *, timestamp: bool = False) -> dict:
    path = path.resolve(strict=True)
    before = path.stat()
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    after = path.stat()
    if (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
        raise ValueError(f"File changed while hashing: {path}")
    result = {"path": str(path), "bytes": after.st_size, "sha256": digest.hexdigest()}
    if timestamp:
        result["mtime_ns"] = after.st_mtime_ns
    return result


def key(value: str | Path) -> str:
    return str(value).replace("\\", "/").casefold()


def build_path(value: str | Path, *, exists: bool = True) -> Path:
    path = workspace_path(value, exists=exists)
    if not path.is_relative_to(ROOT / "build"):
        raise ValueError(f"Verification output must stay under workspace build/: {path}")
    return path


def write_json(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + ".tmp")
    with temporary.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    temporary.replace(path)


def llvm_tool(name: str) -> Path:
    compiler = os.environ.get("GOLDCRAFT_LLVM_ROOT")
    if not compiler:
        compiler = load(ROOT / ".tools/clang-toolchain.json")["root"]
    return (Path(compiler) / "bin" / name).resolve(strict=True)


def register_paths(seed: Path, additions: list[str]) -> list[Path]:
    data = load(seed)
    rows = data if isinstance(data, list) else data["inputs"]
    paths = {}
    for value in [*(row["path"] for row in rows), *additions]:
        path = workspace_path(value)
        if not path.is_file():
            raise ValueError(f"Registered input is not a file: {path}")
        if path.relative_to(ROOT).parts[0].casefold() in {"build", "analysis", "sandbox", "dist"}:
            raise ValueError(f"Expected direct project source, not a copied/runtime tree: {path}")
        paths[key(path)] = path
    return sorted(paths.values(), key=key)


def prepare(args) -> int:
    output = build_path(args.output, exists=False)
    manifest = output / f"{args.prefix}-inputs.json"
    if manifest.exists():
        raise ValueError(f"Input registration exists; select a new prefix: {manifest}")
    seed = workspace_path(args.seed)
    pdb_sources = list(dict.fromkeys([*DEFAULT_PDB_SOURCES, *args.pdb_source]))
    additions = [*args.add_input, *pdb_sources, str(Path(__file__)),
                 "tools/Inspect-MetaHookRuntime.py"]
    paths = register_paths(seed, additions)
    objects = output / "obj/ReHLDS/Release"
    record = {
        "schema": 1, "preparedUnixNs": time.time_ns(),
        "sourceMode": "direct working tree", "coverage": "registered inputs only",
        "seed": identity(seed), "output": str(output),
        "inputs": [identity(path) for path in paths],
        "objectsBefore": [identity(path, timestamp=True)
                          for path in sorted(objects.glob("*.obj"), key=key)],
        "pdbSources": [str(workspace_path(path)) for path in pdb_sources],
        "tools": [identity(llvm_tool(name)) for name in
                  ("clang-cl.exe", "lld-link.exe", "llvm-pdbutil.exe")],
    }
    write_json(manifest, record)
    print(json.dumps({"manifest": identity(manifest), "inputs": len(record["inputs"]),
                      "objectsBefore": len(record["objectsBefore"])}))
    return 0


def linked_objects(log: str, product: Path) -> tuple[list[Path], str]:
    lines = log.splitlines()
    starts = []
    for index, line in enumerate(lines):
        match = re.search(r'/OUT:(?:"([^"]+)"|(\S+))', line, re.IGNORECASE)
        if match and key(match[1] or match[2]) == key(product):
            starts.append(index)
    if len(starts) != 1:
        raise ValueError(f"Expected exactly one engine link command, found {len(starts)}")
    start = starts[0]
    end = next((i for i in range(start + 1, len(lines))
                if re.search(r"ReHLDS\.vcxproj\s+->", lines[i], re.IGNORECASE)), None)
    if end is None or key(product) not in key(lines[end]):
        raise ValueError("Engine link has no matching completed project record")
    command = "\n".join(lines[start:end])
    pattern = r'"([^"\r\n]+\.obj)"|((?:[A-Za-z]:[\\/]|\\\\)[^\s"]+\.obj)(?=\s|$)'
    paths = [workspace_path(match[1] or match[2])
             for match in re.finditer(pattern, command, re.IGNORECASE)]
    if not paths:
        raise ValueError("Engine link object list is empty")
    return paths, command


def read_pe(path: Path):
    spec = importlib.util.spec_from_file_location("runtime_pe", ROOT / "tools/Inspect-MetaHookRuntime.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module.PE(path)


def source_checks(pdb_text: str, paths: list[str]) -> list[dict]:
    checksums = {}
    for match in re.finditer(r"^\s*- \(MD5: ([a-fA-F0-9]{32})\) (.+?)\s*$", pdb_text, re.MULTILINE):
        checksums.setdefault(key(match[2]), set()).add(match[1].lower())
    checks = []
    for value in paths:
        path = workspace_path(value)
        md5 = hashlib.md5(path.read_bytes()).hexdigest()
        saved = sorted(checksums.get(key(path), set()))
        checks.append({"source": identity(path), "md5": md5, "pdbMd5": saved,
                       "matches": saved == [md5]})
    return checks


def compare_registered(rows: list[dict]) -> list[dict]:
    changed = []
    for row in rows:
        try:
            current = identity(Path(row["path"]))
        except (OSError, ValueError) as error:
            changed.append({"before": row, "error": str(error)})
            continue
        if current["sha256"] != row["sha256"] or current["bytes"] != row["bytes"]:
            changed.append({"before": row, "after": current})
    return changed


def verify(args) -> int:
    manifest_path = build_path(args.manifest)
    manifest = load(manifest_path)
    if manifest.get("schema") != 1:
        raise ValueError("Unsupported input registration schema")
    output = build_path(manifest["output"])
    report_path = build_path(args.report, exists=False) if args.report else output / "verification.json"
    report = {"success": False, "registration": identity(manifest_path),
              "sourceMode": "direct working tree", "coverage": "registered inputs only",
              "deployed": False, "gameplay": False}
    errors = []
    try:
        report["registeredInputCount"] = len(manifest["inputs"])
        report["changed"] = compare_registered(manifest["inputs"])
        report["inputsUnchanged"] = not report["changed"]
        report["changedTools"] = compare_registered(manifest["tools"])
        if not report["inputsUnchanged"] or report["changedTools"]:
            errors.append("Registered inputs or tools changed after prepare")
        status_path = workspace_path(args.build_status)
        status = load(status_path)
        report["buildStatus"] = identity(status_path)
        report["buildSeconds"] = status.get("seconds")
        if status.get("exitCode") != 0:
            errors.append("Build status is not exitCode 0")
        if status_path.stat().st_mtime_ns < manifest["preparedUnixNs"]:
            errors.append("Build status predates input registration")
        log_path = output / "msbuild.log"
        report["buildLog"] = identity(log_path)
        if log_path.stat().st_mtime_ns < manifest["preparedUnixNs"]:
            errors.append("Build log predates input registration")
        product, pdb = output / "swds.dll", output / "swds.pdb"
        report["products"] = [identity(product), identity(pdb)]
        pe = read_pe(product)
        report["x86"] = True
        pdb_tool = next(row for row in manifest["tools"] if Path(row["path"]).name == "llvm-pdbutil.exe")
        pdb_text = subprocess.check_output(
            [pdb_tool["path"], "dump", "-summary", "-files", str(pdb)],
            encoding="utf-8", errors="strict", timeout=60)
        guid = re.search(r"GUID: \{([^}]+)\}", pdb_text)
        age = re.search(r"Age: (\d+)", pdb_text)
        if not guid or not age:
            raise ValueError("PDB summary does not contain GUID and age")
        report["pdbGuid"], report["pdbAge"] = guid[1].lower(), int(age[1])
        report["pdbMatches"] = bool(pe.codeview and
            (report["pdbGuid"], report["pdbAge"]) == (pe.codeview["guid"], pe.codeview["age"]))
        report["pdbSourceChecks"] = source_checks(pdb_text, manifest["pdbSources"])
        if not report["pdbMatches"] or not all(row["matches"] for row in report["pdbSourceChecks"]):
            errors.append("DLL/PDB identity or selected source MD5 does not match")
        paths, command = linked_objects(log_path.read_text(encoding="utf-8-sig"), product)
        report["linkObjectReferenceCount"] = len(paths)
        # MSBuild may list the same object via AdditionalDependencies and Inputs.
        # Keep that fact visible, then compare and hash the unique input set.
        paths = list({key(path): path for path in paths}.values())
        actual_paths = list((output / "obj/ReHLDS/Release").glob("*.obj"))
        report["linkObjectSetMatches"] = {key(path) for path in paths} == {key(path) for path in actual_paths}
        report["singleJob"] = bool(re.search(r"/threads:1(?:\s|$)", command))
        report["singleJobScope"] = "engine linker; compile job limit must be supplied by build runner"
        if not report["linkObjectSetMatches"]:
            errors.append("Linked object set differs from the engine output directory")
        before = {key(row["path"]): row for row in manifest["objectsBefore"]}
        objects = [identity(path, timestamp=True) for path in sorted(paths, key=key)]
        report["engineObjectCount"], report["engineObjects"] = len(objects), objects
        report["recompiledObjects"] = [row for row in objects
            if row["mtime_ns"] != before.get(key(row["path"]), {}).get("mtime_ns")]
        report["changedObjects"] = [{"before": before.get(key(row["path"])), "after": row}
            for row in objects if row["sha256"] != before.get(key(row["path"]), {}).get("sha256")]
        report["recompiledObjectCount"] = len(report["recompiledObjects"])
        report["changedObjectCount"] = len(report["changedObjects"])
        report["disabledAPIStringsAbsent"] = {value: value.encode() not in pe.data for value in DISABLED_API_STRINGS}
        if not all(report["disabledAPIStringsAbsent"].values()):
            errors.append("Default-disabled API strings are present in engine")
        report["testEvidence"] = [identity(workspace_path(path)) for path in args.test_evidence]
        # Recheck sources/products after the potentially lengthy PDB read.
        if compare_registered(manifest["inputs"] + manifest["tools"] + report["products"] + objects + [report["buildLog"]]):
            errors.append("A registered file or product changed during verification")
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        errors.append(str(error))
    report["errors"] = errors
    report["success"] = not errors
    write_json(report_path, report)
    print(json.dumps({"success": report["success"], "report": identity(report_path), "errors": errors}))
    return 0 if report["success"] else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    actions = parser.add_subparsers(dest="action", required=True)
    before = actions.add_parser("prepare", help="Hash a declared input set before the build")
    before.add_argument("--seed", required=True, help="JSON path list or previous prepare manifest")
    before.add_argument("--output", default="build/rehlds/Release")
    before.add_argument("--prefix", default="candidate")
    before.add_argument("--add-input", action="append", default=[])
    before.add_argument("--pdb-source", action="append", default=[], help="Additional source/header whose PDB MD5 must match")
    after = actions.add_parser("verify", help="Verify a completed build against its prepare manifest")
    after.add_argument("--manifest", required=True)
    after.add_argument("--build-status", required=True, help="Build runner JSON containing exitCode and optional seconds")
    after.add_argument("--report", help="Defaults to output/verification.json")
    after.add_argument("--test-evidence", action="append", default=[], help="Record existing test evidence identity; does not rerun or interpret it")
    args = parser.parse_args()
    if args.action == "prepare" and not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_-]*", args.prefix):
        parser.error("prefix must be a simple filename stem")
    try:
        return prepare(args) if args.action == "prepare" else verify(args)
    except (OSError, ValueError, KeyError) as error:
        print(str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
