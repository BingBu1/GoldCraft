#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify actual input symbols and reject mismatched catalog data in memory."""
import argparse
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    return dict(path=str(path.resolve()), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--client", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / "build"):
        raise ValueError("Use a fresh workspace output")
    catalog = ROOT / "native/client/cof_input_10210.json"
    tool = ROOT / "tools/Build-ClientGameData.py"
    spec = importlib.util.spec_from_file_location("input_catalog", tool)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    paths = [Path(__file__), args.client, catalog, tool, ROOT / "tools/Inspect-EngineIdentity.py"]
    before = [identity(p) for p in paths]
    out.mkdir(parents=True)
    argv = sys.argv
    checks = 0
    try:
        sys.argv = [str(tool), "--client", str(args.client), "--output", str(out / "matching")]
        module.main()
        index = json.loads((out / "matching/index.json").read_text())
        version = json.loads((out / "matching" / index["versions"][0]["url"]).read_text())
        symbols = {r["symbolName"]: r for r in version["records"]}
        actual = json.loads(catalog.read_text())
        function = actual["functions"][0]
        assert symbols[function["name"]]["payload"]["func_rva"] == function["rva"]
        checks += 1
        slot = symbols["GoldCraftCofInput_MaximumSlot"]["payload"]
        assert slot["gv_rva"] == actual["maximumSlotRva"] and slot["gv_inst_disp"] == "0x2"
        checks += 1
        assert version["binaries"]["client"]["windows"]["crc64"].casefold() == actual["crc64"].casefold()
        checks += 1
        original_read = Path.read_text
        for name, modify in (
            ("sha", lambda d: d.update(sha256="0" * 64)),
            ("crc", lambda d: d.update(crc64="0" * 16)),
            ("function", lambda d: d["functions"][0].update(sha256="0" * 64)),
            ("operand", lambda d: d.update(maximumSlotRva="0x1179dc")),
            ("call", lambda d: d.update(maximumCallBytes="00" * 6)),
        ):
            changed = copy.deepcopy(actual)
            modify(changed)

            def read(path, *pos, **kw):
                return json.dumps(changed) if path.resolve() == catalog.resolve() else original_read(path, *pos, **kw)

            sys.argv = [str(tool), "--client", str(args.client), "--output", str(out / name)]
            with patch.object(Path, "read_text", read):
                try:
                    module.main()
                except ValueError as error:
                    assert "CoF" in str(error), str(error)
                    checks += 1
                else:
                    raise AssertionError("Mismatched input metadata was accepted: " + name)
            assert not (out / name).exists()
            checks += 1
    finally:
        sys.argv = argv
        stable = before == [identity(p) for p in paths]
        (out / "receipt.json").write_text(json.dumps(dict(success=checks == 13 and stable,
            inputsUnchanged=stable, checks=checks, inputs=before, sourceCopies=0,
            boundary="Actual matching catalog generation and five in-memory metadata failures; no runtime game-symbol resolver or native gameplay"), indent=2) + "\n", "utf-8")
    if checks != 13 or not stable:
        raise SystemExit(1)
    print("input gamedata checks=" + str(checks))


if __name__ == "__main__":
    main()
