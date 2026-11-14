#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compare production movement arithmetic to verified original x86 windows."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
ORIGINAL_SHA256 = "0036b91c01e92ed205513f52563053a55a66a32d257efb5f476b8f1f3dde0e63"


def identity(path):
    return dict(path=str(path.resolve()), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ("original", "ida-evidence", "compiler", "environment", "output"):
        parser.add_argument("--" + key, type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / "build") or not args.original.resolve().is_relative_to(ROOT):
        raise ValueError("Use a workspace original copy and fresh build output")
    sample = args.original.read_bytes()
    if hashlib.sha256(sample).hexdigest() != ORIGINAL_SHA256:
        raise ValueError("Original CoF sample identity differs")
    pe = struct.unpack_from("<I", sample, 60)[0]
    base = struct.unpack_from("<I", sample, pe + 52)[0]
    section_table = pe + 24 + struct.unpack_from("<H", sample, pe + 20)[0]
    sections = [struct.unpack_from("<4I", sample, section_table + i * 40 + 8)
                for i in range(struct.unpack_from("<H", sample, pe + 6)[0])]

    def raw(address, size):
        for _, va, count, offset in sections:
            if va <= address - base and address - base + size <= va + count:
                return sample[offset + address - base - va:offset + address - base - va + size]
        raise ValueError("Original window is outside the mapped source")

    evidence = [args.ida_evidence / (name + ".json") for name in
                ("cof-vector-normalize-ida", "cof-pm-precap-ida", "cof-ground-percent-ida")]
    for path in evidence:
        saved = json.loads(path.read_text())["raw"]["result"][0]
        body = bytes(int(word, 16) for word in saved["data"].split())
        if body != raw(int(saved["addr"], 0), len(body)):
            raise ValueError("Saved original IDA window no longer matches the actual binary")
    # Full original Normalize body; retain the float return in ST0. Negative
    # sqrt's runtime error helper is unreachable for these finite bounded inputs.
    normalize = raw(0x100033d0, 0x1000348c - 0x100033d0)
    if normalize[-1] != 0xc3:
        raise ValueError("Original Normalize boundary changed")
    # Explicit x86 cdecl entry: preserve ESI/EDI, reserve original local scratch,
    # set pmove/side and XMM2 exactly, then execute the complete original cap.
    entry = bytes.fromhex("56 57 83 ec 1c 8b 74 24 28 8d be 6c 54 04 00 f3 0f 10 17")
    cap = raw(0x10004203, 0x100042a3 - 0x10004203)
    # Original branches end at42A9. The omitted6-byte global reload is outside
    # this arithmetic boundary; replacing it with NOPs preserves branch targets.
    limit = entry + cap + b"\x90" * 6 + bytes.fromhex("83 c4 1c 5f 5e c3")
    header = "// Generated private oracle bytes; never publish this artifact.\n"
    for name, data in (("NormalizeCode", normalize), ("LimitCode", limit)):
        header += "inline constexpr unsigned char " + name + "[]{" + ",".join(hex(v) for v in data) + "};\n"
    source = Path(__file__).with_name("main.cpp")
    production = ROOT / "native/csol/scenario/custom/cof_ground_movement.hpp"
    paths = [Path(__file__), source, production, args.original, *evidence, args.compiler, args.environment]
    before = [identity(path) for path in paths]
    env = dict(os.environ)
    for key, value in json.loads(args.environment.read_text("utf-8-sig")).items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    out.mkdir(parents=True)
    (out / "original_oracle.inc").write_text(header, "utf-8")
    temporary = out / "temporary"
    temporary.mkdir()
    env["TMP"] = env["TEMP"] = str(temporary)
    command = [str(args.compiler.resolve()), "/nologo", "--target=i686-pc-windows-msvc",
        "/std:c++20", "/EHsc", "/MT", "/DNOMINMAX", "/arch:AVX2", "/fp:precise",
        "/clang:-O3", "/clang:-flto=thin", "-fuse-ld=lld",
        "/I" + str(out), str(source), "/Fo" + str(out / "oracle.obj"),
        "/Fe" + str(out / "oracle.exe"), "/link", "/machine:x86", "/threads:1"]
    steps = []
    success = False
    try:
        for name, argv in (("compile", command), ("test", [str(out / "oracle.exe")])):
            done = subprocess.run(argv, cwd=out, env=env, capture_output=True, timeout=120)
            text = (done.stdout + done.stderr).decode("utf-8", "replace")
            (out / (name + ".log")).write_text(text, "utf-8")
            steps.append(dict(name=name, command=argv, exit=done.returncode))
            print(name, done.returncode, text[-4000:], flush=True)
            if done.returncode:
                raise RuntimeError(name)
        success = True
    finally:
        stable = before == [identity(path) for path in paths]
        (out / "receipt.json").write_text(json.dumps(dict(success=success and stable,
            inputsUnchanged=stable, inputs=before, steps=steps, sourceCopies=0, deployed=False,
            gameplay=False, performance=False, originalArithmeticOnly=True,
            boundary="Verified original Normalize/cap bytes in a separate self-test x86 process, relocated constants and controlled entry scratch; no game process injection, movement/prediction or FPS acceptance"), indent=2) + "\n", "utf-8")
    if not success or not stable:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
