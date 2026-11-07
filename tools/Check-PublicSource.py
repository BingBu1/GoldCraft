"""Check an explicit public-source allowlist and the exact Git index before publication.

Reports paths/reasons, never secret values or matching source excerpts. Local
cluster identities are used only to reject accidental copies into public source.
"""
import argparse
import ast
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "publication.json"
PRIVATE_TOP = {".tools", "external", "references", "sandbox", "analysis", "build", "dist", "fabric", ".git"}
DENIED_NAMES = {"AGENTS.md", "PROJECT_STATE.md", "settings.local.json"}


def git(*args):
    return subprocess.check_output(["git", *args], cwd=ROOT)


def candidates(index_paths=None, manifest_data=None):
    if index_paths is not None and manifest_data is None:
        entries = index_entries()
        if "publication.json" not in entries:
            raise ValueError("Publication manifest is missing from the index")
        oid = entries["publication.json"][1]
        manifest_data = index_blobs([oid])[oid]
    manifest = json.loads(manifest_data.decode("utf-8")
                          if index_paths is not None else MANIFEST.read_text(encoding="utf-8"))
    if manifest["format"] != 1:
        raise ValueError("Unknown publication manifest")
    result = set(manifest["files"])
    for directory, extensions in manifest["trees"].items():
        if index_paths is not None:
            prefix = PurePosixPath(directory).parts
            for name in index_paths:
                relative = PurePosixPath(name)
                if relative.parts[:len(prefix)] == prefix and relative.suffix in extensions:
                    result.add(name)
        else:
            for file in (ROOT / directory).rglob("*"):
                if file.is_file() and file.suffix in extensions:
                    result.add(file.relative_to(ROOT).as_posix())
    for name in result:
        relative = PurePosixPath(name)
        file = ROOT / name
        if (relative.is_absolute() or not relative.parts or ".." in relative.parts
                or "\\" in name or ":" in name or relative.parts[0] in PRIVATE_TOP
                or relative.name in DENIED_NAMES):
            raise ValueError(f"Invalid public source path: {name}")
        if index_paths is None and (not file.is_file() or file.is_symlink()
                                    or not file.resolve().is_relative_to(ROOT)):
            raise ValueError(f"Invalid public source file: {name}")
    return sorted(result)


def index_entries():
    entries = {}
    for record in git("ls-files", "--stage", "-z").split(b"\0"):
        if not record:
            continue
        metadata, raw_name = record.split(b"\t", 1)
        mode, oid, stage = metadata.split()
        name = raw_name.decode("utf-8")
        if stage != b"0" or mode not in (b"100644", b"100755"):
            raise ValueError(f"Unmerged or non-regular index entry: {name}")
        if name in entries or not re.fullmatch(rb"(?:[0-9a-f]{40}|[0-9a-f]{64})", oid):
            raise ValueError("Invalid or duplicate Git index entry")
        entries[name] = (mode, oid)
    return entries


def index_paths():
    return sorted(index_entries())


def index_storage():
    paths = []
    for arguments in (("--git-path", "index"), ("--shared-index-path",)):
        name = git("rev-parse", *arguments).decode("utf-8").removesuffix("\n")
        if name:
            paths.append(ROOT / name)
    return paths


def index_identity(paths):
    identities = []
    for index, path in enumerate(paths):
        before = path.stat()
        digest = hashlib.sha256(path.read_bytes()).digest()
        after = path.stat()
        def stamp(value):
            base = (value.st_dev, value.st_ino, value.st_size)
            # Git refreshes a split index's shared-file timestamp while merely
            # reading it. Its immutable bytes and identity must still match.
            return base if index else base + (value.st_mtime_ns, value.st_ctime_ns)
        if stamp(before) != stamp(after):
            raise ValueError("Git index changed during publication scan")
        identities.append((stamp(after), digest))
    return identities


def index_blobs(oids):
    # Only validated object IDs enter this protocol. Filenames (including tabs
    # and newlines) never become revision expressions or batch commands.
    ordered = list(dict.fromkeys(oids))
    if any(not re.fullmatch(rb"(?:[0-9a-f]{40}|[0-9a-f]{64})", oid) for oid in ordered):
        raise ValueError("Invalid Git object ID")
    result = {}
    with subprocess.Popen(["git", "cat-file", "--batch"], cwd=ROOT,
                          stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                          stderr=subprocess.DEVNULL) as process:
        try:
            for oid in ordered:
                process.stdin.write(oid + b"\n")
                process.stdin.flush()
                header = process.stdout.readline(256)
                fields = header.rstrip(b"\n").split(b" ")
                if (not header.endswith(b"\n") or len(fields) != 3
                        or fields[0] != oid or fields[1] != b"blob"
                        or not re.fullmatch(rb"(?:0|[1-9][0-9]*)", fields[2])):
                    raise ValueError("Missing, non-blob or malformed Git batch object")
                size = int(fields[2])
                # The existing publication limit also bounds pipe allocation.
                if size > 2_000_000:
                    raise ValueError("Git index contains unexpectedly large content")
                payload = process.stdout.read(size + 1)
                if len(payload) != size + 1 or payload[-1:] != b"\n":
                    raise ValueError("Truncated or malformed Git batch object")
                data = payload[:-1]
                algorithm = "sha1" if len(oid) == 40 else "sha256"
                actual = hashlib.new(algorithm, b"blob " + str(size).encode("ascii") + b"\0" + data)
                if actual.hexdigest().encode("ascii") != oid:
                    raise ValueError("Git batch object does not match the requested index ID")
                result[oid] = data
            process.stdin.close()
            if process.stdout.read(1) or process.wait() != 0:
                raise ValueError("Unexpected output or failure from Git batch reader")
        except BaseException:
            process.terminate()
            process.wait()
            raise
    return result


def local_private_values():
    values = {str(ROOT), str(ROOT).replace("\\", "/")}
    paths = [ROOT / "sandbox/cluster.json", ROOT / "sandbox/headless-combat/cluster.json",
             ROOT / "settings.local.json", ROOT / ".tools/clang-toolchain.json",
             ROOT / "sandbox/cs-server/admin-deployment.json"]
    paths += [ROOT / f"sandbox/{name}/instance.json" for name in ("cs-client-a", "cs-client-b", "cs-server")]

    def visit(value, key=""):
        if isinstance(value, dict):
            for k, v in value.items():
                visit(v, k)
        elif isinstance(value, list):
            for v in value:
                visit(v, key)
        elif isinstance(value, str) and len(value) >= 12 and re.search(r"token|secret|password|session|profileId|steamId|authId|originalGame|^root$", key, re.I):
            values.update((value, value.replace("\\", "/"), value.replace("\\", "\\\\")))

    for path in paths:
        if path.exists():
            visit(json.loads(path.read_text(encoding="utf-8-sig")))
    return values


def check_blob(name, data, allowed, private):
    errors = []
    if len(data) > 2_000_000 or b"\0" in data:
        return ["binary or unexpectedly large content"]
    try:
        source = data.decode("utf-8-sig")
    except UnicodeDecodeError:
        return ["non-UTF-8 content"]
    if any(value and value in source for value in private):
        errors.append("contains a local private identity, credential or machine path")
    secret_patterns = (
        r"gh[pousr]_[A-Za-z0-9]{30,}", r"github_pat_[A-Za-z0-9_]{40,}",
        r"-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----",
        r"\bAKIA[0-9A-Z]{16}\b", r"[A-Za-z]:[\\/]+Users[\\/]+[^\\/\s]+",
    )
    if any(re.search(pattern, source) for pattern in secret_patterns):
        errors.append("credential/private-key or personal-profile pattern")
    if name.endswith(".py"):
        try:
            ast.parse(source, filename=name)
        except SyntaxError as error:
            errors.append(f"Python syntax error at line {error.lineno}")
    if name.endswith(".json"):
        try:
            json.loads(source)
        except ValueError:
            errors.append("invalid JSON")
    if name.endswith(".md"):
        for link in re.findall(r"\]\(([^)]+)\)", source):
            if re.match(r"[a-z]+://|#", link):
                continue
            target = (ROOT / name).parent / link.split("#", 1)[0]
            try:
                target_name = target.resolve().relative_to(ROOT).as_posix()
            except ValueError:
                errors.append("documentation link outside source tree")
                continue
            if target_name not in allowed:
                errors.append(f"documentation link not published: {target_name}")
    return errors


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--staged", action="store_true", help="Inspect exact index blobs and require the full allowlist")
    parser.add_argument("--write-pathspec", action="store_true", help="Write build/public-source-paths.txt as an explicit NUL-separated path list")
    args = parser.parse_args()
    entries, storage, identity, blobs = None, None, None, None
    if args.staged:
        storage = index_storage()
        identity = index_identity(storage)
        entries = index_entries()
        if index_identity(storage) != identity:
            raise ValueError("Git index changed during publication scan")
        if "publication.json" not in entries:
            raise ValueError("Publication manifest is missing from the index")
        blobs = index_blobs(entry[1] for entry in entries.values())
    staged_names = sorted(entries) if entries is not None else None
    # Publication is the exact index, including its manifest. Unstaged work
    # must not force a second checkout or enter a reviewed source checkpoint.
    allowed = set(candidates(staged_names, blobs[entries["publication.json"][1]] if args.staged else None))
    names = sorted(allowed)
    if args.staged:
        names = staged_names
        if set(names) != allowed:
            raise ValueError(f"Git index differs from allowlist: extra={sorted(set(names)-allowed)}, missing={sorted(allowed-set(names))}")
    private = local_private_values()
    errors, records = {}, []
    for name in names:
        data = blobs[entries[name][1]] if args.staged else (ROOT / name).read_bytes()
        problems = check_blob(name, data, allowed, private)
        if problems:
            errors[name] = problems
        records.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    if args.staged and (index_entries() != entries or index_identity(storage) != identity):
        raise ValueError("Git index changed during publication scan")
    report = {"scope": "git-index" if args.staged else "working-files", "fileCount": len(records),
              "bytes": sum(r["bytes"] for r in records), "errors": errors, "files": records}
    directory = ROOT / "build/publication"
    directory.mkdir(parents=True, exist_ok=True)
    (directory / (report["scope"] + "-scan.json")).write_text(json.dumps(report, indent=2), encoding="utf-8")
    if errors:
        print(json.dumps({"fileCount": len(records), "errors": errors}, indent=2))
        return 1
    if args.write_pathspec:
        path = ROOT / "build/public-source-paths.txt"
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"\0".join(n.encode() for n in names) + b"\0")
    print(json.dumps({"scope": report["scope"], "files": len(records), "bytes": report["bytes"], "errors": 0}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
