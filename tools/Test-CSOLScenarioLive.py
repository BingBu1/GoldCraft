#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Observe zs_deadend through the separate AMXX test plugin; never drive gameplay.

Deploy/reload goldcraft_scenario_ready_test.amxx and set csol_ready_test 1 first.
This program starts no processes and sends only diagnostic commands. A normal
player/Bot session must supply selection, movement, attacks, death and revival.
"""
from __future__ import annotations

import argparse
from collections import Counter
from datetime import datetime, timezone
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import re
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent
SERVER = ROOT / "sandbox/cs-server/Half-Life"
LOG = SERVER / "cstrike/addons/amxmodx/logs/csol_scenario_probe.log"
MARKER = "[CSOL live] "
MAX_LOG_BYTES = 32 * 1024 * 1024


def identity(path: Path) -> dict:
    if not path.is_file():
        return {"path": str(path.relative_to(ROOT)), "exists": False}
    return {"path": str(path.relative_to(ROOT)), "bytes": path.stat().st_size,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def safe_path(path: Path, directory: Path) -> Path:
    path = path.absolute()
    if not path.resolve().is_relative_to(directory.resolve()):
        raise ValueError(f"Path must remain under {directory}")
    for part in (path, *path.parents):
        if part.is_symlink() or (hasattr(part, "is_junction") and part.is_junction()):
            raise ValueError(f"Linked diagnostic path rejected: {part}")
    return path


def parse_line(line: str, session: str | None = None) -> dict | None:
    if MARKER not in line:
        return None
    fields = dict(re.findall(r"(?:^|\s)([a-z_]+)=([^\s]+)", line.split(MARKER, 1)[1]))
    if not all(key in fields for key in ("session", "seq", "time", "event")):
        return None
    if session is not None and fields["session"] != session:
        return None
    try:
        if int(fields["seq"]) < 1 or not math.isfinite(float(fields["time"])):
            return None
    except ValueError:
        return None
    return fields


def number(event: dict, key: str, default: float = 0) -> float:
    try:
        value = float(event.get(key, default))
        return value if math.isfinite(value) else default
    except (ValueError, TypeError):
        return default


def analyze(events: list[dict], collection_errors: list[str] | None = None) -> dict:
    if len({event.get("session") for event in events}) > 1:
        raise ValueError("Analyze exactly one recording session")
    if any(not 1 <= int(event["seq"]) <= 20001 for event in events):
        raise ValueError("Event sequence exceeds the diagnostic budget")
    events = sorted(events, key=lambda event: int(event["seq"]))
    counts = Counter(event["event"] for event in events)
    taints = [event for event in events if event["event"] in {
        "mutation", "event_limit", "damage_stack_overflow", "damage_stack_missing", "damage_stack_mismatch"}]
    evidence: dict[str, list[int]] = {name: [] for name in (
        "round_start", "freeze_finished", "ct_spawn", "death_then_spawn", "active_monsters",
        "monster_attack_damage", "weapon_damage", "weapon_kill_with_frag_credit", "wall_damage",
        "wall_broken", "marker_overlap", "clearpoint_callback", "target_use_callback",
        "wave_event_message", "round_outcome_message", "kill_chain_message")}
    deaths: dict[str, dict] = {}
    previous_round = None
    damage_calls: dict[str, dict] = {}
    cvars = None
    for event in events:
        kind, sequence = event["event"], int(event["seq"])
        if kind == "round":
            current_cvars = tuple(event.get(key) for key in ("difficulty", "freezetime", "cheats"))
            if cvars is not None and cvars != current_cvars:
                taints.append({"event": "observed_cvar_change", "seq": sequence, "before": cvars, "after": current_cvars})
            cvars = current_cvars
            if previous_round:
                # ScenarioThink resets the round clock when freeze ends too.
                # A clock increase alone is therefore not another BeginRound.
                restarted = number(event, "start") > number(previous_round, "start") and (
                    number(event, "restarts") > number(previous_round, "restarts") or
                    number(event, "round") > number(previous_round, "round"))
                if number(event, "started") == 1 and (number(previous_round, "started") == 0 or restarted):
                    evidence["round_start"].append(sequence)
                if (number(previous_round, "freeze") == 1 and number(event, "freeze") == 0
                        and number(event, "started") == 1):
                    evidence["freeze_finished"].append(sequence)
            previous_round = event
        elif kind == "player_death" and number(event, "alive") == 0 and number(event, "original") == 1:
            deaths[event["userid"]] = event
        elif kind == "player_spawn" and number(event, "alive") == 1 and number(event, "team") == 2 and number(event, "original") == 1:
            evidence["ct_spawn"].append(sequence)
            death = deaths.get(event["userid"])
            if death and number(event, "time") > number(death, "time"):
                evidence["death_then_spawn"].extend([int(death["seq"]), sequence])
                del deaths[event["userid"]]
        elif kind == "monsters" and number(event, "active") > 0 and number(event, "hp_max") > 0:
            evidence["active_monsters"].append(sequence)
        elif kind == "damage_pre":
            damage_calls[event["call"]] = event
        elif kind == "damage_post":
            before = damage_calls.get(event.get("call", ""))
            if not before or before.get("entity") != event.get("entity") or number(event, "valid") == 0:
                continue
            lost_health = number(event, "hp_after") < number(event, "hp_before")
            accepted = number(event, "accepted") != 0 and number(event, "original") == 1
            same_player = number(event, "userid") > 0 and event.get("userid") == event.get("userid_after")
            if number(event, "kind") == 0 and before.get("attacker_class") == "monster_entity" and lost_health and number(event, "original") == 1:
                evidence["monster_attack_damage"].append(sequence)
            elif number(event, "kind") == 1 and same_player and number(event, "weapon") > 0 and accepted:
                if lost_health:
                    evidence["weapon_damage"].append(sequence)
                dead = number(event, "hp_before") > 0 and (number(event, "hp_after") <= 0 or number(event, "dead_after") > 0)
                if dead and number(event, "frags_after") > number(event, "frags_before"):
                    evidence["weapon_kill_with_frag_credit"].append(sequence)
            elif number(event, "kind") == 2 and before.get("entity_class") == "zbsbreak" and accepted:
                if lost_health:
                    evidence["wall_damage"].append(sequence)
                if number(event, "solid_before") != 0 and number(event, "solid_after") == 0:
                    evidence["wall_broken"].append(sequence)
        elif kind == "marker_overlap":
            evidence["marker_overlap"].append(sequence)
        elif kind == "clearpoint_callback":
            evidence["clearpoint_callback"].append(sequence)
        elif kind == "use_callback" and event.get("targetname", "-") != "-" and number(event, "original") == 1:
            evidence["target_use_callback"].append(sequence)
        elif kind in {"wave_message", "round_outcome_message", "kill_chain_message"}:
            evidence["wave_event_message" if kind == "wave_message" else kind].append(sequence)
    sequences = [int(event["seq"]) for event in events]
    gaps = sorted(set(range(1, max(sequences, default=0) + 1)).difference(sequences))
    errors = list(collection_errors or [])
    if gaps:
        errors.append(f"Missing {len(gaps)} diagnostic events")
    if not counts["begin"] or not counts["end"]:
        errors.append("Complete begin/end capture was not observed")
    status = lambda values: "observed" if values and not taints and not errors else (
        "inconclusive" if values else "not_observed")
    return {
        "scope": "passive actual-server observations; never a full-mode completion result",
        "event_count": len(events), "event_counts": dict(counts), "missing_sequences": gaps,
        "collection_errors": errors, "taints": taints,
        "observations": {name: {"status": status(values), "event_sequences": values} for name, values in evidence.items()},
        "scenario_trigger_progression": {"status": "unverified", "reason":
            "zbstrigger overlap is not manager progression; OnControls return requests protection, not target-chain success. Inspect real map outcome and matching target Use evidence."},
        "weapon_attribution_boundary": "Records attacker userid, inflictor class, active native weapon id, health loss/death and synchronous frag credit. It does not decode the private scenario death journal; delayed projectile identity may differ from the active weapon.",
        "respawn_boundary": "Death and subsequent real Spawn of the same userid are correlated; timing/policy correctness still requires comparison with the intended round/revival rules.",
        "intervention_boundary": "This probe sends no gameplay commands. Existing diagnostic mutations and observed cvar changes taint the capture; unrelated external tools cannot all be detected.",
        "runtime_observations_present": bool(events), "all_acceptance_complete": False,
    }


def save(path: Path, value: object) -> None:
    with path.open("x", encoding="utf-8") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def collect(args: argparse.Namespace) -> int:
    output = safe_path(ROOT / args.output, ROOT / "build")
    output.mkdir(parents=True, exist_ok=False)
    safe_path(LOG, ROOT / "sandbox")
    session = "live-" + uuid.uuid4().hex[:20]
    spec = importlib.util.spec_from_file_location("scenario_rcon", ROOT / "tools/GoldSrc-Command.py")
    rcon = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(rcon)
    pinned = [ROOT / "tools/Test-CSOLScenarioLive.py", ROOT / "amxx/tests/goldcraft_scenario_ready_test.sma",
              SERVER / "hlds.exe", SERVER / "swds.dll", SERVER / "cstrike/dlls/mp.dll",
              SERVER / "cstrike/addons/amxmodx/plugins/goldcraft_scenario_ready_test.amxx",
              SERVER / "cstrike/addons/amxmodx/modules/reapi_amxx.dll",
              SERVER / "cstrike/addons/amxmodx/modules/hamsandwich_amxx.dll"]
    save(output / "inputs.json", {"session": session, "at": datetime.now(timezone.utc).isoformat(),
        "duration_seconds": args.duration, "interval_seconds": args.interval,
        "artifacts": [identity(path) for path in pinned], "processes_started": 0,
        "gameplay_commands": [], "required_map": "zs_deadend", "required_mode": 2})
    events: dict[int, dict] = {}
    errors: list[str] = []
    offset = LOG.stat().st_size if LOG.exists() else 0
    initial_offset = offset
    pending = ""
    began = False
    stopped = False
    with (output / "commands.jsonl").open("x", encoding="utf-8") as commands, (output / "events.jsonl").open("x", encoding="utf-8") as stream:
        def ingest(text: str) -> None:
            for line in text.splitlines():
                event = parse_line(line, session)
                if event is not None and int(event["seq"]) not in events:
                    events[int(event["seq"])] = event
                    stream.write(json.dumps(event, ensure_ascii=False) + "\n")
                    stream.flush()

        def read_log() -> None:
            nonlocal offset, pending
            if not LOG.exists():
                return
            if LOG.stat().st_size < offset:
                raise RuntimeError("Diagnostic log was truncated during capture")
            if LOG.stat().st_size - initial_offset > MAX_LOG_BYTES:
                raise RuntimeError("Diagnostic log exceeded the 32 MiB capture budget")
            with LOG.open("rb") as logfile:
                logfile.seek(offset)
                data = logfile.read(MAX_LOG_BYTES)
                offset += len(data)
            pending += data.decode("utf-8", errors="replace")
            complete, separator, pending = pending.rpartition("\n")
            if separator:
                ingest(complete)
            else:
                pending = complete + pending

        def command(text: str) -> str:
            allowed = text in {"version", "meta list", "amxx version", "amxx modules", "csol_live_status"} or text in {
                f"csol_live_begin {session}", f"csol_live_end {session}"}
            if not allowed:
                raise ValueError("Probe does not permit gameplay commands")
            response = rcon.command(text)
            commands.write(json.dumps({"at": datetime.now(timezone.utc).isoformat(), "command": text, "response": response}, ensure_ascii=False) + "\n")
            commands.flush()
            ingest(response)
            read_log()
            return response

        try:
            for text in ("version", "meta list", "amxx version", "amxx modules"):
                command(text)
            began = True
            command(f"csol_live_begin {session}")
            if not any(event["event"] == "begin" for event in events.values()):
                raise RuntimeError("Observer did not start: load the test plugin, set csol_ready_test 1, and use scenario mode 2 on zs_deadend")
            deadline = time.monotonic() + args.duration
            while time.monotonic() < deadline:
                command("csol_live_status")
                if any(event["event"] == "event_limit" for event in events.values()):
                    raise RuntimeError("Plugin event budget reached")
                time.sleep(min(args.interval, max(0, deadline - time.monotonic())))
        except KeyboardInterrupt:
            errors.append("Capture interrupted by operator")
        except Exception as error:
            errors.append(str(error))
        finally:
            if began:
                try:
                    command(f"csol_live_end {session}")
                    stopped = True
                except Exception as error:
                    errors.append(f"Observer stop failed: {error}")
            try:
                read_log()
            except Exception as error:
                errors.append(str(error))
    result = analyze(list(events.values()), errors)
    result.update(session=session, observer_stop_command_acknowledged=stopped)
    save(output / "summary.json", result)
    print(json.dumps({"output": str(output), "events": len(events), "errors": errors,
                      "scenario_progression": "unverified", "full_acceptance": False}, ensure_ascii=False))
    return 1 if errors else 0


def self_test() -> int:
    lines = [
        "event=begin", "event=round started=0 freeze=0 start=0 difficulty=1 freezetime=6 cheats=0",
        "event=round started=1 freeze=1 start=10 difficulty=1 freezetime=6 cheats=0",
        "event=player_spawn userid=7 team=2 alive=1 original=1", "event=round started=1 freeze=0 start=10 difficulty=1 freezetime=6 cheats=0",
        "event=player_death userid=7 alive=0 original=1", "event=player_spawn userid=7 team=2 alive=1 original=1",
        "event=damage_pre call=8 kind=1 entity=70 attacker_class=player userid=7 weapon=144",
        "event=damage_post call=8 kind=1 entity=70 valid=2 userid=7 userid_after=7 weapon=144 hp_before=20 hp_after=0 frags_before=1 frags_after=2 dead_after=2 accepted=1 original=1",
        "event=marker_overlap progression=unverified", "event=clearpoint_callback protection_request=1 progression=unverified", "event=end",
    ]
    events = [parse_line(f"L 00:00: {MARKER}session=selftest seq={i} time={i}.0 {line}") for i, line in enumerate(lines, 1)]
    result = analyze(events)
    assert result["observations"]["round_start"]["status"] == "observed"
    assert result["observations"]["death_then_spawn"]["status"] == "observed"
    assert result["observations"]["weapon_kill_with_frag_credit"]["status"] == "observed"
    assert result["scenario_trigger_progression"]["status"] == "unverified"
    assert not result["all_acceptance_complete"]
    forced = [*events, {"session": "selftest", "seq": "13", "time": "13", "event": "mutation", "operation": "forced_respawn"}]
    assert analyze(forced)["observations"]["death_then_spawn"]["status"] == "inconclusive"
    reused = [dict(event) for event in events]
    reused[8]["userid_after"] = "8"
    assert analyze(reused)["observations"]["weapon_kill_with_frag_credit"]["status"] == "not_observed"
    assert analyze(events[1:])["collection_errors"]
    assert parse_line(f"{MARKER}session=wrong seq=1 time=0 event=begin", "expected") is None
    print("scenario live parser: 9 checks passed; synthetic parser inputs only, no RCON or gameplay")
    return 0


def round_clock_test() -> int:
    lines = ["event=begin",
        "event=round started=0 freeze=1 start=0 round=0 restarts=0",
        "event=round started=1 freeze=1 start=10 round=0 restarts=1",
        "event=round started=1 freeze=0 start=16 round=0 restarts=1",
        "event=round started=1 freeze=1 start=30 round=0 restarts=2", "event=end"]
    events = [parse_line(f"{MARKER}session=clock seq={i} time={i}.0 {line}") for i, line in enumerate(lines, 1)]
    result = analyze(events)["observations"]
    assert result["round_start"]["event_sequences"] == [3, 5]
    assert result["freeze_finished"]["event_sequences"] == [4]
    assert result["round_start"]["status"] == "observed"
    print("scenario round-clock regression: 3 checks passed; synthetic parser inputs, no RCON or gameplay")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="action", required=True)
    run = sub.add_parser("run", help="Record an already running server; requires enabled observer plugin")
    run.add_argument("--duration", type=float, default=120)
    run.add_argument("--interval", type=float, default=1)
    run.add_argument("--output", default="build/csol-scenario-live-" + datetime.now().strftime("%Y%m%d-%H%M%S"))
    offline = sub.add_parser("analyze", help="Analyze captured events without contacting the server")
    offline.add_argument("events", type=Path)
    offline.add_argument("--output", type=Path, required=True)
    test = sub.add_parser("self-test", help="Check only the evidence classifier; does not validate gameplay")
    test.add_argument("--round-clock-only", action="store_true", help="Run only the freeze-clock regression")
    args = parser.parse_args()
    if args.action == "self-test":
        return round_clock_test() if args.round_clock_only else self_test()
    if args.action == "analyze":
        events = [json.loads(line) for line in args.events.read_text("utf-8").splitlines() if line.strip()]
        output = safe_path(args.output, ROOT / "build")
        save(output, analyze(events))
        return 0
    if not 1 <= args.duration <= 900 or not 0.5 <= args.interval <= 10:
        parser.error("duration must be 1..900 seconds and interval 0.5..10 seconds")
    return collect(args)


if __name__ == "__main__":
    raise SystemExit(main())
