#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build actual CharacterSkill methods with controlled registry/engine edges."""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def identity(path):
    return dict(path=path.relative_to(ROOT).as_posix(), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.absolute()
    if not output.resolve().is_relative_to(ROOT / 'build'):
        parser.error('Output must remain under workspace build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output refused')
    output.mkdir(parents=True, exist_ok=False)
    production = ROOT / 'native/csol/server/scenario_active_skill.cpp'
    header = ROOT / 'native/csol/server/scenario_player_runtime.h'
    fixture = ROOT / 'tests/csol-scenario-active-skill/main.cpp'
    runtime = ROOT / 'native/csol/server/scenario_player_runtime.cpp'
    sender = ROOT / 'native/csol/server/scenario_messages.cpp'
    sender_header = ROOT / 'native/csol/server/scenario_messages.h'
    sender_fixture = ROOT / 'tests/csol-scenario-active-skill/sender.cpp'
    session = ROOT / 'native/csol/server/scenario_session.cpp'
    resource_fixture = ROOT / 'tests/csol-scenario-active-skill/resources.cpp'
    hud_fixture = ROOT / 'tests/csol-scenario-active-skill/hud_runtime.cpp'
    inputs = [production, header, fixture, runtime, Path(__file__).resolve(),
              ROOT / 'native/csol/server/scenario_active_skill.h',
              ROOT / 'native/csol/scenario/equipment.hpp', ROOT / 'native/csol/scenario/config.hpp',
              sender, sender_header, sender_fixture, session, resource_fixture,
              ROOT / 'native/csol/scenario_upgrades.hpp', ROOT / 'native/csol/scenario_abilities.hpp', hud_fixture]
    before = [identity(path) for path in inputs]
    source = production.read_text('utf-8')
    marker = 'namespace\n{\nclass CharacterSkill final'
    if source.count(marker) != 1:
        raise RuntimeError('Production CharacterSkill boundary changed; review extraction')
    actual = source[source.index(marker):]
    (output / 'actual_character_skill.inc').write_text(actual, encoding='utf-8')
    declarations = header.read_text('utf-8')
    start = declarations.index('class CSOLScenarioActiveSkill\n{')
    end = declarations.index('enum class CSOLScenarioKillSourcePhase', start)
    actual_interfaces = declarations[start:end]
    (output / 'actual_skill_interfaces.inc').write_text(actual_interfaces, encoding='utf-8')
    runtime_text = runtime.read_text('utf-8')
    start = runtime_text.index('bool CCSOLScenarioPlayerRuntime::RefreshActiveSkill(')
    end = runtime_text.index('bool CCSOLScenarioPlayerRuntime::RefreshClassMods(', start)
    actual_refresh = runtime_text[start:end]
    (output / 'actual_refresh_skill.inc').write_text(actual_refresh, encoding='utf-8')
    hud_start = runtime_text.index('bool CCSOLScenarioPlayerRuntime::UpdateHud(')
    hud_end = runtime_text.index('bool CCSOLScenarioPlayerRuntime::SetClassModRecords(', hud_start)
    actual_update_hud = runtime_text[hud_start:hud_end]
    (output / 'actual_update_hud.inc').write_text(actual_update_hud, encoding='utf-8')
    sender_text = sender.read_text('utf-8')
    start = sender_text.index('namespace\n{')
    end = sender_text.index('bool CCSOLScenarioMessages::Register()', start)
    actual_guard = sender_text[start:end]
    (output / 'actual_message_guard.inc').write_text(actual_guard, encoding='utf-8')
    start = sender_text.index('bool CCSOLScenarioMessages::SendActiveSkillCooldown(')
    end = sender_text.index('bool CCSOLScenarioMessages::SendMonsterKillChain(', start)
    actual_sender = sender_text[start:end]
    (output / 'actual_skill_sender.inc').write_text(actual_sender, encoding='utf-8')
    register_start = sender_text.index('bool CCSOLScenarioMessages::Register()')
    register_end = sender_text.index('bool CCSOLScenarioMessages::SendStatusLabel(', register_start)
    hud_start = sender_text.index('bool CCSOLScenarioMessages::SendUpgradeExperience(')
    hud_end = sender_text.index('bool CCSOLScenarioMessages::SendActiveSkillCooldown(', hud_start)
    actual_hud_senders = sender_text[register_start:register_end] + sender_text[hud_start:hud_end]
    (output / 'actual_hud_senders.inc').write_text(actual_hud_senders, encoding='utf-8')
    sender_declaration = next(line for line in sender_header.read_text('utf-8').splitlines()
                              if 'bool SendActiveSkillCooldown(' in line)
    (output / 'actual_sender_declaration.inc').write_text(sender_declaration, encoding='utf-8')
    session_text = session.read_text('utf-8')
    start = session_text.index('    std::set<std::string> hudPaths{')
    end = session_text.index('    // PRECACHE_MODEL terminates', start)
    actual_hud_resources = session_text[start:end]
    (output / 'actual_hud_resources.inc').write_text(actual_hud_resources, encoding='utf-8')
    flags = ['--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT',
             '/W4', '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
             '/DNDEBUG', '/DNOMINMAX', '/I' + str(output)]
    executable = output / 'active-skill.exe'
    command = [str(args.clang), *flags, str(fixture), str(sender_fixture), str(resource_fixture), str(hud_fixture), '/Fe' + str(executable),
               '/Fo' + str(output) + '/', '/link', '/MACHINE:X86']
    (output / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    compiled = subprocess.run(command, cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    if compiled.returncode:
        raise RuntimeError('Compile failed; see ' + str(output / 'compile.log'))
    result = subprocess.run([str(executable)], cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (output / 'test.log').write_text(result.stdout + result.stderr, encoding='utf-8')
    after = [identity(path) for path in inputs]
    receipt = dict(at=datetime.datetime.now(datetime.timezone.utc).isoformat(), inputsBefore=before, inputsAfter=after,
                   inputsUnchanged=before == after, compileExit=compiled.returncode, testExit=result.returncode,
                   productionBodySha256=hashlib.sha256(actual.encode()).hexdigest(),
                   productionInterfacesSha256=hashlib.sha256(actual_interfaces.encode()).hexdigest(),
                   productionRefreshSha256=hashlib.sha256(actual_refresh.encode()).hexdigest(),
                   productionSenderSha256=hashlib.sha256(actual_sender.encode()).hexdigest(),
                   productionHudSendersSha256=hashlib.sha256(actual_hud_senders.encode()).hexdigest(),
                   productionUpdateHudSha256=hashlib.sha256(actual_update_hud.encode()).hexdigest(),
                   productionGuardSha256=hashlib.sha256(actual_guard.encode()).hexdigest(),
                   productionHudResourcesSha256=hashlib.sha256(actual_hud_resources.encode()).hexdigest(),
                   output=result.stdout + result.stderr,
                   boundary='Actual production factory/callback/RefreshActiveSkill/sender/guard and HUD precache block, exact public virtual and sender declarations, controlled registry/map/engine edges. Does not test full SDK Prepare or complete Session, actual cloak registry implementation, active effects, or gameplay.')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(result.stdout, end='')
    if result.returncode or before != after:
        raise RuntimeError('Test failed or inputs changed; see receipt')


if __name__ == '__main__':
    main()
