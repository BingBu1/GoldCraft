#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Install the pinned standalone Asio headers in the workspace dependency cache."""
import argparse
import hashlib
import json
import pathlib
import tarfile
import urllib.request

ROOT = pathlib.Path(__file__).resolve().parents[1]
VERSION = "1.38.2"
TAG = "asio-1-38-2"
SHA256 = "9f2648fa483e58a6bf848d970ee0ea650ca19ed7769dfa520ed4f7b8d27af1db"
URL = f"https://github.com/chriskohlhoff/asio/archive/refs/tags/{TAG}.tar.gz"


def prepare():
    cache = ROOT / "build/dependencies"
    archive = cache / f"{TAG}.tar.gz"
    target = cache / TAG
    include = target / "include"
    cache.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix(".part")
        with urllib.request.urlopen(URL, timeout=45) as response, temporary.open("wb") as out:
            while block := response.read(1024 * 1024):
                out.write(block)
        temporary.replace(archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA256:
        raise RuntimeError(f"Asio archive identity mismatch: {archive}")
    # Reuse the verified archive; never publish its third-party contents.
    marker = target / "goldcraft-dependency.json"
    if not marker.exists():
        target.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as source:
            for member in source.getmembers():
                parts = pathlib.PurePosixPath(member.name).parts[1:]
                if not parts or not (parts[0] == "include" or
                                     parts in (("COPYING",), ("LICENSE_1_0.txt",), ("README",))):
                    continue
                path = target.joinpath(*parts).resolve()
                if not path.is_relative_to(target.resolve()) or member.issym() or member.islnk():
                    raise RuntimeError("Unsafe Asio archive member")
                if member.isdir():
                    path.mkdir(parents=True, exist_ok=True)
                elif member.isfile():
                    path.parent.mkdir(parents=True, exist_ok=True)
                    with source.extractfile(member) as data:
                        path.write_bytes(data.read())
        marker.write_text(json.dumps(dict(version=VERSION, url=URL, archiveSHA256=SHA256),
                                          indent=2) + "\n", encoding="utf-8")
    if "#define ASIO_VERSION 103802" not in (include / "asio/version.hpp").read_text():
        raise RuntimeError("Asio header version mismatch")
    return include


if __name__ == "__main__":
    argparse.ArgumentParser(description=__doc__).parse_args()
    print(prepare())
