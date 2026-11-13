#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the real emergency controller, current stamina and real guest gun."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("seal", "adapter-dir", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    parser.add_argument("--glock-receipt", type=Path,
                        help="Optional independent component object; omit for a coherent current full-server baseline")
    parser.add_argument("--sealed-production", action="store_true",
                        help="Reuse all actual accepted server objects; compile only the new SDK fixture")
    parser.add_argument("--fixture", type=Path, help="Alternate in-place SDK fixture under tests")
    parser.add_argument("--groups", nargs="+", help="Explicit groups for the alternate fixture")
    args = parser.parse_args()
    if bool(args.fixture) != bool(args.groups):
        raise ValueError("An alternate fixture and explicit groups must be supplied together")
    if args.fixture and (not args.fixture.resolve().is_relative_to(ROOT / "tests") or not args.fixture.is_file()):
        raise ValueError("The alternate SDK fixture must be an existing workspace test")
    spec = importlib.util.spec_from_file_location("sdk", ROOT / "tests/csol-rebuy/run_sdk.py")
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    seal = sdk.read(args.seal)
    glock = sdk.read(args.glock_receipt) if args.glock_receipt else None
    out = sdk.checked(args.output, exists=False)
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Fresh workspace build output required")
    if args.sealed_production and glock:
        raise ValueError("An independent guest object cannot replace a sealed-production baseline")
    replaced = set() if args.sealed_production else {
        "csol_kick_human_kick.obj", "guest_cof_glock_weapon.obj",
        "csol_custom_action_stamina.obj", "csol_custom_stamina.obj"}
    support = [row for row in seal["linkInputs"] if Path(row["path"]).name.casefold() not in replaced]
    removed = [Path(row["path"]).name.casefold() for row in seal["linkInputs"]
               if Path(row["path"]).name.casefold() in replaced]
    if not args.sealed_production and (
        removed.count("csol_kick_human_kick.obj") != 1 or removed.count("guest_cof_glock_weapon.obj") != 1):
        raise ValueError("Expected exactly one controller and one guest gun")
    if glock:
        gun = next(row for row in glock["products"] if Path(row["path"]).name == "glock.obj")
        if not any(step["name"] == "compile-glock" and step["exit"] == 0 for step in glock["steps"]):
            raise ValueError("The reused production object was not successfully compiled")
    else:
        sdk.verify(seal["receipt"])
        full = sdk.read(seal["receipt"]["path"])
        if not full["success"] or not full["inputsUnchanged"]:
            raise ValueError("Current whole-server baseline was not accepted")
        gun = next(row for row in seal["linkInputs"] if Path(row["path"]).name.casefold() == "guest_cof_glock_weapon.obj")
    # A component object can be reused after a fixture failure, but its exact
    # source/header identity must still match. The old failed receipt is kept.
    for row in glock["inputsBefore"] if glock else []:
        path = Path(row["path"])
        if path.suffix.lower() in (".h", ".hpp", ".inc") or path.name == "glock_weapon.cpp":
            sdk.verify(row)
    env = os.environ.copy()
    for key, value in sdk.read(seal["environment"]["path"]).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    units = {
        "fixture": args.fixture.resolve() if args.fixture else ROOT / "tests/csol-emergency-kick/sdk.cpp",
        "controller": ROOT / "native/csol/server/scenario/core/human_kick.cpp",
        "action-stamina": ROOT / "native/csol/server/custom/action_stamina.cpp",
        "stamina": ROOT / "native/csol/scenario/custom/stamina.cpp",
    }
    if args.sealed_production:
        units = {"fixture": units["fixture"]}
    baseline = {row["path"].casefold(): row for row in sdk.read(seal["sourceInventory"]["path"])}
    tracked = {}
    for directory in ("native", "external/ReGameDLL_CS", "tests"):
        for path in (ROOT / directory).rglob("*"):
            if path.is_file() and path.suffix.lower() in (".cpp", ".c", ".h", ".hpp", ".inc"):
                row = sdk.identity(path)
                tracked[row["path"].casefold()] = row
    controls = [sdk.identity(path) for path in (
        args.seal, Path(__file__),
        args.adapter_dir / "engine_edge_names.inc", args.adapter_dir / "engine_edge_defaults.inc")]
    if args.glock_receipt:
        controls.append(sdk.identity(args.glock_receipt))
    consumed = {row["path"].casefold(): row for row in controls}
    for row in support + [gun, seal["compiler"], seal["linker"], seal["environment"]]:
        sdk.verify(row)
    out.mkdir()
    receipt = dict(success=False, inputsUnchanged=False, fullDLL=False, sourceCopies=0,
                   deployed=False, gameplay=False, support=support, gunObject=gun,
                   controls=controls, steps=[], products=[],
                   sealedProductionObjects=args.sealed_production,
                   boundary="Current controller/stamina/gun SDK components; not a current whole-server or visual/performance acceptance")

    def save():
        receipt["inputsBefore"] = list(consumed.values())
        (out / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", "utf8")

    def verify():
        for row in support + [gun] + list(consumed.values()) + receipt["products"]:
            sdk.verify(row)

    def run(name, command, kind, cwd=sdk.SDK_CWD):
        verify()
        receipt["activeStep"] = name
        save()
        job = command
        if kind in ("compile", "link"):
            rsp = out / (name + ".rsp")
            rsp.write_text(subprocess.list2cmdline(command[1:]), "utf8")
            job = [command[0], "@" + str(rsp)]
        started = time.monotonic()
        result = subprocess.run(job, cwd=cwd, env=env, capture_output=True, timeout=900)
        text = (result.stdout + result.stderr).decode("utf8", errors="replace")
        (out / (name + ".log")).write_text(text, "utf8")
        row = dict(name=name, command=command, kind=kind, exit=result.returncode,
                   seconds=time.monotonic() - started)
        receipt["steps"].append(row)
        if kind == "compile" and result.returncode == 0:
            for value in re.findall(r"Note: including file:\s*([^\r\n]+)", text):
                path = sdk.checked(sdk.SDK_CWD / value.strip())
                if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / "build"):
                    current = sdk.identity(path)
                    before = tracked.get(current["path"].casefold())
                    if before != current:
                        raise ValueError("Actual compiler input changed or was unrecorded: " + str(path))
                    if args.sealed_production and not path.is_relative_to(ROOT / "tests"):
                        compiled = baseline.get(current["path"].casefold())
                        if not compiled or compiled["sha256"] != current["sha256"]:
                            raise ValueError("Fixture production header differs from the actual server build: " + str(path))
                    consumed[current["path"].casefold()] = current
        if kind in ("compile", "link") and result.returncode == 0:
            prefix = "/fo" if kind == "compile" else "/out:"
            receipt["products"].append(sdk.identity(next(x[len(prefix):] for x in command if x.lower().startswith(prefix))))
        if kind == "behavior":
            matches = re.findall(r"^behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$", text, re.M)
            if matches:
                row["behaviorChecks"], row["sdkChecks"], row["failures"] = map(int, matches[0])
        receipt.pop("activeStep", None)
        save()
        print(name, result.returncode, flush=True)
        if result.returncode or kind == "behavior":
            print(text[-6500:], flush=True)
        if result.returncode:
            raise RuntimeError(name + " failed")

    try:
        flags = [x for x in seal["compileCommand"] if not x.lower().endswith(".cpp")
                 and not x.lower().startswith(("/fo", "/fp", "/yu", "/yc", "/fd"))]
        flags += ["/fp:precise", "/Y-", "/showIncludes", "/clang:-ffp-contract=off",
                  "/I" + str(ROOT), "/I" + str(args.adapter_dir.resolve())]
        for name, source in units.items():
            consumed[str(source).casefold()] = sdk.identity(source)
            run("compile-" + name, flags + ["/Fo" + str(out / (name + ".obj")), str(source)], "compile")
        remove = ("/out:", "/pdb:", "/implib:", "/def:", "/ltcgout:", "/manifest",
                  "/natvis:", "/subsystem:", "/threads:", "/opt:lldltojobs=", "/lldltocache:")
        link = [x for x in seal["linkCommand"] if x.lower() != "/dll" and not x.lower().startswith(remove)
                and Path(x).name.casefold() not in replaced]
        exe = out / "emergency-integration.exe"
        run("link", link + ["/SUBSYSTEM:CONSOLE", "/OUT:" + str(exe), "/PDB:" + str(out / "integration.pdb"),
                            "/threads:4", "/opt:lldltojobs=4", *([] if args.sealed_production else [gun["path"]]),
                            *[str(out / (name + ".obj")) for name in units]], "link")
        for group in args.groups or ("stock", "reload-pending", "reload-elapsed", "aim-attack", "noon-life", "budget-life", "config-scope", "swap-resume"):
            data = out / ("data-" + group)
            data.mkdir()
            run(group, [str(exe), group], "behavior", data)
        verify()
        receipt["success"] = receipt["inputsUnchanged"] = True
    except Exception as error:
        receipt["error"] = str(error)
        raise
    finally:
        save()


if __name__ == "__main__":
    main()
