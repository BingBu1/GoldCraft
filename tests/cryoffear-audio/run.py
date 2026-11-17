#!/usr/bin/env python3
"""Compile the real XAudio2 service in place; no original assets are distributed."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
INPUTS = ("native/guest/cof/client/audio.hpp", "native/guest/cof/client/audio.cpp",
          "native/guest/cof/client/presentation.hpp", "native/guest/cof/aim_fov.hpp",
          "native/guest/cof/authority.hpp", "native/guest/cof/sound_protocol.hpp",
          "tests/cryoffear-audio/main.cpp", "tests/cryoffear-audio/resources.cpp", "tests/cryoffear-audio/run.py",
          "tests/cryoffear-audio/parameters.cpp", "tests/cryoffear-audio/oracle.py", "tests/cryoffear-aim/oracle.py")


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--environment", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--resources", type=Path, help="optional private sound/weapons_new directory, read only")
    parser.add_argument("--shotgun", action="store_true", help="Exercise the five existing 870 PCM samples and missing draw request")
    parser.add_argument("--sample", type=Path, help="optional private original client.dll for parameter instruction differential")
    args = parser.parse_args()
    if args.shotgun and not args.resources:parser.error("--shotgun requires --resources")
    out = args.output.resolve()
    assert out.is_relative_to(ROOT / "build")
    if out.exists():raise ValueError("Use a fresh workspace build directory")
    out.mkdir(parents=True)
    config = json.loads((ROOT / ".tools/clang-toolchain.json").read_text(encoding="utf-8-sig"))
    compiler = Path(config["root"]) / "bin/clang-cl.exe"
    linker = compiler.with_name("lld-link.exe")
    environment = os.environ.copy()
    loaded = json.loads(args.environment.read_text(encoding="utf-8-sig"))
    environment.update({k: v for k, v in loaded.items() if k in ("INCLUDE", "LIB", "LIBPATH", "PATH")})
    before = {p: sha(ROOT / p) for p in INPUTS}
    revision = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    flags = ["/nologo", "/c", "/TP", "/W4", "/WX", "/utf-8", "/EHsc", "/MT", "/std:c++20",
             "--target=i686-pc-windows-msvc", "/arch:AVX2", "/O2", "/clang:-O3", "/clang:-flto=thin",
             "/fp:precise", "/clang:-ffp-contract=off", "-Wno-overriding-option", "/DNDEBUG"]
    dependencies={ROOT/p for p in INPUTS}|{args.environment.resolve(),ROOT/".tools/clang-toolchain.json"}
    if args.sample:dependencies.add(args.sample.resolve())
    resource_inputs={}
    if args.resources:
        families=("shotgun",) if args.shotgun else ("glock","p345","revolver","vp70")
        for family in families:
            for path in sorted((args.resources/family).iterdir()):
                if path.is_file():resource_inputs[str(path.resolve())]=sha(path)
        if args.shotgun and (args.resources/"syringe/syringe_sleeve.wav").exists():
            raise ValueError("The original missing 870 draw request now exists; re-audit its consumer")
    dependency_commands=[]
    for index,source in enumerate(ROOT/p for p in INPUTS if p.endswith(".cpp")):
        dep=out/f"headers-{index}.d"
        command=[str(compiler),*(flag for flag in flags if flag!="/c"),"/clang:-M",
                 "/clang:-MF","/clang:"+str(dep),str(source)]
        dependency_commands.append(command)
        result=subprocess.run(command,cwd=ROOT,env=environment,capture_output=True,text=True,timeout=60)
        (out/f"headers-{index}.log").write_text(result.stdout+result.stderr,"utf-8")
        result.check_returncode()
        make=dep.read_text().replace(chr(92)+chr(10)," ")
        dependencies.update(Path(path.replace(chr(92)+" "," ")).resolve() for path in
                            re.findall(r"(?:\\ |[^\s])+",make.split(":",1)[1]))
    dependencies_before={str(path):sha(path) for path in sorted(dependencies)}
    compiler_before,linker_before=sha(compiler),sha(linker)
    commands = [[str(compiler), *flags, "/Fo" + str(out) + os.sep,
                 str(ROOT / "native/guest/cof/client/audio.cpp"),
                 str(ROOT / "tests/cryoffear-audio/main.cpp"), str(ROOT / "tests/cryoffear-audio/resources.cpp"),
                 str(ROOT / "tests/cryoffear-audio/parameters.cpp")]]
    for label, unit in (("lifecycle", "main"), ("resources", "resources"), ("parameters", "parameters")):
        commands.append([str(linker), "/nologo", "/machine:X86", "/subsystem:console", "/incremental:no",
                         "/OPT:REF", "/OPT:ICF", "/out:" + str(out / (label + ".exe")),
                         str(out / "audio.obj"), str(out / (unit + ".obj")), "ole32.lib"])
    logs = []
    for argv in commands:
        result = subprocess.run(argv, cwd=ROOT, env=environment, capture_output=True, text=True)
        logs.append(dict(program=Path(argv[0]).name, exitCode=result.returncode, stdout=result.stdout, stderr=result.stderr))
        (out / "build-log.json").write_text(json.dumps(logs, indent=2), encoding="utf-8")
        if result.returncode:
            print(result.stdout, result.stderr)
        result.check_returncode()
    checks = [["lifecycle", str(out / "lifecycle.exe")]]
    if args.resources:
        checks.append(["resources", str(out / "resources.exe"), str(args.resources.resolve()), *(["--shotgun"] if args.shotgun else [])])
    for label, *argv in checks:
        result = subprocess.run(argv, capture_output=True, text=True)
        (out / (label + ".txt")).write_text(result.stdout + result.stderr, encoding="utf-8")
        print(result.stdout + result.stderr)
        result.check_returncode()
    if args.sample:
        subprocess.run([os.sys.executable, "-I", str(ROOT / "tests/cryoffear-audio/oracle.py"),
                        "--sample", str(args.sample.resolve()), "--runner", str(out / "parameters.exe"),
                        "--output", str(out / "parameter-differential.json")], cwd=ROOT, check=True)
    outputs = {}
    for name in ("lifecycle.exe", "resources.exe", "parameters.exe"):
        data = (out / name).read_bytes()
        pe = struct.unpack_from("<I", data, 0x3C)[0]
        assert struct.unpack_from("<H", data, pe + 4)[0] == 0x14C
        outputs[name] = hashlib.sha256(data).hexdigest()
    after = {p: sha(ROOT / p) for p in INPUTS}
    assert before == after
    dependencies_after={str(path):sha(path) for path in sorted(dependencies)}
    assert dependencies_before==dependencies_after and compiler_before==sha(compiler) and linker_before==sha(linker)
    assert all(sha(path)==value for path,value in resource_inputs.items())
    receipt = dict(success=True,revision=revision, sourcesBefore=before, sourcesAfter=after,
                   dependenciesBefore=dependencies_before,dependenciesAfter=dependencies_after,
                   dependencyCommands=dependency_commands,resourceInputs=resource_inputs,
                   compilerVersion=config["version"].splitlines()[0], compilerSha256=sha(compiler),
                   linkerSha256=sha(linker), flags=flags, architecture="PE32 x86", outputs=outputs,
                   realXAudio2=True, sourceSnapshots=False, gameplay=False, audibleAcceptance=False, performance=False)
    (out / "compile-receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")


if __name__ == "__main__":
    main()
