"""Audit effective C++/link commands and the x86 artifacts before deployment."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def validate_command(command):
    standards = re.findall(r"[-/]std:([^\s]+)", command.lower())
    architectures = re.findall(r"[-/]arch:([^\s]+)", command.lower())
    return (standards and standards[-1] == "c++20" and
            architectures and architectures[-1] == "avx2" and
            "-o3" in command.lower() and "-flto=thin" in command.lower() and
            "--target=i686-pc-windows-msvc" in command.lower() and
            "/fp:precise" in command.lower() and "fast-math" not in command.lower())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map-delivery-fixture', action='store_true')
    parser.add_argument('--packet-entities-fixture', action='store_true')
    args = parser.parse_args()
    compiler = json.loads((ROOT / ".tools/clang-toolchain.json").read_text(encoding="utf-8-sig"))
    selected = str(Path(compiler["root"]) / "bin/clang-cl.exe").replace("\\", "/").lower()
    version = subprocess.check_output([selected, "--version"], text=True).strip()
    if version != compiler["version"].strip():
        raise ValueError("The selected compiler changed since toolchain configuration")
    resource_dir = subprocess.check_output([selected, "-print-resource-dir"], text=True).strip().replace("\\", "/").lower()
    reports = []
    for directory in ("native-clang-x86-Release", "metahook-clang-Release-sdk26100",
                      "renderer-clang-avx2-Release", "bulletphysics-clang-Release", "utilthreadtask-clang-Release",
                      "vgui2extension-clang-Release", "interpfix-clang-Release", "metahook-tests-clang-Release"):
        rows = json.loads((ROOT / "build" / directory / "compile_commands.json").read_text())
        cpp = [row for row in rows if Path(row["file"]).suffix.lower() in (".cc", ".cpp", ".cxx")]
        if not cpp or any(not validate_command(row["command"]) or
                          selected not in row["command"].replace("\\", "/").lower() for row in cpp):
            raise ValueError(f"Effective Clang/C++20/optimization command mismatch: {directory}")
        reports.append({"build": directory, "cppUnits": len(cpp), "passed": True})

    locks = json.loads((ROOT / "sources.lock.json").read_text())
    msbuild = [("ReHLDS", ROOT / locks["sources"]["ReHLDS"]["path"]),
               ("ReGameDLL", ROOT / locks["sources"]["ReGameDLL_CS"]["path"]),
               ("SyPB", ROOT / "build/sypb/obj/Release")]
    if args.map_delivery_fixture:
        msbuild.append(("ReHLDS map delivery fixture", ROOT / "build/rehlds/Delivery/obj"))
    if args.packet_entities_fixture:
        msbuild.append(("ReHLDS packet entities fixture", ROOT / "build/rehlds/Packets/obj"))
    for name, base in msbuild:
        count = 0
        logs = list(base.rglob("clang-cl.command.1.tlog"))
        for path in logs:
            lines = path.read_text(encoding="utf-16").splitlines()
            for index in range(0, len(lines) - 1, 2):
                if not any(extension in lines[index].lower() for extension in (".cpp", ".cxx", ".cc")):
                    continue
                if not validate_command(lines[index + 1]):
                    raise ValueError(f"Effective MSBuild Clang/C++20 command mismatch: {path.relative_to(ROOT)}")
                count += 1
            compiler_log = path.with_name("clang-cl.read.1.tlog").read_text(encoding="utf-16")
            # MSBuild's read tracker records opened headers, not the executed
            # compiler image. Its clang-cl command log plus the selected
            # compiler's resource headers identify the actual toolchain.
            if resource_dir + "/include/" not in compiler_log.replace("\\", "/").lower():
                raise ValueError(f"Wrong Clang resource headers recorded in {path.relative_to(ROOT)}")
        links = list(base.rglob("lld-link.command.1.tlog"))
        if not count or not links or any("/OPT:LLDLTO=3" not in path.read_text(encoding="utf-16") for path in links):
            raise ValueError(f"Missing optimized MSBuild compile/link evidence: {name}")
        reports.append({"build": name, "cppUnits": count, "passed": True})

    artifacts = []
    artifact_paths = ["build/native-x86/Release/GoldCraft.dll", "build/native-x86/Release/goldcraft_amxx.dll",
                     "build/rehlds/Release/swds.dll", "build/rehlds/Release/hlds.exe",
                     "build/rehlds/Release/filesystem_stdio.dll", "build/regamedll/Release/mp.dll",
                     "dist/metahook/MetaHook.exe", "dist/renderer/svencoop/metahook/plugins/Renderer_AVX2.dll",
                     "dist/renderer/svencoop/metahook/dlls/FreeImage/FreeImage.dll",
                     "dist/renderer/svencoop/metahook/dlls/UtilThreadTask.dll",
                     "dist/bulletphysics/svencoop/metahook/plugins/BulletPhysics.dll",
                     "dist/vgui2extension/svencoop/metahook/plugins/VGUI2Extension.dll",
                     "dist/interpfix/svencoop/metahook/plugins/InterpFix.dll",
                     "build/sypb/Release/sypb.dll", "build/sypb/Release/sypb_amxx.dll"]
    if args.map_delivery_fixture:
        artifact_paths.append("build/rehlds/Delivery/swds.dll")
    if args.packet_entities_fixture:
        artifact_paths.append("build/rehlds/Packets/swds.dll")
    for relative in artifact_paths:
        data = (ROOT / relative).read_bytes()
        header = struct.unpack_from("<I", data, 0x3c)[0]
        if data[header:header + 4] != b"PE\0\0" or struct.unpack_from("<H", data, header + 4)[0] != 0x14c:
            raise ValueError(f"Expected x86 PE artifact: {relative}")
        artifacts.append({"path": relative, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest(), "x86": True})
    report = {"compiler": version.splitlines()[0], "compilerSha256": hashlib.sha256(Path(selected).read_bytes()).hexdigest(), "standard": "C++20",
              "optimization": "O3 / ThinLTO / AVX2 / precise FP", "builds": reports, "artifacts": artifacts}
    target = ROOT / "analysis/goldcraft-tests/clang-build-audit.json"
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({"builds": len(reports), "cppUnits": sum(r["cppUnits"] for r in reports),
                      "x86Artifacts": len(artifacts), "passed": True}))


if __name__ == "__main__":
    main()
