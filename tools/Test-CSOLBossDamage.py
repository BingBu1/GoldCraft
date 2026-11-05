#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Execute production Oberon damage modifier and Boss HUD owners at SDK boundaries."""
import argparse
import hashlib
import json
import pathlib
import subprocess

root = pathlib.Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--clang", type=pathlib.Path, required=True)
parser.add_argument("--output", type=pathlib.Path, required=True)
parser.add_argument("--damage-source", type=pathlib.Path,
                    help="Use a preserved production TU for a same-fixture negative control")
args = parser.parse_args()
out = args.output.resolve()
out.relative_to(root / "build")
out.mkdir(parents=True, exist_ok=False)
damage = args.damage_source.resolve() if args.damage_source else root / "native/csol/server/scenario_monster_damage.cpp"
damage.relative_to(root)
messages = root / "native/csol/server/scenario_messages.cpp"
fixture = root / "tests/csol-scenario-boss-damage/main.cpp"
math = root / "native/csol/scenario/monster_damage.cpp"
inputs = [damage, messages, fixture, math, pathlib.Path(__file__),
          root / "native/csol/server/scenario_monster_damage_behavior.h",
          root / "native/csol/server/scenario_monster_damage.h",
          root / "native/csol/scenario/map_support.hpp",
          root / "native/csol/server/scenario_entity_access.h",
          root / "native/csol/server/scenario_messages.h"]
def identity():
    return [{"path": str(p.relative_to(root)), "sha256": hashlib.sha256(p.read_bytes()).hexdigest()} for p in inputs]
before = identity()
text = damage.read_text(encoding="utf-8-sig")
wire = messages.read_text(encoding="utf-8-sig")
parts = {
    "actual_boss_methods.inc": text[text.index("struct CCSOLScenarioDamageServices::BossState"):text.index("const bool& CCSOLScenarioDamageServices::DamageSuppressed")],
    "actual_oberon_hit.inc": text[text.index("bool CSOL_ResolveOberonHitDamage("):text.index("BOOL CCSOLOrdinaryMonsterDamage::Apply(")],
    "actual_prearmor.inc": text[text.index("bool CCSOLMonsterDamageBehavior::ResolvePreArmorDamage("):text.index("bool CCSOLOrdinaryMonsterBehavior::ResolveHitDamage(")],
    "actual_apply.inc": text[text.index("BOOL CCSOLOrdinaryMonsterDamage::Apply("):text.index("bool CSOL_ApplyMonsterMeleeImpulse(")],
    "actual_message_scope.inc": wire[wire.index("namespace\n{"):wire.index("bool CCSOLScenarioMessages::Register()")],
    "actual_messages.inc": wire[wire.index("bool CCSOLScenarioMessages::SendBossParticipation("):wire.index("bool CCSOLScenarioMessages::RemoveStatusLabel(")],
}
for name, value in parts.items():
    (out / name).write_text(value, encoding="utf-8")
command = [str(args.clang), "--target=i686-pc-windows-msvc", "/nologo", "/std:c++20", "/EHsc", "/MT", "/utf-8",
           "/clang:-O3", "/clang:-flto=thin", "/clang:-fuse-ld=lld", "/arch:AVX2", "/fp:precise", "/DNDEBUG", "/DNOMINMAX",
           "/showIncludes",
           "/I" + str(out), str(fixture), str(math), "/Fe" + str(out / "boss-damage.exe"), "/Fo" + str(out) + "/", "/link", "/MACHINE:X86"]
(out / "command.json").write_text(json.dumps(command, indent=2), encoding="utf-8")
compiled = subprocess.run(command, cwd=out, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=180)
(out / "compile.log").write_text(compiled.stdout + compiled.stderr, encoding="utf-8")
if compiled.returncode:
    raise RuntimeError("Compilation failed: " + str(out / "compile.log"))
run = subprocess.run([str(out / "boss-damage.exe")], cwd=out, capture_output=True, text=True, timeout=30)
(out / "test.log").write_text(run.stdout + run.stderr, encoding="utf-8")
after = identity()
receipt = {"compile": compiled.returncode, "test": run.returncode, "output": run.stdout + run.stderr,
           "inputsBefore": before, "inputsAfter": after, "inputsUnchanged": before == after,
           "extractedSha256": {k: hashlib.sha256(v.encode()).hexdigest() for k, v in parts.items()},
           "boundary": "Actual production Oberon hit modifier, contribution/health methods, all three packet writers and common damage math. Controlled entity/lifecycle/network/appearance SDK boundaries. No live game or original instructions executed."}
(out / "receipt.json").write_text(json.dumps(receipt, indent=2), encoding="utf-8")
print(run.stdout + run.stderr, end="")
if run.returncode or before != after:
    raise RuntimeError("Checks failed or source changed")
