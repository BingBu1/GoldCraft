"""Build and compare the production CRC helper with original x86 host instructions.

Private raw code fixture, objects and logs stay under build. No DLL entry point
is called; only the standalone CRC routine is executed in the test process.
"""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import struct
import subprocess
import time
import zlib

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("pe_audit", ROOT / "tools/Inspect-MetaHookRuntime.py")
AUDIT = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(AUDIT)
EXPECTED = "9ba9a2db5e07598fd59afa35507a98c86162e4e15b3835177b78c11842cd2295"


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return {"path": str(path), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--seed-plan", type=Path, default=ROOT / "build/engine-crc32/plan.json")
    parser.add_argument("--output", type=Path, default=ROOT / "build/client-crc32/native")
    args = parser.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(ROOT / "build") or out.exists():
        raise ValueError("Choose a fresh output under build")
    seed = json.loads(args.seed_plan.read_text(encoding="utf-8-sig"))
    compiler = Path(seed["compiler"]["path"])
    environment_file = Path(seed["environment"]["path"])
    if identity(compiler) != seed["compiler"] or identity(environment_file) != seed["environment"]:
        raise ValueError("Seed compiler/environment identity changed")
    pe = AUDIT.PE(ROOT / "sandbox/cs-client-b/Half-Life/hw.dll")
    if pe.sha256 != EXPECTED:
        raise ValueError("Unsupported host identity")
    data = pe.data
    header = struct.unpack_from("<I", data, 60)[0]
    optional = header + 24
    if struct.unpack_from("<I", data, optional + 28)[0] != 0x10000000:
        raise ValueError("Unexpected preferred host base")
    reloc_rva, reloc_size = struct.unpack_from("<2I", data, optional + 96 + 5 * 8)
    reloc = data[pe.offset(reloc_rva):pe.offset(reloc_rva) + reloc_size]
    offsets, cursor = [], 0
    while cursor < len(reloc):
        page, size = struct.unpack_from("<2I", reloc, cursor)
        if size < 8 or size % 2 or cursor + size > len(reloc):
            raise ValueError("Invalid PE relocation block")
        for at in range(cursor + 8, cursor + size, 2):
            value = struct.unpack_from("<H", reloc, at)[0]
            rva, kind = page + (value & 4095), value >> 12
            if 0x1BC430 <= rva < 0x1BC63D:
                if kind != 3 or rva + 4 > 0x1BC63D:
                    raise ValueError("Unsupported CRC relocation")
                offsets.append(rva)
        cursor += size
    if not offsets or len(set(offsets)) != len(offsets):
        raise ValueError("Missing or duplicate code relocations")
    out.mkdir(parents=True)
    private_code = out / "host-crc.bin"
    private_code.write_bytes(struct.pack("<7I", 0x52434347, 1, 0x1BC430, 0x20D, 0x2BA290, 1024, len(offsets)) +
                             struct.pack("<" + "I" * len(offsets), *offsets) +
                             data[pe.offset(0x1BC430):pe.offset(0x1BC430) + 0x20D] +
                             data[pe.offset(0x2BA290):pe.offset(0x2BA290) + 1024])
    models = [Path(p).resolve(strict=True) for p in seed["models"]]
    if not models or len(set(models)) != len(models) or any(not p.is_relative_to(ROOT / "sandbox") for p in models):
        raise ValueError("Invalid sandbox model set")
    inputs = []
    for model in models:
        content = model.read_bytes()
        if not 244 <= len(content) <= 128 * 1024 * 1024:
            raise ValueError("Invalid model size")
        inputs.append({**identity(model), "crc": zlib.crc32(content) ^ 0xFFFFFFFF})
    model_list = out / "models.txt"
    model_list.write_text("\n".join(map(str, models)) + "\n", encoding="utf-8")
    obj, exe = out / "test.obj", out / "test.exe"
    commands = [
        [str(compiler), "--target=i686-pc-windows-msvc", "/nologo", "/std:c++20", "/EHsc", "/MT", "/utf-8",
         "/O2", "/clang:-O3", "/clang:-flto=thin", "/arch:AVX2", "/Gy", "/Gw", "/fp:precise", "/showIncludes",
         "/c", str(HERE / "fixture.cpp"), f"/Fo{obj}"],
        [str(compiler), "--target=i686-pc-windows-msvc", "-fuse-ld=lld", "/MT", str(obj), f"/Fe{exe}",
         "/link", "/OPT:REF", "/OPT:ICF", "/OPT:LLDLTO=3", "/threads:1"],
        [str(exe), str(private_code), str(model_list)],
    ]
    sources = [identity(p) for p in (Path(__file__), HERE / "fixture.cpp", ROOT / "native/csol/client/model_crc.hpp",
                                   ROOT / "native/engine/crc32_ieee.h", private_code, model_list)]
    plan = {"host": identity(pe.path), "compiler": identity(compiler), "environment": identity(environment_file),
            "commands": commands, "sources": sources, "models": inputs, "relocations": offsets}
    (out / "plan.json").write_text(json.dumps(plan, indent=2) + "\n", encoding="utf-8")
    env = dict(os.environ)
    env.update(json.loads(environment_file.read_text(encoding="utf-8-sig")))
    env["VSLANG"] = "1033"
    result = {"passed": False, "liveGameplay": False, "commands": []}
    try:
        for name, command in zip(("compile", "link", "test"), commands):
            started = time.monotonic()
            run = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, timeout=180,
                                 creationflags=subprocess.CREATE_NO_WINDOW)
            log = out / (name + ".log")
            log.write_text(run.stdout + run.stderr, encoding="utf-8")
            result["commands"].append({"name": name, "exit": run.returncode, "seconds": time.monotonic() - started,
                                       "log": identity(log)})
            if run.returncode:
                raise RuntimeError(f"{name} failed: see {log}")
        lines = (out / "test.log").read_text(encoding="utf-8").splitlines()
        seen = []
        for line in lines:
            fields = line.split("\t")
            if fields[0] == "model":
                index = int(fields[1]); seen.append(index)
                if int(fields[2]) != inputs[index]["bytes"] or int(fields[3], 16) != inputs[index]["crc"]:
                    raise ValueError("Native host/helper result differs from zlib model oracle")
            elif fields[0] == "complete":
                result.update(checks=int(fields[1]), models=int(fields[2]), originalSeconds=float(fields[3]),
                              optimizedSeconds=float(fields[4]))
            else:
                raise ValueError("Unexpected fixture output")
        if seen != list(range(len(models))) or result.get("models") != len(models):
            raise ValueError("Incomplete model coverage")
        for row in sources + [plan["host"], plan["compiler"], plan["environment"]]:
            if identity(row["path"]) != row:
                raise ValueError("Test input changed")
        for row in inputs:
            if identity(row["path"]) != {k: v for k, v in row.items() if k != "crc"}:
                raise ValueError("Model input changed")
        result["binary"] = identity(exe)
        result["passed"] = True
    finally:
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("passed", "checks", "models", "originalSeconds", "optimizedSeconds")}))


if __name__ == "__main__":
    main()
