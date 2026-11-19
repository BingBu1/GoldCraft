"""Check the reported new-GameDLL/old-FastDL mix against the actual issued ZIPs.

The mixed-file round trip checks package data on disk. Windows process lifecycle
evidence comes separately from the byte-matched original installer's receipt.
"""
import argparse
import base64
from concurrent.futures import ThreadPoolExecutor
from contextlib import ExitStack
import hashlib
import json
from pathlib import Path
import shutil
import subprocess
from tempfile import TemporaryDirectory
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[2]
PACKAGES = ROOT / "server-packages"
PROOF = ROOT / "build/ragdoll-release-verification-20261119"
BASE = PROOF / "server with spaces"
MP = "cs/cstrike/dlls/mp.dll"
ENGINE = "cs/swds.dll"
PUBLICATION = "cs/launcher-public/manifest.json"
GAME_DLL_SHA = "3acb7fb3faa6d7ca1b5af15359ed4d1401678597995caa5bb03d7a67034bb1be"


def require(value, message):
    if not value:
        raise ValueError(message)


def read(archive, name):
    return archive.read(Path(archive.filename).stem + "/" + name)


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def verify(package, evidence, stack):
    require(evidence.is_relative_to(ROOT / "build") and evidence.resolve() == evidence, "Use a fresh build directory")
    evidence.mkdir()
    archives = []
    for folder in ("jump-recovery-20261116", "download-connection-20261118",
                   "download-admission-20261119", "ragdoll-fastdl-20261119"):
        archive, = (PACKAGES / folder).glob("*.zip")
        archives.append(stack.enter_context(ZipFile(archive)))
    sealed = stack.enter_context(ZipFile(package))
    meta = json.loads(read(sealed, "update-manifest.json"))
    inputs = [json.loads(read(a, "update-manifest.json")) for a in archives]
    inventories = [{r["path"]: r["sha256"] for r in m["files"] + m["requiredBaseline"]} for m in inputs]
    jump, connection, _, final = inventories
    full = {r["path"]: r["sha256"] for r in meta["files"] + meta["requiredBaseline"]}
    require(full == final and full[MP] == GAME_DLL_SHA, "Wrong final inventory or reported GameDLL")
    payload = {r["path"]: r for r in meta["files"]}
    for path, row in payload.items():
        priors = {v[path] for v in inventories if path in v} - {full[path]}
        require(set(row["priorSha256"]) == priors, "Unexpected accepted hash for " + path)
        require(hashlib.sha256(read(sealed, "payload/" + path)).hexdigest() == full[path], "Wrong payload: " + path)

    def common(row):
        path = BASE / row["path"]
        return path.stat().st_size == row["bytes"] and digest(path) == row["sha256"]

    with ThreadPoolExecutor(max_workers=4) as pool:
        require(all(pool.map(common, meta["requiredBaseline"])), "Common server fixture differs")

    def plan(state):
        changes = set()
        for path, expected in full.items():
            actual = state.get(path)
            if actual == expected:
                continue
            if (path not in payload or (actual is not None and actual not in payload[path]["priorSha256"])
                    or (actual is None and path in (MP, ENGINE, "cs/filesystem_stdio.dll"))):
                raise ValueError(path)
            changes.add(path)
        return changes

    counts = {}
    for m, state in zip(inputs, inventories, strict=True):
        expected = {p for p, sha in final.items() if state.get(p) != sha}
        require(plan(state) == expected, "Wrong plan for " + m["release"])
        counts[m["release"]] = len(expected)
    require(plan(final) == set(), "Reinstall would write files")
    overlay = {r["path"]: r["sha256"] for r in inputs[-1]["files"]}
    mixed = jump | overlay
    parallel = next(p for p in payload if p.endswith("/ParallelDownload.dll"))
    require(plan(mixed) == {ENGINE, parallel}, "Wrong plan after unpacking Ragdoll over JumpRecovery")
    require(plan(connection | overlay) == {ENGINE}, "Wrong plan after unpacking Ragdoll over DownloadConnection")
    older_manifest = mixed | {PUBLICATION: connection[PUBLICATION]}
    require(plan(older_manifest) == {ENGINE, parallel, PUBLICATION}, "Wrong plan with old publication")
    require(MP not in plan(jump | {MP: GAME_DLL_SHA}), "GameDLL must be skipped when only it is current")
    for path in (MP, ENGINE, PUBLICATION, meta["requiredBaseline"][0]["path"]):
        try:
            plan(mixed | {path: "0" * 64})
        except ValueError as failure:
            require(str(failure) == path, "Rejected a different file")
        else:
            raise AssertionError("Accepted unknown SHA for " + path)

    previous = PROOF / inputs[-1]["release"] / "verification.json"
    inherited = json.loads(previous.read_bytes())
    require(inherited["passed"] and inherited["isolatedManagedProcessesRestarted"], "Missing Windows lifecycle proof")
    template = inputs[-1]["release"].encode()
    for name, expected in inherited["installerHashes"].items():
        if name.endswith((".ps1", ".cmd")):
            data = read(sealed, name)
            if name == "Update-Server.ps1":
                require(data.count(meta["release"].encode()) == 2, "Unexpected installer contract")
                data = data.replace(meta["release"].encode(), template)
            require(hashlib.sha256(data).hexdigest() == expected, "Different verified installer: " + name)

    sources = {(r["path"], r["sha256"]): a for a, m in zip(archives, inputs, strict=True) for r in m["files"]}
    # JumpRecovery inherits its engine. Read the original payload for the old
    # side of the round trip and verify it against JumpRecovery's inventory.
    nightfall, = (PACKAGES / "nightfall-20261115").glob("*.zip")
    sources[ENGINE, jump[ENGINE]] = stack.enter_context(ZipFile(nightfall))

    def issued_bytes(path, sha):
        if path in payload and sha == payload[path]["sha256"]:
            data = read(sealed, "payload/" + path)
        elif (path, sha) in sources:
            data = read(sources[path, sha], "payload/" + path)
        else:
            data = (BASE / path).read_bytes()
        require(hashlib.sha256(data).hexdigest() == sha, "Wrong fixture source for " + path)
        return data

    temp = Path(stack.enter_context(TemporaryDirectory(dir=evidence)))
    # Exercise existing, newly created and already-current files in one round trip.
    changes = sorted(plan(older_manifest))
    watched = [*changes, MP]
    folder = temp / "mixed server"
    for path in watched:
        dest = folder / path
        dest.parent.mkdir(parents=True, exist_ok=True)
        if path in older_manifest:
            dest.write_bytes(issued_bytes(path, older_manifest[path]))

    def snapshot():
        return {p: digest(folder / p) for p in watched if (folder / p).exists()}

    before = snapshot()
    stamp = (folder / MP).stat()
    backup = temp / "backup"
    for path in changes:
        dest = folder / path
        saved = backup / path
        if dest.exists():
            saved.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(dest, saved)
        fresh = dest.with_suffix(".stage")
        fresh.write_bytes(read(sealed, "payload/" + path))
        fresh.replace(dest)
    require(snapshot() == {p: full[p] for p in watched}, "Installed mixed file hashes differ")
    for path in changes:
        dest = folder / path
        saved = backup / path
        if saved.exists():
            saved.replace(dest)
        else:
            dest.unlink()
    now = (folder / MP).stat()
    require(snapshot() == before and (now.st_ino, now.st_mtime_ns) == (stamp.st_ino, stamp.st_mtime_ns),
            "Mixed state or skipped GameDLL changed through round trip")

    def publication(label, state):
        directory = temp / label
        directory.mkdir()
        signed = issued_bytes(PUBLICATION, state[PUBLICATION])
        (directory / "manifest.json").write_bytes(signed)
        release = json.loads(base64.b64decode(json.loads(signed)["payload"], validate=True))
        cores = {}
        for row in release["files"]:
            name = "blobs/" + row["sha256"] + "/" + Path(row["path"]).name
            require(state["cs/launcher-public/" + name] == row["sha256"], "Missing signed DLL")
            blob = directory / name
            blob.parent.mkdir(parents=True, exist_ok=True)
            blob.write_bytes(issued_bytes("cs/launcher-public/" + name, row["sha256"]))
            cores[row["path"]] = row["sha256"]
        return directory, cores

    current, cores = publication("current", final)
    launcher_checks = {}
    for label, state in (("jump", jump), ("connection", connection), ("current", final)):
        old, prior = (current, cores) if label == "current" else publication(label, state)
        different = sum(prior.get(p) != sha for p, sha in cores.items())
        run = temp / ("launcher-" + label)
        args = ["dotnet", str(PROOF / "launcher-roslyn/ManagedTests.dll"), "--publication-upgrade",
                str(run), str(ROOT / "dist/冰桑启动器/冰桑启动器.exe"), str(ROOT / "sandbox/cs-client-b/Half-Life"),
                str(old), str(current), str(different)]
        subprocess.run(args, check=True, timeout=150)
        result = json.loads((run / "receipt.json").read_bytes())
        require(result["passed"] and result["manifestSha256"] == final[PUBLICATION], "Wrong tested signature")
        launcher_checks[label] = result

    report = dict(archiveSha256=digest(package), payload=len(payload), verifiedCommonFiles=len(meta["requiredBaseline"]),
                  issuedPlanCounts=counts, manualRagdollOverJump=sorted(plan(mixed)),
                  manualRagdollOverConnection=sorted(plan(connection | overlay)), olderPublication=changes,
                  unknownHashCases=4, mixedFileRoundTrip=True, skippedMpIdentityAndTimePreserved=True,
                  actualLauncherHTTP=launcher_checks, identicalWindowsInstallerPriorChecks=len(inherited["checks"]),
                  windowsInstallerInvokedThisRun=False, productionDeployed=False)
    (evidence / "verification.json").write_text(json.dumps(report, indent=2) + "\n", "utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "actualLauncherHTTP"}, indent=2))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    parser.add_argument("evidence", type=Path)
    args = parser.parse_args()
    with ExitStack() as resources:
        verify(args.package.resolve(), args.evidence.absolute(), resources)
