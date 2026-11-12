#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify compiled client/server buy-menu revisions and their complete resource closure."""
from __future__ import annotations
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path, PurePosixPath
import re
import struct
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
MENU = re.compile(rb"models/csol/buymenu_v[0-9]+[.]json\x00")

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def compiled_menu(path: Path) -> str:
    data = path.read_bytes()
    if len(data) < 64 or data[:2] != b"MZ":
        raise ValueError(f"Not a PE module: {path.name}")
    pe = struct.unpack_from("<I", data, 60)[0]
    if pe + 26 > len(data) or data[pe:pe+4] != b"PE\0\0" or struct.unpack_from("<H", data, pe+4)[0] != 0x14c:
        raise ValueError(f"Expected x86 PE: {path.name}")
    names = {match[:-1].decode("ascii") for match in MENU.findall(data)}
    if len(names) != 1:
        raise ValueError(f"Expected one compiled buy-menu resource in {path.name}; found {sorted(names)}")
    return names.pop()

def resource_path(name: str) -> str:
    parts = PurePosixPath(name)
    if not name or parts.is_absolute() or ".." in parts.parts or chr(92) in name or ":" in name:
        raise ValueError("Unsafe menu resource path")
    return parts.as_posix()

def menu_resources(name: str, data: bytes) -> list[str]:
    value = json.loads(data.decode("utf-8-sig"))
    if not isinstance(value, dict) or value.get("version") not in (3, 4):
        raise ValueError("Unsupported buy-menu presentation schema")
    header = (ROOT / "native/csol/character_menu.hpp").read_text("utf-8-sig")
    resources = {name}
    # These two mandatory non-image consumers are the actual published contract.
    for symbol in ("MenuFontResource", "MenuTabClickResource"):
        found = re.search(symbol + r'\[\]\s*=\s*"([^"]+)"', header)
        if not found:
            raise ValueError(f"Missing menu consumer contract: {symbol}")
        resources.add(resource_path(found[1]))
    def visit(node):
        if isinstance(node, dict):
            if "path" in node:
                if not isinstance(node["path"], str):
                    raise ValueError("Invalid image resource path")
                if node["path"]:
                    resources.add(resource_path(node["path"]))
            for child in node.values():
                visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)
    visit(value)
    return sorted(resources)

def verify_pair(server: Path, client: Path, resources: dict[str, Path], http_url: str | None = None) -> dict:
    server_name, client_name = compiled_menu(server), compiled_menu(client)
    if server_name != client_name:
        raise ValueError(f"Compiled menu revisions differ: server={server_name}, client={client_name}")
    if client_name not in resources:
        raise ValueError(f"Compiled menu is absent from the resource set: {client_name}")
    names = menu_resources(client_name, resources[client_name].read_bytes())
    missing = [name for name in names if name not in resources or not resources[name].is_file() or resources[name].stat().st_size == 0]
    if missing:
        raise ValueError("Missing required menu resources: " + ", ".join(missing))
    rows = []
    for name in names:
        data = resources[name].read_bytes()
        rows.append(dict(path=name, bytes=len(data), sha256=sha(data)))
    if http_url:
        if urllib.parse.urlsplit(http_url).scheme not in ("http", "https"):
            raise ValueError("Expected HTTP resource URL")
        def download(row):
            url = http_url.rstrip("/") + "/" + urllib.parse.quote(row["path"], safe="/")
            with urllib.request.urlopen(url, timeout=15) as response:
                if response.status != 200:
                    raise ValueError("Menu HTTP status differs from 200: " + row["path"])
                data = response.read(row["bytes"] + 1)
            if len(data) != row["bytes"] or sha(data) != row["sha256"]:
                raise ValueError("Menu HTTP bytes differ: " + row["path"])
            return row["path"]
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
            list(pool.map(download, rows))
    return dict(success=True, menu=client_name, serverSha256=sha(server.read_bytes()),
                clientSha256=sha(client.read_bytes()), resources=rows, httpVerified=bool(http_url),
                boundary="Compiled revision and actual resource bytes; presentation parser, UI, gameplay and performance require separate checks")

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("server", "client", "resources", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--http-url")
    args = parser.parse_args()
    base = args.resources.resolve(strict=True)
    name = compiled_menu(args.client)
    menu = base / name
    if not menu.is_file():
        raise ValueError("Compiled menu is missing: " + name)
    resources = {name: base / name for name in menu_resources(name, menu.read_bytes())}
    result = verify_pair(args.server.resolve(strict=True), args.client.resolve(strict=True), resources, args.http_url)
    output = args.output.absolute()
    if output.exists() or not output.resolve().is_relative_to(ROOT / "build") or any(p.is_symlink() or p.is_junction() for p in (output, *output.parents)):
        raise ValueError("Use a fresh workspace build receipt")
    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("x", encoding="utf8") as stream:
        json.dump(result, stream, indent=2)
    print(json.dumps(dict(success=True, menu=result["menu"], resources=len(result["resources"]), httpVerified=result["httpVerified"])))

if __name__ == "__main__":
    main()
