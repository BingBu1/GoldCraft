"""Extend an issued server update, keeping operator-owned files in its rollback journal."""
from pathlib import Path
import hashlib
import os
import re
import shutil

from collections.abc import Iterable

HERE = Path(__file__).resolve().parent
COF = "cs/cstrike/configs/guest/cof/"
GAMEPLAY = "cs/cstrike/addons/goldcraft/config/gameplay.json"
HOST = "cs/hlds.exe"


def digest(path: Path) -> str:
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def replace_once(text: str, old: str, new: str) -> str:
    if text.count(old) != 1:
        raise ValueError("Unexpected updater template: " + old[:75])
    return text.replace(old, new, 1)


def include_file(package: Path, files: dict, required: dict, name: str, source: Path,
                 expected: str, compatible: Iterable[str] = ()) -> None:
    if (not re.fullmatch(r"[A-Za-z0-9_./ -]+", name) or
            any(part in ("", ".", "..") for part in name.split("/"))):
        raise ValueError("Invalid package destination: " + name)
    if name.casefold() == HOST:
        # Older packages can already have a copy in their extracted payload.
        # Remove only that staging entry. The operator owns the installed host.
        for path in {name, HOST, *files, *required}:
            if path.casefold() == HOST:
                (package / "payload" / path).unlink(missing_ok=True)
                files.pop(path, None)
                required.pop(path, None)
        return
    if digest(source) != expected:
        raise ValueError("Release input changed: " + name)
    old = files.get(name) or required.get(name) or {}
    if (name in required and expected == old["sha256"] and not compatible):
        return
    prior = set(old.get("priorSha256", [])) | set(compatible)
    if old:
        prior.add(old["sha256"])
    prior.discard(expected)
    if any(not re.fullmatch(r"[0-9a-f]{64}", value) for value in prior):
        raise ValueError("Invalid preceding identity: " + name)
    target = package / "payload" / name
    target.parent.mkdir(parents=True, exist_ok=True)
    if not target.is_file() or digest(target) != expected:
        # Never write into a shared inode, including when the old file is linked.
        temp = target.with_name(target.name + ".new")
        with source.open("rb") as inp, temp.open("xb") as out:
            shutil.copyfileobj(inp, out)
        if digest(temp) != expected:
            raise ValueError("Payload staging mismatch: " + name)
        os.replace(temp, target)
    required.pop(name, None)
    files[name] = dict(path=name, bytes=target.stat().st_size, sha256=expected, priorSha256=sorted(prior))


def select_cs_lifecycle(text: str) -> str:
    entry = ". (Join-Path $PSScriptRoot 'Lifecycle.ps1')"
    return replace_once(text, entry + "\n", entry + " -CsOnly\n")


def install_policy(package: Path, old: dict, new: dict) -> None:
    install = package / "Update-Server.ps1"
    text = install.read_text("utf-8-sig").replace("\r\n", "\n")
    # Some older issued templates have CRCRLF at line endings.
    text = "\n".join(line for line in text.splitlines() if line.strip())
    for token in ("$manifest.release -cne '", "'.bingsang-current-update/"):
        text = replace_once(text, token + old["release"] + "'", token + new["release"] + "'")
    text = replace_once(text, ". (Join-Path $PSScriptRoot 'Configuration.ps1')",
                        ". (Join-Path $PSScriptRoot 'OperatorSettings.ps1')")
    for script in ("Scenario", "Nightfall"):
        text = replace_once(text, f". (Join-Path $PSScriptRoot '{script}.ps1')\n", "")
    start = text.index("function Config-Path(")
    end = text.index("function Payload-Path(", start)
    text = text[:start] + """function Config-Path([string]$Name){return [bool](Get-OperatorPolicy $Name)}
""" + text[end:]
    text = replace_once(text, "if($Name -ceq 'cs/cstrike/configs/guest/cof/scenario_damage.json'){return $true}",
                        "if(Config-Path $Name){return $true}")
    text = replace_once(text, "    $result=@(foreach($row in $manifest.files){", """    $script:configurationBytes=@{}
    $result=@(foreach($row in $manifest.files){""")
    text = replace_once(text, "        if($before -ceq $row.sha256){continue}", """        if($before -ceq $row.sha256){continue}
        $policy=Get-OperatorPolicy $row.path
        if($policy){Get-OperatorChange $row $policy $before;continue}""")
    start = text.index("    $script:configuration=@{}")
    end = text.index("    return $result", start)
    text = text[:start] + text[end:]
    text = replace_once(text, "if(!$files.ContainsKey($row.path) -or $row.after",
                        "if((Config-Path $row.path) -or !$files.ContainsKey($row.path) -or $row.after")
    text = replace_once(text, "!(Config-Path $row.path)){throw 'Invalid rollback configuration path'}",
                        "(!(Config-Path $row.path) -or !$files.ContainsKey($row.path))){throw 'Invalid rollback configuration path'}")
    old_rows = {r["path"]: r for r in old["files"] + old["requiredBaseline"]}
    new_rows = {r["path"]: r for r in new["files"] + new["requiredBaseline"]}
    for name in ("cs/swds.dll", "cs/filesystem_stdio.dll", "cs/cstrike/dlls/mp.dll"):
        text = replace_once(text, f"'{name}'='{old_rows[name]['sha256']}'", f"'{name}'='{new_rows[name]['sha256']}'")
    if any(path.casefold() == HOST for path in new_rows):
        raise ValueError("CS host launcher must stay outside the update manifest")
    text = select_cs_lifecycle(text)
    text = replace_once(text, "Mode 2 / zs_deadend ready. Ports, HTTP settings, passwords, admins, database and Minecraft preserved.",
                        "Existing CS mode, maps, ports, HTTP settings, passwords and admins preserved.")
    install.write_text(text + "\n", "utf-8-sig", newline="\n")
    shutil.copyfile(HERE / "OperatorSettings.ps1", package / "OperatorSettings.ps1")
