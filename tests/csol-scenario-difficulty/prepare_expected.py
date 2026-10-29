# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze observed resources and emit independent literal expectations, never run C++.

Uses prior research JSON only. Each saved resource must still match its recorded
identity. The scale oracle is a static translation with explicit float32 stores;
it does not execute the original DLL and must not be described as such.
"""
from pathlib import Path
import argparse
import hashlib
import json
import math
import struct

ROOT = Path(__file__).resolve().parents[2]
RESEARCH = ROOT / "analysis/csol-model-20261017/scenario"


def identity(path):
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def f32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build"):
        raise ValueError("generated research data must remain under workspace build")
    output.mkdir(parents=True, exist_ok=True)
    origins = [RESEARCH / name for name in (
        "difficulty-options/default-resource.json",
        "death-drop-table/actual-resource.json",
        "monster-types/deadend-types-expected.json")]
    observed = [json.loads(p.read_text(encoding="utf-8-sig")) for p in origins]
    difficulty, drops, monsters = observed
    assert len(difficulty["rows"]) == 7 and len(drops["rows"]) == 57 and len(monsters["rows"]) == 38
    resource_pairs = []
    for table in observed:
        record = table["source"]
        source = Path(record["path"])
        assert identity(source) == record, str(source)
        target = output / "resources" / source.name
        target.parent.mkdir(parents=True, exist_ok=True)
        data = source.read_bytes()
        if target.exists() and target.read_bytes() != data:
            raise ValueError("refusing to replace a different frozen resource")
        target.write_bytes(data)
        resource_pairs.append({"source": record, "saved": identity(target)})

    lines = ["// Generated from separately frozen research; no production parser used.",
             "#pragma once", "#include <array>", "#include <cstdint>", "#include <string_view>",
             "#include <vector>", "namespace actual_expected {",
             "inline constexpr std::array<std::string_view,7> names{"]
    lines.extend(json.dumps(row["cells"][1]) + "," for row in difficulty["rows"])
    lines += ["};", "inline constexpr std::uint32_t difficulty[7][27]{"]
    for row in difficulty["rows"]:
        # The original nonnumeric name cell converts to 0; all others are simple decimal values.
        values = [float(row["cells"][0]), 0.0] + row["numericColumns2To26Float32"]
        assert len(values) == 27
        assert [f32(float(c)) for c in row["cells"][2:]] == values[2:]
        lines.append("{" + ",".join(f"0x{bits(v):08x}u" for v in values) + "},")
    lines += ["};", "struct Drop {std::size_t row; int id,difficulty,effect; std::uint32_t probability; int owner,count,seconds,reward;};",
              "inline constexpr std::array<Drop,57> drops{{"]
    for row in drops["rows"]:
        values = [row["line"] - 1, row["id"], row["difficulty"], row["dropEffect"],
                  f"0x{bits(row['probability']):08x}u", row["ownerType"], row["count"], row["removeSeconds"], row["rewardID"]]
        lines.append("{" + ",".join(map(str, values)) + "},")
    lines += ["}};", "struct Selected {int difficulty,id; std::vector<std::size_t> rows;};", "inline const std::vector<Selected> selected{"]
    for level, rules in drops["selectionByDifficultyAsStaticTranslation"].items():
        for key, rows in rules.items():
            lines.append("{" + str(level) + "," + str(key) + ",{" + ",".join(str(n-1) for n in rows) + "}},")
    lines += ["};", "struct Monster {std::size_t record; int id,key,priority,health,speed,attack;};",
              "inline constexpr std::array<Monster,38> monsters{{"]
    effective = {row["effectiveKey"]: i for i, row in enumerate(monsters["rows"])}
    for row in monsters["rows"]:
        t = row["typed"]
        lines.append("{" + ",".join(str(x) for x in (row["record"], t["id"], row["effectiveKey"], t["priority"], t["health"], t["speed"], t["attackDamage"])) + "},")
    lines += ["}};", "struct Scale {int health,speed;};", "inline constexpr Scale scaled[7][38]{"]
    scale_observations = []
    for row in difficulty["rows"]:
        values = []
        for i, original in enumerate(monsters["rows"]):
            t = original["typed"]
            hp, speed = t["health"], t["speed"]
            if effective[original["effectiveKey"]] == i and 0 <= t["priority"] <= 2:
                col = 10 + t["priority"] * 3
                hp_float = f32(f32(hp) * f32(float(row["cells"][col])))
                speed_float = f32(f32(speed) * f32(float(row["cells"][col+1])))
                assert math.isfinite(hp_float) and -2147483648 <= hp_float < 2147483648
                assert math.isfinite(speed_float) and -2147483648 <= speed_float < 2147483648
                hp = math.trunc(hp_float)
                speed = struct.unpack("<h", struct.pack("<H", math.trunc(speed_float) & 65535))[0]
            values.append({"health": hp, "speed": speed})
        lines.append("{" + ",".join("{" + str(v["health"]) + "," + str(v["speed"]) + "}" for v in values) + "},")
        scale_observations.append({"row": row["parsedRow"], "values": values})
    lines += ["};", "}", ""]
    header = output / "actual_expected.hpp"
    header.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    scale_file = output / "scaled-expected.json"
    scale_file.write_text(json.dumps({"method": "static float32/int32 translation from prior independent typed resource observations; no original instruction execution", "rows": scale_observations}, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    manifest = {"status": "expectations prepared only; no compilation or C++ execution", "productionImported": False,
                "originalExecuted": False, "script": identity(Path(__file__)), "research": [identity(p) for p in origins],
                "resources": resource_pairs, "header": identity(header), "scaled": identity(scale_file)}
    (output / "expected-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    print(json.dumps({"resources": len(resource_pairs), "difficultyRows": 7, "dropRows": 57,
                      "typeRows": 38, "scaleRows": 266, "header": identity(header)}, ensure_ascii=False))


if __name__ == "__main__":
    main()
