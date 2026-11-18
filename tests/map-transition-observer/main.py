"""Check observer admission and atomic evidence retention without starting a game."""
import argparse
import importlib.util
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
SPEC = importlib.util.spec_from_file_location("transition", ROOT / "tools/Measure-ClientMapTransition.py")
transition = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(transition)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / "build") or output.exists():
        parser.error("use a new directory inside build")
    output.mkdir(parents=True)
    checks = 0
    target = output / "record.json"
    transition.save(target, {"complete": True})
    assert json.loads(target.read_text("utf-8")) == {"complete": True}
    checks += 1
    temporary = target.with_name(target.name + ".tmp")
    temporary.write_text("preserve", "utf-8")
    try:
        transition.save(target, {"complete": False})
    except FileExistsError:
        pass
    else:
        raise AssertionError("Must not overwrite an unknown temporary file")
    assert temporary.read_text("utf-8") == "preserve"
    assert json.loads(target.read_text("utf-8"))["complete"]
    checks += 1

    valid = ('map     :  zs_trace at: 0 x\nplayers :  1 active (32 max)\n'
             '# 1 "任意玩家名" 7 STEAM_0:1:42 0 1:00 12 0 127.0.0.1:50596\n')
    for text, accept in (
        (valid, True),
        (valid.replace("1 active", "2 active"), False),
        (valid.replace("127.0.0.1:50596", "8.8.8.8:27005"), False),
        (valid.replace("zs_trace", "zs_deadend"), False),
    ):
        transition.RCON.command = lambda command, text=text: text
        try:
            observed = transition.server_status("zs_trace")
        except RuntimeError:
            assert not accept
        else:
            assert accept and observed == "zs_trace"
        checks += 1

    invalid = output / "invalid.json"
    invalid.write_bytes(b"{\0}")
    transition.STATUS = invalid
    assert transition.snapshot() is None
    checks += 1
    receipt = {"success": True, "checks": checks,
               "scope": "Observer preflight and evidence retention; no live-game or performance claim"}
    (output / "receipt.json").write_text(json.dumps(receipt, indent=2) + "\n", "utf-8")
    print(json.dumps(receipt))


if __name__ == "__main__":
    main()
