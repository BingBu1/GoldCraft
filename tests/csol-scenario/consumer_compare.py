"""Compare production JSON with a separately generated resource/CSV oracle.

This adapter only renames fields and representations. It does not read or
reimplement production parsing or wave scheduling.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


def identity(path: Path) -> dict:
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def adapt(source: dict) -> dict:
    def document(value):
        return {"sourceHex": value["sourceBytesHex"], "records": [
            {"offset": row["byteOffset"], "fieldsHex": row["fieldsHex"]}
            for row in value["records"]]}

    def clock(value):
        return [value[key] for key in ("next", "end", "interval")]

    rows = source["waveRows"]
    by_record = {row["recordIndex"]: row for row in rows}
    rounds = source["roundRows"]
    ticks = []
    for index, tick in enumerate(source["roundTick0"]):
        programs = []
        for wave in dict.fromkeys(tick["activeBefore"]):
            if str(wave) in tick["clocks"]:
                programs.append({"wave": wave,
                    "clocks": [clock(row) for row in tick["clocks"][str(wave)]]})
        dispatch = [{"record": row["recordIndex"],
            "wave": by_record[row["recordIndex"]]["waveId"],
            "point": by_record[row["recordIndex"]]["spawnPoint"],
            "clock": clock(row)} for row in tick["dispatch"]]
        ticks.append({"record": rounds[index]["recordIndex"], "round": tick["round"],
            "activeBefore": tick["activeBefore"], "missing": tick["missing"],
            "activeAfter": tick["activeAfter"], "valid": True,
            "finished": not tick["activeAfter"], "attempts": len(dispatch),
            "dispatch": dispatch, "programs": programs})
    return {
        "waveDocument": document(source["waveDocument"]),
        "roundDocument": document(source["roundDocument"]),
        "waveRows": [{"record": row["recordIndex"], "wave": row["waveId"],
            "point": row["spawnPoint"], "start": row["startTime"],
            "end": row["endTime"], "interval": row["interval"],
            "types": row["types"], "weights": row["weights"],
            "weightTotal": row["weightTotal"], "eventHex": row["eventMessageHex"],
            "isEvent": row["spawnPoint"] == 9999} for row in rows],
        "waveGroups": [{"wave": int(wave), "point": int(point), "rows": indices}
            for wave, groups in sorted(source["waveGroups"].items(), key=lambda item: int(item[0]))
            for point, indices in sorted(groups.items(), key=lambda item: int(item[0]))],
        "roundHeaderRecord": source["roundHeader"],
        "roundRows": [{"record": row["recordIndex"], "round": row["round"],
            "waves": row["waves"]} for row in rounds],
        "roundTicks": ticks,
        "missingReferences": [{"kind": {"Wave": 0, "MonsterPoint": 1}[row["kind"]],
            "roundRecord": row["roundRecord"],
            "waveRecord": 0xFFFFFFFF if row["waveRecord"] is None else row["waveRecord"],
            "round": row["round"], "wave": row["wave"], "point": row["spawnPoint"]}
            for row in source["missingReferences"]],
    }


def compare(expected, actual, path, failures, stats):
    stats["checks"] += 1
    if type(expected) is not type(actual):
        failures.append({"path": path, "expected": expected, "actual": actual})
    elif isinstance(expected, dict):
        if set(expected) != set(actual):
            failures.append({"path": path, "expectedKeys": list(expected), "actualKeys": list(actual)})
        for key in expected.keys() & actual.keys():
            compare(expected[key], actual[key], f"{path}.{key}", failures, stats)
    elif isinstance(expected, list):
        if len(expected) != len(actual):
            failures.append({"path": path, "expectedLength": len(expected), "actualLength": len(actual)})
        for index, (left, right) in enumerate(zip(expected, actual)):
            compare(left, right, f"{path}[{index}]", failures, stats)
    elif expected != actual:
        failures.append({"path": path, "expected": expected, "actual": actual})


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("expected", type=Path)
    parser.add_argument("actual", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    oracle = json.loads(args.expected.read_text(encoding="utf-8-sig"))
    if not oracle["passed"]:
        raise ValueError("independent oracle validation did not pass")
    expected = adapt(oracle)
    actual = json.loads(args.actual.read_text(encoding="utf-8-sig"))
    failures, stats = [], {"checks": 0}
    compare(expected, actual, "$", failures, stats)
    result = {"passed": not failures, **stats, "failures": failures,
        "oracle": identity(args.expected), "actual": identity(args.actual),
        "comparisonSource": identity(Path(__file__)),
        "waveRows": len(expected["waveRows"]), "roundRows": len(expected["roundRows"]),
        "tick0Attempts": [tick["attempts"] for tick in expected["roundTicks"]],
        "missingWavesByRound": [tick["missing"] for tick in expected["roundTicks"]],
        "missingMonsterPointReferences": sum(row["kind"] == 1 for row in expected["missingReferences"]),
        "scope": "Production CSV/parser/index/reference and five tick-zero dispatch captures; no entities/gameplay"}
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({key: result[key] for key in ("passed", "checks", "waveRows", "roundRows", "tick0Attempts", "missingMonsterPointReferences")}))
    return int(bool(failures))


if __name__ == "__main__":
    raise SystemExit(main())
