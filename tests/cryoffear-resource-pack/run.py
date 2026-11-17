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
    parser.add_argument('--mp5-player-model', type=Path, required=True)
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
    vp70_baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root, output=output/'vp70'/'candidate',
        plan=output/'vp70'/'plan.json', server_model_sounds='disabled', component='vp70-and-shot-decals'))
    tmp_baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root, output=output/'tmp'/'candidate',
        plan=output/'tmp'/'plan.json', server_model_sounds='disabled', component='tmp-and-shot-decals'))
    mp5_baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root, output=output/'mp5'/'candidate',
        plan=output/'mp5'/'plan.json', server_model_sounds='disabled', component='mp5-and-shot-decals',
        authored_player_model=args.mp5_player_model))
    m16_baseline = tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root,output=output/'m16'/'candidate',
        plan=output/'m16'/'plan.json',server_model_sounds='disabled',component='m16-and-shot-decals'))
    g43_baseline=tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=args.inherited_game_root,output=output/'g43'/'candidate',
        plan=output/'g43'/'plan.json',server_model_sounds='disabled',component='g43-and-shot-decals'))
    shotgun_baseline=tool.plan(SimpleNamespace(original_game_root=args.original_game_root,
        inherited_game_root=None,output=output/'shotgun'/'candidate',
        plan=output/'shotgun'/'plan.json',server_model_sounds='disabled',component='shotgun-and-shot-decals'))
    inputs = {Path(__file__).resolve(), Path(__file__).with_name('README.md'), TOOL, tool.HEADER,
              *(ROOT/p for p in tool.CONSUMERS), *(Path(p['source']) for p in baseline['assets']),
              *(Path(p['source']) for p in glock_baseline['assets']),
              *(Path(p['source']) for p in revolver_baseline['assets']),
              *(Path(p['source']) for p in vp70_baseline['assets']),
              *(Path(p['source']) for p in tmp_baseline['assets']),
              *(Path(p['source']) for p in mp5_baseline['assets']),
              *(Path(p['source']) for p in m16_baseline['assets']),
              *(Path(p['source']) for p in g43_baseline['assets']),
              *(Path(p['source']) for p in shotgun_baseline['assets'])}
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
    vp70_dry = invoke('vp70', ['--component', 'vp70-and-shot-decals'], expected=0)
    vp70_plan_path = output/'vp70'/'plan.json'
    vp70_plan = json.loads(vp70_plan_path.read_bytes())
    check(vp70_plan['weapon']=='VP70' and vp70_plan['primaryAssets']==44 and
          vp70_plan['dependencyAssets']==1 and len(vp70_plan['assets'])==45,
          '39 VP70 resources, five decals and one real texture companion')
    check([(e['sequence'],e['frame']) for e in vp70_plan['studio5004']]==
          [(6,10),(6,51),(7,10),(7,51),(7,76)] and all(
          e['localLogicalKey'].startswith('weapons_new/vp70/') for e in vp70_plan['studio5004']),
          'five actual VP70 Studio sound events retain original order')
    vp70_written = invoke('vp70-write', ['--component','vp70-and-shot-decals','--write',
        '--expected-plan-sha256',json.loads(vp70_dry.stdout)['planSha256']], expected=0,
        candidate=output/'vp70'/'candidate',plan_path=vp70_plan_path)
    check(json.loads(vp70_written.stdout)['files']==45,'VP70 actual candidate count')
    for row in vp70_plan['assets']:
        copied=output/'vp70'/'candidate'/row['destination']
        check(sha(copied)==row['sha256'] and copied.stat().st_size==row['bytes'],
              'actual VP70 candidate identity '+row['destination'])
    vp70_source=next(p for p in vp70_plan['assets'] if p['original']=='models/weapons/vp70/v_vp70.mdl')
    check((output/'vp70'/'candidate'/vp70_source['destination']).read_bytes()==Path(vp70_source['source']).read_bytes(),
          'all original VP70 model events and geometry preserved')
    tmp_dry = invoke('tmp', ['--component', 'tmp-and-shot-decals'], expected=0)
    tmp_plan_path = output/'tmp'/'plan.json'
    tmp_plan = json.loads(tmp_plan_path.read_bytes())
    check(tmp_plan['weapon']=='TMP' and tmp_plan['primaryAssets']==46 and
          tmp_plan['dependencyAssets']==1 and len(tmp_plan['assets'])==47,
          '41 TMP resources, five decals and one inherited texture companion')
    check([(e['sequence'],e['frame']) for e in tmp_plan['studio5004']]==
          [(2,20),(5,21),(5,51),(6,21),(6,51),(6,75),(6,83)] and
          len({e['localLogicalKey'] for e in tmp_plan['studio5004']})==5,
          'seven actual TMP Studio events use five independent local samples')
    destinations={p['destination'] for p in tmp_plan['assets']}
    for sound in ('tmp_shoot_loop.wav','tmp_shoot_end.wav'):
        check('sound/guest/cof/weapons/tmp/'+sound in destinations and
              'sound/guest/cof/weapons_new/tmp/'+sound not in destinations,
              'automatic native sample is not invented as local Studio sample '+sound)
    tmp_written = invoke('tmp-write', ['--component','tmp-and-shot-decals','--write',
        '--expected-plan-sha256',json.loads(tmp_dry.stdout)['planSha256']], expected=0,
        candidate=output/'tmp'/'candidate',plan_path=tmp_plan_path)
    check(json.loads(tmp_written.stdout)['files']==47,'TMP actual candidate count')
    for row in tmp_plan['assets']:
        copied=output/'tmp'/'candidate'/row['destination']
        check(copied.is_file() and sha(copied)==row['sha256'] and copied.stat().st_nlink==1,
              'actual TMP independent candidate identity '+row['destination'])
    tmp_source=next(p for p in tmp_plan['assets'] if p['original']=='models/weapons/tmp/v_tmp.mdl')
    check((output/'tmp'/'candidate'/tmp_source['destination']).read_bytes()==Path(tmp_source['source']).read_bytes(),
          'all original TMP geometry, animations and Studio events preserved')
    mp5_args = ['--component', 'mp5-and-shot-decals', '--authored-player-model', args.mp5_player_model.resolve()]
    invoke('mp5-missing-player', ['--component', 'mp5-and-shot-decals'])
    invoke('mp5-player-wrong-family', ['--authored-player-model', args.mp5_player_model.resolve()])
    unmodified_world = output/'fixtures'/'unmodified-mp5-world.mdl'
    mp5_world = next(p for p in mp5_baseline['assets'] if p['destination']=='models/guest/cof/mp5/w_mp5.mdl')
    shutil.copyfile(mp5_world['source'], unmodified_world)
    invoke('mp5-world-as-player', ['--component','mp5-and-shot-decals','--authored-player-model', unmodified_world])
    mp5_dry = invoke('mp5', mp5_args, expected=0)
    mp5_plan_path = output/'mp5'/'plan.json'
    mp5_plan = json.loads(mp5_plan_path.read_bytes())
    check(mp5_plan['weapon']=='MP5' and mp5_plan['primaryAssets']==45 and
          mp5_plan['dependencyAssets']==1 and len(mp5_plan['assets'])==46,
          '40 MP5 resources, five decals and one inherited texture companion')
    check([(e['sequence'],e['frame']) for e in mp5_plan['studio5004']]==
          [(2,30),(2,67),(3,17),(3,53),(3,88),(3,108)] and
          len({e['localLogicalKey'] for e in mp5_plan['studio5004']})==4,
          'six original MP5 Studio events use four local samples')
    player_row = next(p for p in mp5_plan['assets'] if p['destination']=='models/guest/cof/mp5/p_mp5.mdl')
    check(player_row['sourceKind']=='explicit-host-authored-player-model' and
          not player_row['originalBytesPreserved'] and
          player_row['originalInput']['sha256']==mp5_world['sha256'] and
          player_row['sha256']!=mp5_world['sha256'], 'authored P retains explicit original W provenance')
    check(all(p['originalBytesPreserved'] for p in mp5_plan['assets'] if p is not player_row),
          'all other MP5 original resource bytes are preserved')
    flash2 = next(p for p in mp5_plan['assets'] if p['original']=='models/flash2.mdl')
    check(flash2['group']=='MP5ClientModels' and flash2['destination']=='models/guest/cof/common/flash2.mdl' and
          flash2['bytes']==122304 and flash2['sha256']=='f7f098c0db5eb3254458593d14c6b5a2e2e37af2d4afc1a1e56bb166c1df778c',
          'original local flash2 is a distinct client model dependency')
    mp5_written = invoke('mp5-write', [*mp5_args,'--write',
        '--expected-plan-sha256',json.loads(mp5_dry.stdout)['planSha256']], expected=0,
        candidate=output/'mp5'/'candidate',plan_path=mp5_plan_path)
    check(json.loads(mp5_written.stdout)['files']==46, 'MP5 actual candidate count')
    for row in mp5_plan['assets']:
        copied=output/'mp5'/'candidate'/row['destination']
        check(copied.is_file() and sha(copied)==row['sha256'] and copied.stat().st_nlink==1,
              'actual MP5 independent candidate identity '+row['destination'])
    m16_args=['--component','m16-and-shot-decals']
    m16_dry=invoke('m16',m16_args,expected=0)
    m16_plan_path=output/'m16'/'plan.json';m16_plan=json.loads(m16_plan_path.read_bytes())
    check(m16_plan['weapon']=='M16' and m16_plan['primaryAssets']==44 and m16_plan['dependencyAssets']==1 and len(m16_plan['assets'])==45,
          'M16 thirty-nine resources, five decals and one explicit inherited texture')
    check(len(m16_plan['studio5004'])==20 and len({e['localLogicalKey'] for e in m16_plan['studio5004']})==6,
          'all twenty M16 Studio events retain six local sample keys')
    m16_names={p['destination'] for p in m16_plan['assets']}
    check('models/guest/cof/common/556shell.mdl' in m16_names and 'models/guest/cof/common/flash2.mdl' in m16_names,
          'M16 real rifle-shell and flash2 models retained')
    for name in ('m16_magtap','m16_cock1','m16_cock2','m16_switch','m16_fire'):
        check('sound/guest/cof/weapons_new/m16/'+name+'.wav' in m16_names and 'sound/guest/cof/weapons/m16/'+name+'.wav' in m16_names,
              'M16 local/native sound counterparts '+name)
    check('sound/guest/cof/weapons_new/m16/m16_cock.wav' not in m16_names and 'sound/guest/cof/weapons/m16/m16_cock.wav' in m16_names,
          'unconsumed local legacy cock excluded while original native precache preserved')
    m16_written=invoke('m16-write',[*m16_args,'--write','--expected-plan-sha256',json.loads(m16_dry.stdout)['planSha256']],expected=0,
        candidate=output/'m16'/'candidate',plan_path=m16_plan_path)
    check(json.loads(m16_written.stdout)['files']==45,'actual M16 candidate count')
    for row in m16_plan['assets']:
        copied=output/'m16'/'candidate'/row['destination']
        check(sha(copied)==row['sha256'] and copied.stat().st_size==row['bytes'] and copied.stat().st_nlink==1,
              'actual M16 independent candidate identity '+row['destination'])
    m16_view=next(row for row in m16_plan['assets'] if row['destination']=='models/guest/cof/m16/v_m16.mdl')
    check((output/'m16'/'candidate'/m16_view['destination']).read_bytes()==Path(m16_view['source']).read_bytes(),
          'all M16 original geometry animations and model events preserved')
    g43_args=['--component','g43-and-shot-decals']
    g43_dry=invoke('g43',g43_args,expected=0)
    g43_plan_path=output/'g43'/'plan.json';g43_plan=json.loads(g43_plan_path.read_bytes())
    check(g43_plan['weapon']=='G43' and g43_plan['primaryAssets']==37 and g43_plan['dependencyAssets']==1 and len(g43_plan['assets'])==38,
          'G43 thirty-two resources, five decals and inherited texture companion')
    check(len(g43_plan['studio5004'])==5 and len({e['localLogicalKey'] for e in g43_plan['studio5004']})==3,
          'five real G43 reload events map to three new local sample keys')
    g43_names={row['destination'] for row in g43_plan['assets']}
    check('models/guest/cof/common/shell.mdl' in g43_names and 'models/guest/cof/common/556shell.mdl' not in g43_names,
          'G43 retains original small shell and never inherits M16 shell')
    check('models/guest/cof/common/flash1.mdl' in g43_names and 'models/guest/cof/common/flash2.mdl' in g43_names,
          'G43 keeps independent original world and local flash models')
    for suffix in ('fire','cock','magin','magout'):
        check('sound/guest/cof/weapons_new/g43/g43_'+suffix+'.wav' in g43_names and
              'sound/guest/cof/weapons/g43/g43_'+suffix+'.wav' in g43_names,'G43 native/local sample pair '+suffix)
    g43_write=invoke('g43-write',[*g43_args,'--write','--expected-plan-sha256',json.loads(g43_dry.stdout)['planSha256']],expected=0,
        candidate=output/'g43'/'candidate',plan_path=g43_plan_path)
    check(json.loads(g43_write.stdout)['files']==38,'actual G43 resource candidate count')
    for row in g43_plan['assets']:
        copied=output/'g43'/'candidate'/row['destination']
        check(copied.is_file() and copied.stat().st_nlink==1 and sha(copied)==row['sha256'] and copied.stat().st_size==row['bytes'],
              'G43 actual independent candidate identity '+row['destination'])
    view=next(row for row in g43_plan['assets'] if row['destination']=='models/guest/cof/g43/v_g43.mdl')
    check((output/'g43'/'candidate'/view['destination']).read_bytes()==Path(view['source']).read_bytes(),
          'G43 original geometry, animation channels and event records preserved')
    shotgun_args=['--component','shotgun-and-shot-decals']
    shotgun_dry=invoke('shotgun',shotgun_args,inherit=False,expected=0)
    shotgun_path=output/'shotgun'/'plan.json';shotgun_plan=json.loads(shotgun_path.read_bytes())
    check(shotgun_plan['weapon']=='Shotgun' and len(shotgun_plan['assets'])==37 and
          shotgun_plan['primaryAssets']==37 and shotgun_plan['dependencyAssets']==0,
          '870 thirty-two declared assets and five decals without pistol companion')
    names={row['destination'] for row in shotgun_plan['assets']}
    check('models/guest/cof/common/shotgunshell.mdl' in names and
          'models/guest/cof/common/flash1.mdl' in names and
          not any('w_9mmclip' in name or 'flash2' in name for name in names),
          '870 uses exact shotgun shell and flash1 without magazine/flash2')
    events=shotgun_plan['studio5004']
    missing=[event for event in events if event.get('originalMissingLocalFile')]
    check(len(events)==7 and len(missing)==1 and missing[0]['sequence']==6 and
          missing[0]['frame']==1 and missing[0]['localAsset'] is None and missing[0]['remoteAsset'] is None and
          missing[0]['localLogicalKey']=='weapons_new/syringe/syringe_sleeve.wav',
          '870 raw draw request remains original failed local load without replacement asset')
    check(all(event['localAsset'].startswith('sound/guest/cof/weapons_new/shotgun/') for event in events if not event.get('originalMissingLocalFile')),
          '870 insert and pump events use existing original local consumers')
    check(all('syringe' not in name for name in names),'870 never authors an unconsumed legacy draw WAV')
    write=invoke('shotgun-write',[*shotgun_args,'--write','--expected-plan-sha256',json.loads(shotgun_dry.stdout)['planSha256']],
        inherit=False,expected=0,candidate=output/'shotgun'/'candidate',plan_path=shotgun_path)
    check(json.loads(write.stdout)['files']==37,'actual 870 resource candidate count')
    for row in shotgun_plan['assets']:
        copied=output/'shotgun'/'candidate'/row['destination']
        check(copied.stat().st_nlink==1 and copied.stat().st_size==row['bytes'] and sha(copied)==row['sha256'],
              '870 actual independent candidate '+row['destination'])
    view=next(row for row in shotgun_plan['assets'] if row['destination']=='models/guest/cof/shotgun/v_shotgun.mdl')
    check((output/'shotgun'/'candidate'/view['destination']).read_bytes()==Path(view['source']).read_bytes(),
          '870 original model geometry animation and all events preserved')
    after = {str(p): sha(p) for p in sorted(inputs)}
    check(before == after, 'all original/source/tool/consumer inputs unchanged')
    receipt = dict(success=all(p['passed'] for p in checks), revision=revision,
        checks=checks, commands=commands, before=before, after=after, unchanged=before == after,
        planSha256=dry_result['planSha256'], candidateManifestSha256=sha(candidate/'manifest.json'),
        primaryAssets=48, dependencyAssets=1, sourceCopies=0, resourceCopies=327+len(g43_plan['assets'])+len(shotgun_plan['assets']),
        shotgunPrimaryAssets=37,shotgunDependencyAssets=0,
        g43PrimaryAssets=37,g43DependencyAssets=1,
        m16PrimaryAssets=44,m16DependencyAssets=1,
        mp5PrimaryAssets=44, mp5DependencyAssets=1,
        tmpPrimaryAssets=46, tmpDependencyAssets=1,
        vp70PrimaryAssets=44, vp70DependencyAssets=1,
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
