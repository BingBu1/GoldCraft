"""Build one Nightfall updater from hash-pinned, already issued server ZIPs."""
import argparse
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import re
import runpy
from tempfile import TemporaryDirectory
from zipfile import ZIP_DEFLATED, ZipFile, ZipInfo

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ROOT / "server-packages"
LIFECYCLE = runpy.run_path(str(ROOT / "tools/server-update-lifecycle/package.py"))
SCRIPTS = (
    "Configuration.ps1", "Files.ps1", "Hash.ps1", "Lifecycle.ps1", "Nightfall.ps1",
    "Publication.ps1", "Scenario.ps1", "Update-Server.ps1", "Update-Server.cmd",
    "Restore-Server.cmd", "gameplay-defaults.json", "release-public-key.xml",
)
MANIFEST = "update-manifest.json"
SUMS = "SHA256SUMS.txt"


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def check_archive(archive, prefix):
    sums = {}
    for line in archive.read(prefix + SUMS).decode("utf-8-sig").splitlines():
        sha, name = line.split("  ", 1)
        if name in sums or not re.fullmatch("[0-9a-f]{64}", sha):
            raise ValueError("Invalid checksum entry: " + name)
        sums[name] = sha
    names = archive.namelist()
    if len(names) != len(set(names)) or set(names) != {prefix + n for n in (*sums, SUMS)}:
        raise ValueError("ZIP members differ from the issued checksum list")
    for name, sha in sums.items():
        with archive.open(prefix + name) as stream:
            if hashlib.file_digest(stream, "sha256").hexdigest() != sha:
                raise ValueError("ZIP checksum mismatch: " + name)
    return sums


def load_issued(stack, sha, path):
    path = LIFECYCLE["plain"](path)
    if not path.is_relative_to(PACKAGES) or path.suffix != ".zip" or digest(path) != sha:
        raise ValueError("Wrong issued archive: " + path.name)
    archive = stack.enter_context(ZipFile(path))
    prefix = path.stem + "/"
    sums = check_archive(archive, prefix)
    manifest = json.loads(archive.read(prefix + MANIFEST))
    if (manifest["schema"] != 1 or manifest["scope"] != "remote-deployed-delta"
            or path.stem != "GoldCraft-Server-Update-" + manifest["release"]):
        raise ValueError("Unexpected issued server manifest")
    rows = manifest["files"] + manifest["requiredBaseline"]
    inventory = {r["path"]: r for r in rows}
    if len({n.casefold() for n in inventory}) != len(rows):
        raise ValueError("Duplicate server path in " + path.name)
    for name in inventory:
        if (re.search(r'[\\:<>"|?*\x00-\x1f]', name) or any(not p or p.endswith((" ", "."))
                or re.fullmatch(r"(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\..*)?", p, re.I)
                for p in name.split("/"))):
            raise ValueError("Invalid server path: " + name)
    payload = {"payload/" + r["path"]: r for r in manifest["files"]}
    if set(sums) != set(payload) | set(SCRIPTS) | {MANIFEST, "README.md"}:
        raise ValueError("Unexpected updater layout: " + path.name)
    for member, row in payload.items():
        if sums[member] != row["sha256"] or archive.getinfo(prefix + member).file_size != row["bytes"]:
            raise ValueError("Payload differs from server manifest: " + member)
    return archive, prefix, manifest, inventory


def build(issued, release, output, readme=None):
    if len(issued) < 2 or not re.fullmatch(r"[0-9]{8}-[A-Za-z0-9-]+", release):
        raise ValueError("Supply at least two issued versions and a distinct release ID")
    output = LIFECYCLE["plain"](output)
    if output.parent != PACKAGES or output.exists():
        raise ValueError("Use a fresh directory directly under server-packages")
    name = "GoldCraft-Server-Update-" + release
    with ExitStack() as stack:
        versions = [load_issued(stack, sha, Path(path)) for sha, path in issued]
        prior_ids = [m["release"] for _, _, m, _ in versions]
        if len(set([*prior_ids, release])) != len(versions) + 1:
            raise ValueError("Duplicate release or transaction ID")
        archive, prefix, target, inventory = versions[-1]
        available = {}
        previous_inventory = set()
        previous_sequence = 0
        for source, base, meta, rows in versions:
            if (not previous_inventory.issubset(rows) or meta["bundleId"] != target["bundleId"]
                    or meta["managedSequence"] < previous_sequence
                    or meta["requiredPlugins"] != target["requiredPlugins"]):
                raise ValueError("Incompatible release chain: " + meta["release"])
            previous_inventory = set(rows)
            previous_sequence = meta["managedSequence"]
            for row in meta["files"]:
                available[row["path"], row["sha256"]] = (source, base + "payload/" + row["path"])

        files, required = [], []
        latest_payload = {row["path"] for row in target["files"]}
        for path, row in sorted(inventory.items()):
            states = [rows.get(path) for _, _, _, rows in versions]
            priors = {state["sha256"] for state in states if state}
            priors.discard(row["sha256"])
            if any(s and s["sha256"] == row["sha256"] and s["bytes"] != row["bytes"] for s in states):
                raise ValueError("Conflicting byte count: " + path)
            record = dict(path=path, bytes=row["bytes"], sha256=row["sha256"], priorSha256=sorted(priors))
            if priors or None in states or path in latest_payload:
                if (path, row["sha256"]) not in available:
                    raise ValueError("No issued target bytes for " + path)
                files.append(record)
            else:
                record["baselineRelease"] = states[0].get("baselineRelease") or prior_ids[0]
                required.append(record)
        manifest = dict(target, release=release, files=files, requiredBaseline=required)
        updater = archive.read(prefix + "Update-Server.ps1")
        old = target["release"].encode("ascii")
        if (updater.count(old) != 2 or b"$manifest.release -cne '" + old + b"'" not in updater
                or b"'.bingsang-current-update/" + old + b"'" not in updater):
            raise ValueError("Unexpected installer version or journal layout")

        staging = Path(stack.enter_context(TemporaryDirectory(dir=PACKAGES)))
        ready = staging / "sealed"
        ready.mkdir()
        package = staging / name
        package.mkdir()
        for item in SCRIPTS:
            data = archive.read(prefix + item)
            if item == "Update-Server.ps1":
                data = updater.replace(old, release.encode("ascii"))
            (package / item).write_bytes(data)
        for row in files:
            source, member = available[row["path"], row["sha256"]]
            dest = LIFECYCLE["plain"](package / "payload" / row["path"])
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_bytes(source.read(member))
        (package / MANIFEST).write_text(json.dumps(manifest, indent=2) + "\n", "utf-8", newline="\n")
        instructions = (
            f"# 服务器累计更新 {release}\n\n"
            f"适用基线为 {', '.join(prior_ids)}，也支持这些版本文件混装的服务器。\n\n"
            "1. 将整个 ZIP 解压到 GoldCraft-Servers 目录外。\n"
            "2. 双击 Update-Server.cmd。安装器会逐项校验，并跳过内容已经相同的文件。\n"
            "3. 显示 Already current 表示全套已齐。需要替换文件时，安装器先完成预检，"
            "再保存并停止正在运行的受管服务器，完成后按原脚本恢复启动。"
            "玩家退出 CS 后，通过原冰桑启动器重新启动即可获取配套的插件、符号表和着色器。\n\n"
            "只读预检可运行 Update-Server.cmd -Plan。回滚本次写入可使用同目录的 Restore-Server.cmd。\n"
        )
        if readme is not None:
            instructions = LIFECYCLE["plain"](readme).read_text("utf-8-sig")
        (package / "README.md").write_text(instructions, "utf-8-sig", newline="\r\n")
        LIFECYCLE["enable"](package, refresh_sums=True)
        date = archive.getinfo(prefix + MANIFEST).date_time
        scratch = ready / (name + ".zip")
        with ZipFile(scratch, "x", ZIP_DEFLATED) as out:
            for p in sorted(x for x in package.rglob("*") if x.is_file()):
                info = ZipInfo(name + "/" + p.relative_to(package).as_posix(), date)
                out.writestr(info, p.read_bytes(), compress_type=ZIP_DEFLATED, compresslevel=6)
        with ZipFile(scratch) as sealed:
            sums = check_archive(sealed, name + "/")
            for row in files:
                if sums["payload/" + row["path"]] != row["sha256"]:
                    raise ValueError("Sealed payload identity differs: " + row["path"])
            if (sealed.read(name + "/Update-Server.ps1") != updater.replace(old, release.encode("ascii"))
                    or sealed.read(name + "/Lifecycle.ps1") != archive.read(prefix + "Lifecycle.ps1")):
                raise ValueError("Issued installer lifecycle changed")
        sha = digest(scratch)
        receipt = dict(release=release, bases=prior_ids, inputZipSha256=[s for s, _ in issued],
                       archive=scratch.name, sha256=sha, bytes=scratch.stat().st_size, members=len(sums) + 1,
                       payload=len(files), baseline=len(required), managedSequence=target["managedSequence"])
        (ready / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", "utf-8")
        (ready / SUMS).write_text(sha + "  " + scratch.name + "\n", "ascii")
        ready.rename(output)
        return receipt


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--issued", action="append", nargs=2, required=True, metavar=("ZIP_SHA256", "ZIP"),
                        help="Repeat in release order, ending with the desired target")
    parser.add_argument("--release", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--readme", type=Path, help="Release-specific installation instructions")
    args = parser.parse_args()
    print(json.dumps(build(args.issued, args.release, args.output, args.readme), indent=2))
