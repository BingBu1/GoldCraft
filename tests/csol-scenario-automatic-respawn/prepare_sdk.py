"""Freeze unchanged production rules and explicit SDK-boundary regression inputs."""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import shutil

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
if output.exists() or not output.is_relative_to(root / 'build'):
    raise ValueError('Use a fresh workspace build directory')
for parent in (output, *output.parents):
    if parent.is_symlink() or parent.is_junction():
        raise ValueError('Do not follow reparse paths')
inputs = ['native/csol/scenario/automatic_respawn.hpp',
          'native/csol/server/game_rules_gate.cpp',
          'external/ReGameDLL_CS/regamedll/dlls/player.cpp',
          'external/ReGameDLL_CS/regamedll/dlls/client.cpp',
          'external/ReGameDLL_CS/regamedll/dlls/multiplay_gamerules.cpp',
          'native/csol/server/characters.cpp',
          'tests/csol-scenario-automatic-respawn/sdk.cpp',
          'tests/csol-scenario-automatic-respawn/CMakeLists.txt',
          'tests/csol-scenario-automatic-respawn/prepare_sdk.py',
          'tests/csol-scenario-respawn-integration/prepare.py',
          'cmake/clang-x86.cmake', 'cmake/cxx20.cmake']
records = []
for relative in inputs:
    source = root / relative
    target = output / 'source' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    sha = hashlib.sha256(source.read_bytes()).hexdigest()
    if hashlib.sha256(target.read_bytes()).hexdigest() != sha:
        raise RuntimeError('Input changed during freeze: ' + relative)
    records.append({'source': str(source), 'frozen': str(target), 'sha256': sha})
spec = importlib.util.spec_from_file_location('existing_extract', output / 'source/tests/csol-scenario-respawn-integration/prepare.py')
helper = importlib.util.module_from_spec(spec)
spec.loader.exec_module(helper)
producer = output / 'source/native/csol/server/game_rules_gate.cpp'
text = producer.read_text('utf-8-sig')
methods = ['struct RespawnSlot', 'bool CanRespawn(CBasePlayer& player) const',
           'bool DeathClock(CBasePlayer& player)', 'void SpawnStarted(CBasePlayer& player)',
           'bool RespawnThink(CBasePlayer& player)', 'void CharacterSelected(CBasePlayer& player)',
           'bool Spawnable(CBasePlayer& player) const', 'bool RefreshRevivalTeam()',
           'RespawnSlot* RespawnState(CBasePlayer& player, bool create)', 'bool PreparePlayer(CBasePlayer& player)']
blocks = [helper.extract(text, token) + (';' if token.startswith('struct ') else '') for token in methods]
target = output / 'source/tests/csol-scenario-automatic-respawn/actual_automatic_rules.inc'
target.write_text('\n\n'.join(blocks) + '\n', encoding='utf-8')
methods_record = [{'token': token, 'sha256': hashlib.sha256(block.encode()).hexdigest()} for token, block in zip(methods, blocks)]
(output / 'inputs.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
(output / 'methods.json').write_text(json.dumps(methods_record, indent=2), encoding='utf-8')
print(json.dumps({'output': str(output), 'frozenInputs': len(records), 'actualMethods': len(methods)}))
