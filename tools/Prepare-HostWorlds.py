"""Create empty Minecraft dimensions; host map epochs control their transient block state."""
import json
import argparse
from pathlib import Path
import zlib

root = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--loader', choices=('neoforge', 'fabric'), default='neoforge')
args = parser.parse_args()
server = root / ("sandbox/neoforge-server" if args.loader == 'neoforge' else "sandbox/mc-server")
maps = root / "sandbox/cs-client-a/Half-Life/cstrike/maps"
world = server / "goldcraft-worlds"
pack = world / "datapacks/goldcraft_host_maps"
dimensions = pack / "data/goldcraft/dimension"
dimensions.mkdir(parents=True, exist_ok=True)
(pack / "pack.mcmeta").write_text(json.dumps({"pack": {"pack_format": 48, "description": "GoldCraft isolated host map worlds"}}), encoding="utf-8")
settings = {"biome": "minecraft:plains", "features": False, "lakes": False,
            "layers": [{"height": 1, "block": "minecraft:air"}], "structure_overrides": []}
record = []
for bsp in sorted(maps.glob("*.bsp")):
    if not all(c.isalnum() or c == "_" for c in bsp.stem):
        continue
    crc = zlib.crc32(bsp.read_bytes())
    name = f"{bsp.stem.lower()}_{crc:08x}"
    data = {"type": "minecraft:overworld", "generator": {"type": "minecraft:flat", "settings": settings}}
    (dimensions / f"{name}.json").write_text(json.dumps(data), encoding="utf-8")
    record.append({"map": bsp.stem, "crc32": crc, "dimension": f"goldcraft:{name}"})
properties = server / "server.properties"
if properties.exists():
    # Preserve credentials and every unrelated setting; the previous development world remains on disk.
    values = properties.read_text(encoding="utf-8-sig").splitlines()
    updates = {"level-name": "goldcraft-worlds", "level-type": "minecraft\\:flat",
               "generator-settings": json.dumps(settings, separators=(",", ":"))}
    for key, value in updates.items():
        values = [line for line in values if not line.startswith(key + "=")]
        values.append(key + "=" + value)
    properties.write_text("\n".join(values) + "\n", encoding="utf-8")
(root / f"analysis/goldcraft-tests/host-dimensions-{args.loader}.json").write_text(json.dumps(record, indent=2), encoding="utf-8")
print(f"Prepared {len(record)} empty host-map dimensions under the isolated Minecraft server")
