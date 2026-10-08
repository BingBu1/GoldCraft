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


def candidates():
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    if manifest["format"] != 1:
        raise ValueError("Unknown publication manifest")
    result = set(manifest["files"])
    for directory, extensions in manifest["trees"].items():
        for file in (ROOT / directory).rglob("*"):
            if file.is_file() and file.suffix in extensions:
                result.add(file.relative_to(ROOT).as_posix())
    for name in result:
        relative = PurePosixPath(name)
        file = ROOT / name
        if (relative.is_absolute() or ".." in relative.parts or relative.parts[0] in PRIVATE_TOP
                or relative.name in DENIED_NAMES or not file.is_file() or file.is_symlink()
                or not file.resolve().is_relative_to(ROOT)):
            raise ValueError(f"Invalid public source path: {name}")
    return sorted(result)


def local_private_values():
    values = {str(ROOT), str(ROOT).replace("\\", "/")}
    paths = [ROOT / "sandbox/cluster.json", ROOT / "sandbox/headless-combat/cluster.json",
             ROOT / "settings.local.json", ROOT / ".tools/clang-toolchain.json"]
    paths += [ROOT / f"sandbox/{name}/instance.json" for name in ("cs-client-a", "cs-client-b", "cs-server")]

    def visit(value, key=""):
        if isinstance(value, dict):
            for k, v in value.items():
                visit(v, k)
        elif isinstance(value, list):
            for v in value:
                visit(v, key)
        elif isinstance(value, str) and len(value) >= 12 and re.search(r"token|secret|password|session|profileId|originalGame|^root$", key, re.I):
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
    allowed = set(candidates())
    names = sorted(allowed)
    if args.staged:
        names = sorted(git("ls-files", "--cached", "-z").decode().rstrip("\0").split("\0"))
        if set(names) != allowed:
            raise ValueError(f"Git index differs from allowlist: extra={sorted(set(names)-allowed)}, missing={sorted(allowed-set(names))}")
    private = local_private_values()
    errors, records = {}, []
    for name in names:
        data = git("show", ":" + name) if args.staged else (ROOT / name).read_bytes()
        problems = check_blob(name, data, allowed, private)
        if problems:
            errors[name] = problems
        records.append({"path": name, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()})
    report = {"scope": "git-index" if args.staged else "working-files", "fileCount": len(records),
              "bytes": sum(r["bytes"] for r in records), "errors": errors, "files": records}
    directory = ROOT / "analysis/publication"
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
