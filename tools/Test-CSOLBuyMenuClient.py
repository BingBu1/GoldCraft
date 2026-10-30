"""Exercise production purchase-menu data/controller with controlled engine/VGUI edges."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parent.parent


def identity(path):
    return dict(path=str(path.relative_to(ROOT)), sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def extract(source, signature):
    start = source.index(signature)
    opening = source.index('{', start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    p.add_argument('--menu', type=Path, required=True)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--environment', type=Path, required=True)
    p.add_argument('--server-teams', action='store_true')
    args = p.parse_args()
    output = args.output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh build output')
    output.mkdir(parents=True)
    client = ROOT / 'native/csol/client/buy_menu.cpp'
    inputs = [client, ROOT / 'native/csol/buy_menu.cpp', ROOT / 'native/csol/buy_menu.hpp',
              ROOT / 'native/csol/character_menu.hpp', ROOT / 'tests/csol-buy-menu/main.cpp']
    before = [identity(path) for path in inputs]
    source = client.read_text(encoding='utf8')
    signatures = ['void LoadFavorites()', 'void SaveFavorites()', 'std::wstring Wide(',
                  'void RestoreMouse()', 'void Close(', 'void Refresh()', 'void OpenMenu(',
                  'const std::vector<const Item*>& Rows()', 'void Purchase(', 'void Select(',
                  'void RememberPurchase(', 'void ContinueRebuy()', 'int Receive(', 'int Hit(',
                  'void Click(', 'void EditFavorite(', 'void ResetBuyMenu()',
                  'void BuyMenuFrame()', 'void BuyMenuCreateMove(']
    chunks = [extract(source, signature) for signature in signatures]
    (output / 'actual_controller.inc').write_text('\n\n'.join(chunks) + '\n', encoding='utf8')
    (output / 'actual_key.inc').write_text(extract(source, 'void Key_Event(') + '\n', encoding='utf8')
    rapidjson = ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include'
    executable = output / 'buy-menu-tests.exe'
    command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc',
               '/MT', '/utf-8', '/O2', '/DNOMINMAX', '/I' + str(output), '/I' + str(rapidjson),
               str(ROOT / 'tests/csol-buy-menu/main.cpp'), str(ROOT / 'native/csol/buy_menu.cpp'),
               '/Fe:' + str(executable), '/link', '/machine:x86']
    saved = json.loads(args.environment.read_text(encoding='utf8'))
    env = os.environ.copy()
    env.update({key: saved[key] for key in ('INCLUDE', 'LIB', 'LIBPATH')})
    result = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, encoding='utf8', errors='replace')
    (output / 'compile.log').write_text(result.stdout + result.stderr, encoding='utf8')
    run = None
    if result.returncode == 0:
        run = subprocess.run([str(executable), str(args.menu.resolve())], cwd=output, capture_output=True,
                             text=True, encoding='utf8', errors='replace')
        (output / 'test.log').write_text(run.stdout + run.stderr, encoding='utf8')
    teams = None
    if args.server_teams and result.returncode == 0 and run.returncode == 0:
        table_path = ROOT / 'external/ReGameDLL_CS/regamedll/dlls/weapontype.cpp'
        helper_path = ROOT / 'native/csol/server/buy_menu.cpp'
        ids_path = ROOT / 'external/ReGameDLL_CS/regamedll/dlls/weapontype.h'
        team_sources = [identity(path) for path in (table_path, helper_path, ids_path)]
        (output / 'weapon_ids.inc').write_text(extract(ids_path.read_text(encoding='utf8'), 'enum WeaponIdType') + ';\n', encoding='utf8')
        (output / 'actual_team_helper.inc').write_text(extract(helper_path.read_text(encoding='utf8'), 'bool CSOL_CanBuyScenarioNativeWeapon('), encoding='utf8')
        (output / 'actual_team_table.inc').write_text(extract(table_path.read_text(encoding='utf8'), 'bool CanBuyWeaponByMaptype('), encoding='utf8')
        old = subprocess.run(['git', '-C', str(ROOT / 'external/ReGameDLL_CS'), 'show', 'HEAD:regamedll/dlls/weapontype.cpp'], check=True,
                             capture_output=True, text=True, encoding='utf8').stdout
        upstream_revision = subprocess.run(['git', '-C', str(ROOT / 'external/ReGameDLL_CS'), 'rev-parse', 'HEAD'], check=True,
                                           capture_output=True, text=True, encoding='utf8').stdout.strip()
        old_function = extract(old, 'bool CanBuyWeaponByMaptype(')
        if 'CSOL_CanBuyScenarioNativeWeapon' in old_function:
            raise ValueError('Expected a pinned upstream table, not our adaptation')
        (output / 'upstream_team_table.inc').write_text(old_function.replace('CanBuyWeaponByMaptype(', 'UpstreamCanBuyWeaponByMaptype(', 1), encoding='utf8')
        team_exe = output / 'buy-team-tests.exe'
        team_command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/O2',
                        '/I' + str(output), '/I' + str(rapidjson), str(ROOT / 'tests/csol-buy-menu/server_team.cpp'),
                        str(ROOT / 'native/csol/buy_menu.cpp'), '/Fe:' + str(team_exe), '/link', '/machine:x86']
        compiled = subprocess.run(team_command, cwd=output, env=env, capture_output=True, text=True, encoding='utf8', errors='replace')
        (output / 'team-compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf8')
        tested = subprocess.run([str(team_exe)], cwd=output, capture_output=True, text=True, encoding='utf8') if compiled.returncode == 0 else None
        if tested:
            (output / 'team-test.log').write_text(tested.stdout + tested.stderr, encoding='utf8')
        teams = dict(inputs=team_sources, inputsUnchanged=team_sources == [identity(path) for path in (table_path,helper_path,ids_path)],
                     upstreamRevision=upstream_revision,
                     upstreamFunctionSha256=hashlib.sha256(old_function.encode()).hexdigest(), command=team_command,
                     compileExit=compiled.returncode, testExit=None if tested is None else tested.returncode)
    after = [identity(path) for path in inputs]
    receipt = dict(inputsBefore=before, inputsAfter=after, unchanged=before == after, command=command,
                   compileExit=result.returncode, testExit=None if run is None else run.returncode, teams=teams,
                   menu=dict(path=str(args.menu.resolve()), sha256=hashlib.sha256(args.menu.read_bytes()).hexdigest()),
                   boundary='Complete production buy data TU and unchanged controller bodies; engine and VGUI are controlled boundaries. Not a game or GL/IME test.')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf8')
    print(result.stdout + result.stderr if result.returncode else (run.stdout + run.stderr), end='')
    if teams:
        print((output / ('team-test.log' if teams['compileExit'] == 0 else 'team-compile.log')).read_text(encoding='utf8'))
    if result.returncode or run is None or run.returncode or before != after or (teams and (teams['compileExit'] or teams['testExit'] or not teams['inputsUnchanged'])):
        raise SystemExit(1)


if __name__ == '__main__':
    main()
