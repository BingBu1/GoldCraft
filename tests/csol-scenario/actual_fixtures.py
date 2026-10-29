"""Build an external oracle using Python's CSV reader and recorded map inventory.

Only this generator is source. Decoded tables, BSP inventory and the generated
binary fixture remain outside publication. Point team/quota defaults are test
arguments, not evidence that an engine entity adapter is implemented.
"""
from pathlib import Path
import argparse
import collections
import csv
import hashlib
import io
import json
import struct


def evidence(path):
    data = path.read_bytes()
    return {"path": str(path.resolve()), "bytes": len(data),
            "sha256": hashlib.sha256(data).hexdigest()}


def document(path):
    raw = path.read_bytes()
    text = raw.decode("latin1")
    # line_num counts physical lines, including CRLF inside a quoted field.
    physical = text.splitlines(keepends=True)
    offsets = [0]
    for line in physical:
        offsets.append(offsets[-1] + len(line))
    reader = csv.reader(io.StringIO(text, newline=""), strict=True)
    rows, previous = [], 0
    for row in reader:
        rows.append((offsets[previous], row))
        previous = reader.line_num
    return raw, rows


class Writer:
    def __init__(self):
        self.data = bytearray()

    def u32(self, value):
        self.data += struct.pack("<I", value & 0xffffffff)

    def i32(self, value):
        self.data += struct.pack("<i", value)

    def string(self, value):
        if isinstance(value, str):
            value = value.encode("latin1")
        self.u32(len(value))
        self.data += value

    def integers(self, values):
        self.u32(len(values))
        for value in values:
            self.i32(value)

    def document(self, value):
        raw, rows = value
        self.string(raw)
        self.u32(len(rows))
        for offset, fields in rows:
            self.u32(offset)
            self.u32(len(fields))
            for field in fields:
                self.string(field)


def signed(value, bits):
    value &= (1 << bits) - 1
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def key(team, point_id):
    return signed(signed(team, 8) << 16 | signed(point_id, 16), 32)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    source = args.source.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    paths = [source / "zbs_wave_zs_lostcity-decoded.bin",
             source / "zbs_zs_lostcity-decoded.bin",
             source / "season-one-inventory.json"]
    expected = ["3792716919aabb4919a85cee625610b85bd3239a51a249e32c104b180aa5d81d",
                "10fc0bfe7956ed808d4c708e52c804a9c04764c4ad2c94808aa7e816e0e93300"]
    for path, digest in zip(paths, expected):
        assert evidence(path)["sha256"] == digest, path
    wave_doc, round_doc = document(paths[0]), document(paths[1])
    waves = []
    for record, (_, row) in enumerate(wave_doc[1][1:], 1):
        data = dict(zip(wave_doc[1][0][1], row))
        types = list(map(int, data["zombi"].split()))
        weights = list(map(int, data["zombiRatio"].split()))
        waves.append(dict(record=record, wave=int(data["waveID"]), point=int(data["spawnPoint"]),
                          start=int(data["startTime"]), end=int(data["endTime"]),
                          interval=int(data["intervalTime"]), types=types, weights=weights,
                          total=signed(sum(weights), 8), event=data["eventmessage"]))
    header = next(i for i, (_, row) in enumerate(round_doc[1]) if row[0] == "round")
    rounds = [dict(record=i, round=int(row[0]), waves=list(map(int, row[1].split())))
              for i, (_, row) in enumerate(round_doc[1]) if i > header]
    assert len(waves) == 218 and header == 6 and len(rounds) == 5
    assert rounds[0]["waves"] == [2, 3, 19, 820, 821]
    duplicates = collections.Counter((r["wave"], r["point"]) for r in waves)
    assert [(pair, n) for pair, n in duplicates.items() if n > 1] == [((1, 13), 2)]

    writer = Writer()
    writer.string("CSOL-SCENARIO-FIXTURE-1")
    writer.document(wave_doc)
    writer.document(round_doc)
    writer.u32(len(waves))
    for row in waves:
        writer.u32(row["record"])
        for field in ("wave", "point", "start", "end", "interval"):
            writer.i32(row[field])
        writer.integers(row["types"])
        writer.integers(row["weights"])
        writer.i32(row["total"])
        writer.string(row["event"])
    writer.u32(header)
    writer.u32(len(rounds))
    for row in rounds:
        writer.u32(row["record"])
        writer.i32(row["round"])
        writer.integers(row["waves"])

    inventory = json.loads(paths[2].read_text(encoding="utf8"))
    maps = [m for m in inventory["maps"] if m["currentSeasonOne"]]
    assert len(maps) == 7 and sum(m["entityCount"] for m in maps) == 3234
    writer.u32(len(maps))
    summaries, lostcity = [], None
    for item in maps:
        path = source / (item["name"] + "-entities.json")
        paths.append(path)
        info = json.loads(path.read_text(encoding="utf8"))
        bsp = Path(info["source"]["path"])
        assert evidence(bsp)["sha256"] == item["source"]["sha256"] == info["source"]["sha256"]
        paths.append(bsp)
        assert len(info["entities"]) == item["entityCount"]
        points, monsters, players = [], {}, collections.defaultdict(list)
        for entity in info["entities"]:
            fields = entity["fields"]
            if fields.get("classname") not in ("zombiespawn", "playerspawn"):
                continue
            kind = int(fields["classname"] == "playerspawn")
            team, point_id, maximum = int(fields.get("team", 0)), int(fields["id"]), int(fields.get("maxspawn", 0))
            coords = [float(v) for v in (fields["origin"] + " " + fields.get("angles", "0 0 0")).split()]
            assert len(coords) == 6
            points.append((kind, entity["index"], team, point_id, maximum, coords))
            if kind:
                players[key(team, point_id)].append(entity["index"])
            else:
                monsters[key(team, point_id)] = entity["index"]
        writer.string(item["name"])
        writer.u32(item["entityCount"])
        writer.u32(len(points))
        for kind, index, team, point_id, maximum, coords in points:
            writer.u32(kind)
            writer.u32(index)
            for value in (team, point_id, maximum):
                writer.i32(value)
            writer.data += struct.pack("<6f", *coords)
        writer.u32(len(monsters))
        for point_key, entity in sorted(monsters.items()):
            writer.i32(point_key)
            writer.u32(entity)
        writer.u32(len(players))
        for point_key, entities in sorted(players.items()):
            writer.i32(point_key)
            writer.integers(entities)
        summaries.append(dict(name=item["name"], entities=item["entityCount"],
                              monsterRecords=sum(not p[0] for p in points),
                              monsterGroups=len(monsters), playerRecords=sum(p[0] for p in points),
                              playerGroups=len(players)))
        if item["name"] == "zs_lostcity":
            lostcity = monsters

    assert lostcity is not None
    missing = []
    for row in rounds:
        for wave in row["waves"]:
            rows = [r for r in waves if r["wave"] == wave]
            if not rows:
                missing.append((0, row["record"], 0xffffffff, row["round"], wave, 0))
            for wave_row in sorted(rows, key=lambda r: r["point"]):
                point = wave_row["point"]
                if point != 9999 and key(0, point) not in lostcity:
                    missing.append((1, row["record"], wave_row["record"], row["round"], wave, point))
    missing_waves = sorted({r[4] for r in missing if r[0] == 0})
    first_points = sorted({r[5] for r in missing if r[0] == 1 and r[3] == 1})
    assert missing_waves == [820, 821, 831, 832, 833, 841] and first_points == [36, 37]
    writer.u32(len(missing))
    for kind, rr, wr, number, wave, point in missing:
        for value in (kind, rr, wr):
            writer.u32(value)
        for value in (number, wave, point):
            writer.i32(value)
    fixture = args.output / "actual-fixture.bin"
    fixture.write_bytes(writer.data)
    report = dict(scope="Offline decoded CSV and map-inventory data only; no engine/entity/default-team integration",
                  inputs=[evidence(p) for p in paths], generator=evidence(Path(__file__)),
                  fixture=evidence(fixture), waves=len(waves), roundMetadataRecords=header,
                  rounds=len(rounds), maps=summaries, missingWaves=missing_waves,
                  firstRoundMissingPoints=first_points, missingReferenceUses=len(missing),
                  cp949MetadataBytesPreserved=True,
                  pointDefaults="Explicit test caller arguments team=0, maxSpawn=0, angles=(0,0,0) for absent fields")
    (args.output / "actual-fixture.json").write_text(json.dumps(report, indent=2, ensure_ascii=False) + "\n", encoding="utf8")
    print(json.dumps({k: report[k] for k in ("waves", "roundMetadataRecords", "rounds", "maps", "missingWaves", "firstRoundMissingPoints", "missingReferenceUses")}, ensure_ascii=True))


if __name__ == "__main__":
    main()
