"""Build the real MetaHook CRC adapter in enabled and native-baseline modes."""
import argparse
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SPEC = importlib.util.spec_from_file_location("crc_fixture", HERE / "run.py")
FIXTURE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(FIXTURE)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "build/client-crc32/adapter")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / "build") or out.exists():
        raise ValueError("Choose a fresh output under build")
    seed = json.loads((ROOT / "build/engine-crc32/plan.json").read_text(encoding="utf-8"))
    compiler, environment = seed["compiler"], seed["environment"]
    if FIXTURE.identity(compiler["path"]) != compiler or FIXTURE.identity(environment["path"]) != environment:
        raise ValueError("Compiler/environment changed")
    env = dict(os.environ)
    env.update(json.loads(Path(environment["path"]).read_text(encoding="utf-8-sig")))
    env["VSLANG"] = "1033"
    root = ROOT / "external/MetaHookSv-20261007"
    sdk = root / "MetaHook/include"
    includes = [sdk / p for p in ("", "Interface", "HLSDK/cl_dll", "HLSDK/common", "HLSDK/engine",
                                 "HLSDK/pm_shared", "HLSDK/public", "SourceSDK", "SourceSDK/tier0", "SourceSDK/tier1")]
    includes += [root / p for p in ("MetaHook/thirdparty/capstone_fork/include", "MetaHook/thirdparty/rapidjson/include",
                                   "thirdparty/glew_fork/include", "Plugins/Renderer/include/Interface")]
    flags = [compiler["path"], "--target=i686-pc-windows-msvc", "/nologo", "/std:c++20", "/EHsc", "/MT", "/utf-8",
             "/O2", "/clang:-O3", "/clang:-flto=thin", "/arch:AVX2", "/Gy", "/Gw", "/fp:precise", "/showIncludes"]
    flags += ["/D" + p for p in ("WIN32", "_WINDOWS", "_USRDLL", "_MBCS", "NO_MALLOC_OVERRIDE", "NOMINMAX",
                                "GLEW_STATIC", "GLEW_NO_GLU", "_CRT_SECURE_NO_WARNINGS")]
    flags += ["/I" + str(p) for p in includes]
    out.mkdir(parents=True)
    commands = []
    for enabled in (0, 1):
        obj, exe = out / f"adapter-{enabled}.obj", out / f"adapter-{enabled}.exe"
        commands += [
            {"name": f"compile-{enabled}", "argv": [*flags, f"/DCSOL_OPTIMIZE_HOST_CRC={enabled}", "/c",
                                                     str(HERE / "adapter.cpp"), f"/Fo{obj}"]},
            {"name": f"link-{enabled}", "argv": [compiler["path"], "--target=i686-pc-windows-msvc", "-fuse-ld=lld",
                "/MT", str(obj), f"/Fe{exe}", "/link", "/OPT:REF", "/OPT:ICF", "/threads:1"]},
            {"name": f"test-{enabled}", "argv": [str(exe)]},
        ]
    paths = [Path(__file__), HERE / "adapter.cpp", ROOT / "native/csol/client/model_crc.cpp",
             ROOT / "native/csol/client/model_crc.hpp", ROOT / "native/csol/client/runtime.hpp", ROOT / "native/engine/crc32_ieee.h"]
    sources = [FIXTURE.identity(p) for p in paths]
    plan = {"compiler": compiler, "environment": environment, "sources": sources, "commands": commands}
    (out / "plan.json").write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    result = {"passed": False, "commands": [], "includes": []}
    try:
        for command in commands:
            begin = time.monotonic()
            run = subprocess.run(command["argv"], cwd=out, env=env, text=True, capture_output=True, timeout=120,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
            log = out / (command["name"] + ".log")
            log.write_text(run.stdout + run.stderr, encoding="utf-8")
            result["commands"].append({"name": command["name"], "exit": run.returncode,
                                       "seconds": time.monotonic() - begin, "log": FIXTURE.identity(log)})
            if run.returncode:
                raise RuntimeError(f"{command['name']} failed; see {log}")
            if command["name"].startswith("compile-"):
                headers = {Path(line.split("including file:", 1)[1].strip())
                           for line in run.stdout.splitlines() if "including file:" in line}
                result["includes"].append([FIXTURE.identity(p) for p in sorted(headers)])
        if any(FIXTURE.identity(row["path"]) != row for row in sources):
            raise ValueError("Adapter source changed")
        result["passed"] = True
    finally:
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"passed": True, "enabledAndBaseline": True, "commands": len(commands)}))


if __name__ == "__main__":
    main()
