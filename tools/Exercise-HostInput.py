"""Exercise real sandbox CS engine commands and record authoritative Minecraft movement."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import socket
import time
import shutil

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("minecraft_command", ROOT / "tools/Minecraft-Command.py")
rcon = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rcon)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instance", default="cs-client-a", choices=("cs-client-a", "cs-client-b"))
    parser.add_argument("--smoothing", type=int, choices=(0,1), default=1)
    parser.add_argument("--position", nargs=3, type=float, default=(12.25, 59.001, 74),
                        help="Verified flat fixture start in the cs_militia Minecraft dimension")
    args = parser.parse_args()
    config = json.loads((ROOT / "sandbox/cluster.json").read_text(encoding="utf-8-sig"))
    logs = ROOT / "sandbox" / args.instance / "logs"
    command_file = logs / "goldcraft-test-command.txt"
    command_id = max(int(command_file.read_text().split()[0]) + 1, time.time_ns() // 1_000_000)
    player = "GoldCraft_A" if args.instance.endswith("-a") else "GoldCraft_B"
    result = {"source": "CS +commands through CL_CreateMove; MC server queried over private RCON", "instance":args.instance,"player":player,"smoothing": args.smoothing, "phases": []}
    with socket.create_connection(("127.0.0.1", config["minecraftRconPort"]), timeout=10) as connection:
        rcon.exchange(connection, 1, 3, config["rconToken"])
        request = 2

        def command(text):
            nonlocal request
            request += 1
            return rcon.exchange(connection, request, 2, text)

        def host(text):
            nonlocal command_id
            command_id += 1
            temporary = command_file.with_suffix(".tmp")
            temporary.write_text(f"{command_id} {text}\n", encoding="ascii")
            temporary.replace(command_file)

        def sample():
            raw = command(f"data get entity {player} Pos")
            values = re.search(r"\[([^\]]+)\]", raw)
            if not values:
                raise RuntimeError(raw)
            position = [float(value.strip().removesuffix("d")) for value in values.group(1).split(",")]
            item = {"time": time.monotonic(), "position": position}
            for name in ("client", "server"):
                try:
                    status_path=(ROOT/"sandbox/cs-server/logs/goldcraft-server-status.json") if name=="server" else logs/"goldcraft-client-status.json"
                    item[name] = json.loads(status_path.read_text())
                except (OSError, ValueError):
                    pass
            return item

        # Fixture setup only. Every movement phase below goes through the CS engine's real input path.
        host(f"smoothing {args.smoothing}")
        time.sleep(0.15)
        host("profile 6")
        time.sleep(0.15)
        result["setup"] = command(f"execute as {player} in goldcraft:cs_militia_66f1b9b6 run tp @s {' '.join(map(str, args.position))} -90 0")
        host("view 0 0")
        time.sleep(0.6)
        for action, duration, observe in (("forward", 0.5, 1.3), ("jump", 0.12, 1.2), ("duck", 0.5, 1.2)):
            phase = {"action": action, "duration": duration, "before": sample(), "samples": []}
            host(f"{action} {duration}")
            start = time.monotonic()
            while time.monotonic() - start < observe:
                phase["samples"].append(sample())
                time.sleep(0.05)
            phase["after"] = sample()
            phase["delta"] = [b - a for a, b in zip(phase["before"]["position"], phase["after"]["position"])]
            phase["heightRise"] = max(x["position"][1] for x in phase["samples"]) - phase["before"]["position"][1]
            phase["eyeHeights"] = sorted(set(s.get("client",{}).get("minecraftEye",0) for s in phase["samples"]))
            result["phases"].append(phase)
        result["onGround"] = command(f"data get entity {player} OnGround")
        forward, jump, duck = result["phases"]
        result["checks"] = {
            "forward": 1.5 < forward["delta"][0] < 3.0 and abs(forward["delta"][1]) < 0.01 and abs(forward["delta"][2]) < 0.01,
            "jump": 1.1 < jump["heightRise"] < 1.35 and all(abs(v) < 0.01 for v in jump["delta"]),
            "landed": result["onGround"].endswith("1b"),
        }
        result["checks"]["crouchCamera"] = any(0.3 < eye < 1.5 for eye in duck["eyeHeights"])
        result["outcome"] = "passed for walking, jump, landing and crouch camera" if all(result["checks"].values()) else "inconclusive or failed: inspect concurrent input and runtime samples"
        time.sleep(1.5)
    output = ROOT / "analysis/goldcraft-tests" / f"cs-input-{int(time.time())}.json"
    output.write_text(json.dumps(result, indent=2), encoding="utf-8")
    if((logs / "goldcraft-motion.csv").exists()):
        shutil.copy2(logs / "goldcraft-motion.csv", output.with_suffix(".csv"))
    print(json.dumps({"report": str(output), "outcome": result["outcome"], "checks": result["checks"], "phases": [{k: v for k, v in p.items() if k in ("action", "delta", "heightRise")} for p in result["phases"]], "onGround": result["onGround"]}, indent=2))


if __name__ == "__main__":
    main()
