"""Compare render-frame camera motion from sandbox captures, excluding acceleration/startup."""
import argparse
import csv
import json
from pathlib import Path
import statistics


def analyze(path):
    with path.open(newline="") as stream:
        rows = [{key: float(value) for key, value in row.items()} for row in csv.DictReader(stream)]
    moving = [row for row in rows if row["forward"] > 100]
    if len(moving) < 10:
        raise ValueError("Insufficient forward input samples")
    start, end = moving[0]["time"] + 0.15, moving[-1]["time"] - 0.025
    steps = []
    for a, b in zip(rows, rows[1:]):
        if not start <= a["time"] < b["time"] <= end:
            continue
        dt = b["time"] - a["time"]
        steps.append({"dt": dt, "raw": b["raw_x"]-a["raw_x"], "view": b["view_x"]-a["view_x"], "echo": b["input_echo_ms"]})
    if len(steps) < 10:
        raise ValueError("Insufficient steady movement samples")
    output = {"file": str(path), "steadyFrames": len(steps), "meanFrameMs": statistics.mean(s["dt"] for s in steps)*1000,
              "maxFrameMs": max(s["dt"] for s in steps)*1000, "meanInputEchoMs": statistics.mean(s["echo"] for s in steps)}
    for name in ("raw", "view"):
        speed = [s[name]/s["dt"] for s in steps]
        output[name] = {"heldFramePercent": 100*sum(abs(s[name]) < 1e-5 for s in steps)/len(steps),
                        "maxStepGoldSrcUnits": max(abs(s[name]) for s in steps)*32,
                        "meanBlocksPerSecond": statistics.mean(speed),
                        "speedCoefficientOfVariation": statistics.pstdev(speed)/abs(statistics.mean(speed))}
    return output


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("before", type=Path)
    parser.add_argument("after", type=Path)
    args = parser.parse_args()
    report = {"before": analyze(args.before), "after": analyze(args.after)}
    report["scope"] = "Actual CS render-frame camera movement in one stationary-floor forward run per mode; broader multiplayer behavior remains unverified."
    output = args.after.parent / "walking-smoothing-comparison.json"
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
