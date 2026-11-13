# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the first-pistol candidate packs with immutable inputs and CLI refusals."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
from types import SimpleNamespace

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
TOOL = ROOT/'tools/Prepare-CryOfFearResourcePack.py'


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('original-game-root', 'inherited-game-root', 'output'):
        parser.add_argument('--'+name, type=Path, required=True)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('cof_pack', TOOL)
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    output = tool.workspace_target(args.output)
    if output.exists():
        raise ValueError('Use a fresh test output directory')
    output.mkdir(parents=True)
    checks, commands = [], []

    def check(value, name):
        checks.append(dict(name=name, passed=bool(value)))

    def invoke(name, extra=(), inherit=True, expected=2, candidate=None, plan_path=None):
        command = [sys.executable, '-X', 'utf8', str(TOOL), '--original-game-root', str(args.original_game_root.resolve()),
            '--output', str(candidate or output/name/'candidate'), '--plan', str(plan_path or output/name/'plan.json'),
            '--server-model-sounds', 'disabled']
        if inherit:
            command += ['--inherited-game-root', str(args.inherited_game_root.resolve())]
        command += list(map(str, extra))
        run = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, encoding='utf-8', timeout=30)
        (output/(name+'.log')).write_text(run.stdout+run.stderr, 'utf-8')
        commands.append(dict(name=name, command=command, returncode=run.returncode, expected=expected))
        check(run.returncode == expected, name+' CLI return')
        return run

    # Capture every source, including the real inherited texture companion.
    baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root, output=output/'accepted'/'candidate',
        plan=output/'accepted'/'plan.json', server_model_sounds='disabled', component='p345-and-shot-decals'))
    glock_baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root, output=output/'glock'/'candidate',
        plan=output/'glock'/'plan.json', server_model_sounds='disabled', component='glock-and-shot-decals'))
    revolver_baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root, output=output/'revolver'/'candidate',
        plan=output/'revolver'/'plan.json', server_model_sounds='disabled', component='revolver-and-shot-decals'))
    inputs = {Path(__file__).resolve(), Path(__file__).with_name('README.md'), TOOL, tool.HEADER,
              *(ROOT/p for p in tool.CONSUMERS), *(Path(p['source']) for p in baseline['assets']),
              *(Path(p['source']) for p in glock_baseline['assets']),
              *(Path(p['source']) for p in revolver_baseline['assets'])}
    before = {str(p): sha(p) for p in sorted(inputs)}
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()

    dry = invoke('accepted', expected=0)
    dry_result = json.loads(dry.stdout)
    plan_path = output/'accepted'/'plan.json'
    plan = json.loads(plan_path.read_bytes())
    candidate = output/'accepted'/'candidate'
    check(not candidate.exists(), 'dry-run writes no candidate')
    check(len(plan['assets']) == 49 and plan['primaryAssets'] == 48 and plan['dependencyAssets'] == 1,
          '43 P345 resources, five decals and actual clip texture dependency')
    check(plan['policy']['serverAutoModelSoundsRequired'] == 0 and not plan['policy']['liveServerConfigurationVerified'] and
          not plan['policy']['publicSoundAliases'] and not plan['policy']['weaponBackendReady'], 'policy does not claim live/backend readiness')
    check(len(plan['studio5004']) == 7 and all(e['localLogicalKey'].startswith('weapons_new/p345/') and
        e['localAsset'].startswith('sound/guest/cof/') and e['remoteAsset'].startswith('guest/cof/') and
        e['forbiddenAutomaticScanPath'].startswith('sound/weapons/p345/') for e in plan['studio5004']),
        'all original Studio sound events mapped without mutation')
    inherited_rows = [p for p in plan['assets'] if p['sourceKind'] == 'explicit-inherited-game']
    check({p['original'] for p in inherited_rows} == {'models/w_9mmclip.mdl', 'models/w_9mmclipT.mdl'},
          'explicit inherited provenance of both clip files')

    invoke('missing-inherited', inherit=False)
    invoke('enabled-scan', ['--server-model-sounds', 'enabled'])
    invoke('outside-build', candidate=ROOT/'not-a-pack')
    invoke('plan-inside-candidate', candidate=output/'inside'/'candidate', plan_path=output/'inside'/'candidate'/'plan.json')
    existing = output/'already'/'candidate'
    existing.mkdir(parents=True)
    (existing/'keep.txt').write_text('keep')
    invoke('existing-output', candidate=existing)
    check((existing/'keep.txt').read_text() == 'keep', 'existing target preserved')
    invoke('absent-root', ['--original-game-root', output/'absent'])
    invoke('missing-plan-hash', ['--write'], candidate=candidate, plan_path=plan_path)
    invoke('wrong-plan-hash', ['--write', '--expected-plan-sha256', '0'*64], candidate=candidate, plan_path=plan_path)
    invoke('changed-output', ['--write', '--expected-plan-sha256', dry_result['planSha256']], plan_path=plan_path)
    original_plan_bytes = plan_path.read_bytes()
    invoke('existing-plan', expected=2, candidate=candidate, plan_path=plan_path)
    check(plan_path.read_bytes() == original_plan_bytes, 'existing plan never overwritten')
    tampered = output/'tampered-plan.json'
    changed = dict(plan, scope='unreviewed25-weapons')
    tampered.write_bytes(tool.json_bytes(changed))
    invoke('tampered-plan', ['--write', '--expected-plan-sha256', sha(tampered)], candidate=candidate, plan_path=tampered)
    check(not candidate.exists(), 'all refused plans left accepted candidate absent')

    fixtures = output/'fixtures'
    corrupt_root = fixtures/'corrupt-original'
    bad_view = corrupt_root/'models/weapons/p345/v_p345.mdl'
    bad_view.parent.mkdir(parents=True)
    original_view = Path(next(p['source'] for p in plan['assets'] if p['original'] == 'models/weapons/p345/v_p345.mdl'))
    corrupted = bytearray(original_view.read_bytes()); corrupted[-1] ^= 1
    bad_view.write_bytes(corrupted)
    invoke('corrupt-original', ['--original-game-root', corrupt_root])
    missing_texture = fixtures/'missing-texture'
    (missing_texture/'models').mkdir(parents=True)
    original_clip = Path(next(p['source'] for p in inherited_rows if p['original'] == 'models/w_9mmclip.mdl'))
    shutil.copyfile(original_clip, missing_texture/'models/w_9mmclip.mdl')
    invoke('missing-texture', ['--inherited-game-root', missing_texture])
    original_texture = Path(next(p['source'] for p in inherited_rows if p['original'] == 'models/w_9mmclipT.mdl'))
    texture_bytes = bytearray(original_texture.read_bytes()); texture_bytes[-1] ^= 1
    (missing_texture/'models/w_9mmclipT.mdl').write_bytes(texture_bytes)
    invoke('corrupt-texture', ['--inherited-game-root', missing_texture])

    # A real Windows junction, removed with rmdir on the link itself only.
    target = output/'junction-target'; target.mkdir()
    link = output/'junction-link'
    linked = subprocess.run([str(Path(os.environ['SystemRoot'])/'System32/cmd.exe'), '/d', '/c',
        'mklink', '/J', str(link), str(target)], capture_output=True, text=True,
        encoding='oem', errors='replace', timeout=10)
    (output/'junction-create.log').write_text(linked.stdout+linked.stderr, 'utf-8')
    check(linked.returncode == 0 and os.path.isjunction(link), 'actual junction created')
    if linked.returncode == 0:
        try:
            invoke('junction-output', candidate=link/'candidate')
            invoke('junction-plan', plan_path=link/'plan.json')
            check(not list(target.iterdir()), 'junction target untouched')
        finally:
            os.rmdir(link)

    # Fail after a real first atomic copy and verify no partial candidate leaks.
    fault_plan = dict(plan, output=str(output/'interrupted'/'candidate'))
    saved_atomic = tool.atomic_file
    writes = 0

    def interrupted(path, data):
        nonlocal writes
        writes += 1
        if writes == 2:
            raise OSError('controlled second-file interruption')
        saved_atomic(path, data)

    tool.atomic_file = interrupted
    try:
        try:
            tool.write_candidate(fault_plan)
            check(False, 'interruption rejected')
        except OSError as error:
            check(str(error) == 'controlled second-file interruption', 'interruption rejected')
        check(not Path(fault_plan['output']).exists() and not list((output/'interrupted').iterdir()),
              'interrupted candidate temporary tree fully removed')
    finally:
        tool.atomic_file = saved_atomic

    written = invoke('write', ['--write', '--expected-plan-sha256', dry_result['planSha256']],
        expected=0, candidate=candidate, plan_path=plan_path)
    check(json.loads(written.stdout)['files'] == 49, 'write CLI reports actual49 assets')
    manifest = json.loads((candidate/'manifest.json').read_bytes())
    check(manifest['status'] == 'candidate-only' and manifest['validation']['independentCopies'], 'candidate manifest status')
    expected_paths = {p['destination'] for p in manifest['assets']} | {'manifest.json'}
    actual_paths = {p.relative_to(candidate).as_posix() for p in candidate.rglob('*') if p.is_file()}
    check(actual_paths == expected_paths, 'no hidden aliases or extra resources')
    for row in manifest['assets']:
        path, source = candidate/row['destination'], Path(row['source'])
        check(sha(path) == row['sha256'] and path.stat().st_size == row['bytes'], 'actual candidate bytes '+row['destination'])
        check((path.stat().st_dev, path.stat().st_ino) != (source.stat().st_dev, source.stat().st_ino) and
            path.stat().st_nlink == 1, 'independent candidate '+row['destination'])
    copied_view = candidate/'models/guest/cof/p345/v_p345.mdl'
    check(copied_view.read_bytes() == original_view.read_bytes(), 'V model and Studio options byte preserved')
    check(not manifest['policy']['playerModelGripAccepted'] and not manifest['policy']['deployment'], 'original P is no grip/deployment claim')
    invoke('repeat-write', ['--write', '--expected-plan-sha256', dry_result['planSha256']],
        candidate=candidate, plan_path=plan_path)
    check(sha(candidate/'manifest.json') == json.loads(written.stdout)['manifestSha256'], 'repeat refusal preserves complete candidate')
    glock_dry = invoke('glock', ['--component', 'glock-and-shot-decals'], expected=0)
    glock_plan_path = output/'glock'/'plan.json'
    glock_plan = json.loads(glock_plan_path.read_bytes())
    check(glock_plan['weapon'] == 'Glock' and glock_plan['primaryAssets'] == 49 and
          glock_plan['dependencyAssets'] == 1 and len(glock_plan['assets']) == 50,
          '44 Glock resources, five decals and one texture companion')
    check(len(glock_plan['studio5004']) == 12 and all(
        e['localLogicalKey'].startswith('weapons_new/glock/') and
        e['localAsset'].startswith('sound/guest/cof/weapons_new/glock/')
        for e in glock_plan['studio5004']), 'all twelve original Glock Studio sounds mapped')
    glock_expected = {'models/guest/cof/common/flash1.mdl', 'sprites/guest/cof/gunsmoke.spr'}
    check(glock_expected.issubset(p['destination'] for p in glock_plan['assets']), 'actual muzzle resources included')
    glock_written = invoke('glock-write', ['--component', 'glock-and-shot-decals', '--write',
        '--expected-plan-sha256', json.loads(glock_dry.stdout)['planSha256']], expected=0,
        candidate=output/'glock'/'candidate', plan_path=glock_plan_path)
    check(json.loads(glock_written.stdout)['files'] == 50, 'Glock candidate CLI actual count')
    for row in glock_plan['assets']:
        copied = output/'glock'/'candidate'/row['destination']
        check(sha(copied) == row['sha256'] and copied.stat().st_size == row['bytes'],
              'actual Glock candidate identity '+row['destination'])
    glock_source = next(p for p in glock_plan['assets'] if p['original'] == 'models/weapons/glock/v_glock.mdl')
    check((output/'glock'/'candidate'/glock_source['destination']).read_bytes() == Path(glock_source['source']).read_bytes(),
          'all original Glock model events and geometry preserved')
    revolver_dry = invoke('revolver', ['--component', 'revolver-and-shot-decals'], expected=0)
    revolver_plan_path = output/'revolver'/'plan.json'
    revolver_plan = json.loads(revolver_plan_path.read_bytes())
    check(revolver_plan['weapon'] == 'Revolver' and revolver_plan['primaryAssets'] == 44 and
          revolver_plan['dependencyAssets'] == 1 and len(revolver_plan['assets']) == 45,
          '39 Revolver resources, five decals and one texture companion')
    check(len(revolver_plan['studio5004']) == 7 and all(
        e['localLogicalKey'].startswith('weapons_new/revolver/') and
        e['localAsset'].startswith('sound/guest/cof/weapons_new/revolver/')
        for e in revolver_plan['studio5004']), 'all original Revolver local Studio samples mapped')
    expected_revolver = {'events/guest/cof/revolvershells.sc', 'sound/guest/cof/weapons/revolver/revolver_shell.wav'}
    check(expected_revolver.issubset(p['destination'] for p in revolver_plan['assets']) and
          not any(p['destination']=='sound/guest/cof/weapons_new/revolver/revolver_shell.wav'
                  for p in revolver_plan['assets']), 'ordinary shell event and exact four local samples kept separate')
    revolver_written = invoke('revolver-write', ['--component', 'revolver-and-shot-decals', '--write',
        '--expected-plan-sha256', json.loads(revolver_dry.stdout)['planSha256']], expected=0,
        candidate=output/'revolver'/'candidate', plan_path=revolver_plan_path)
    check(json.loads(revolver_written.stdout)['files'] == 45, 'Revolver candidate CLI actual count')
    for row in revolver_plan['assets']:
        copied = output/'revolver'/'candidate'/row['destination']
        check(sha(copied)==row['sha256'] and copied.stat().st_size==row['bytes'],
              'actual Revolver candidate identity '+row['destination'])
    revolver_source=next(p for p in revolver_plan['assets'] if p['original']=='models/weapons/revolver/v_revolver.mdl')
    check((output/'revolver'/'candidate'/revolver_source['destination']).read_bytes()==Path(revolver_source['source']).read_bytes(),
          'all original Revolver model events and geometry preserved')
    after = {str(p): sha(p) for p in sorted(inputs)}
    check(before == after, 'all original/source/tool/consumer inputs unchanged')
    receipt = dict(success=all(p['passed'] for p in checks), revision=revision,
        checks=checks, commands=commands, before=before, after=after, unchanged=before == after,
        planSha256=dry_result['planSha256'], candidateManifestSha256=sha(candidate/'manifest.json'),
        primaryAssets=48, dependencyAssets=1, sourceCopies=0, resourceCopies=144,
        revolverPrimaryAssets=44, revolverDependencyAssets=1,
        glockPrimaryAssets=49, glockDependencyAssets=1,
        boundary='Real resource candidate copy and refusals only; no live cvar verification, deployment, original P grip, audio/particle admission, weapon backend, gameplay or performance acceptance.')
    (output/'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print(json.dumps(dict(success=receipt['success'], checks=len(checks), failed=[p['name'] for p in checks if not p['passed']],
        cliCases=len(commands), inputs=len(inputs), primaryAssets=48, dependencyAssets=1,
        revolverPrimaryAssets=44, revolverDependencyAssets=1,
        glockPrimaryAssets=49, glockDependencyAssets=1)))
    raise SystemExit(0 if receipt['success'] else 1)


if __name__ == '__main__':
    main()
