#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Black-box production HUD reader and actual Python plan-writer regression.

Real unpacked resources are read-only. Unrelated catalog/monster collectors use
recorded or empty boundaries, explicitly recorded below. No deployment/copy of
game assets; tiny synthetic files are test fixtures, never replacement assets.
"""
from pathlib import Path
import argparse
import contextlib
import csv
import hashlib
import importlib.util
import io
import json
import struct
import subprocess
import sys
from unittest import mock

BASE = {
    'maps/ZBS_message.cso', 'resource/cso_chn.txt', 'resource/font/df_gb_y9.ttf',
    'resource/Hud/hud_scoreboard_bg_humanscenario.tga',
    'resource/Hud/hud_text_icon_totalkill_left.tga',
    'resource/Hud/hud_text_icon_kill_right.tga', 'resource/Hud/hud_text_icon_round.tga',
    'resource/Hud/hud_sb_num_humanscenario.tga', 'resource/Hud/hud_sb_num_center.tga',
    'resource/Hud/hud_sb_num_bottom.tga', 'resource/Hud/hud_sb_num_bottom_colon.tga',
    'resource/hud/zbs/TextBg_Left.tga', 'resource/hud/zbs/TextBg_Center.tga',
    'resource/hud/zbs/TextBg_Right.tga',
}
DEADEND = BASE | {'sound/events/tutor_msg.wav'}
WAVE = 'maps/zbs_wave_zs_deadend.cso'
HEAD = 'waveID,spawnPoint,startTime,endTime,intervalTime,zombi,zombiRatio,eventmessage\n'


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def load(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data if isinstance(data, bytes) else data.encode('utf-8'))


def module(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


def packet(wave, messages):
    return struct.pack('<I', len(wave)) + wave + struct.pack('<I', len(messages)) + messages


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('source', 'reader', 'common', 'old-source', 'old-evidence', 'old-collector', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    source, common, out = args.source.resolve(strict=True), args.common.resolve(strict=True), args.output.resolve()
    for part in (out, *out.parents):
        if part.is_symlink() or part.is_junction(): raise ValueError('Linked test output refused')
    out.mkdir(parents=True, exist_ok=False)
    sys.dont_write_bytecode = True
    sys.path.insert(0, str(source / 'tools'))
    production = module('production_resources', source / 'tools/csol_scenario_resources.py')
    reference = module('independent_tea', source / 'tests/csol-scenario-message-table/prepare_expected.py')
    tool = source / 'tools/csol_scenario_resources.py'
    reader = args.reader.resolve(strict=True)
    assertions, native_calls, external_calls = [], [], []
    pinned = [identity(reader), identity(tool), identity(source / 'tools/csol_material_stage.py'),
              identity(Path(__file__)), identity(source / 'tests/csol-scenario-message-table/prepare_expected.py')]
    pinned += [identity(common / name) for name in sorted(DEADEND | {WAVE})]
    old_plan = args.old_evidence / 'resource-plan.json'
    old = load(old_plan)
    pinned += [identity(old_plan), identity(args.old_collector)]
    pinned += [identity(Path(row['source'])) for row in old['files']]

    def check(name, value):
        assertions.append(dict(name=name, passed=bool(value)))
        save(out / 'assertions.json', assertions)
        if not value: raise AssertionError(name)

    def rejected(name, callback, text=''):
        try: callback()
        except (ValueError, OSError, subprocess.CalledProcessError) as error:
            check(name, not text or text in str(error))
            save(out / (name + '.error.json'), dict(type=type(error).__name__, message=str(error)))
        else: check(name, False)

    def run_reader(name, raw, expected=None, diagnostic=None):
        result = subprocess.run([str(reader)], input=raw, capture_output=True, timeout=10)
        native_calls.append(dict(case=name, inputBytes=len(raw), inputSha256=hashlib.sha256(raw).hexdigest(),
            exitCode=result.returncode, stdout=result.stdout.decode('utf-8', errors='replace'),
            stderr=result.stderr.decode('utf-8', errors='replace')))
        save(out / 'native-calls.json', native_calls)
        if expected is None:
            check(name + ': rejected without partial output', result.returncode == 1 and not result.stdout and bool(result.stderr))
            if diagnostic: check(name + ': exact diagnostic family', diagnostic in result.stderr.decode('utf-8'))
        else:
            values = result.stdout.decode('utf-8').splitlines()
            check(name + ': exit0 and clean stderr', result.returncode == 0 and not result.stderr)
            check(name + ': exact sorted deduplicated closure', values == sorted(expected))
        return result

    wave, messages = (common / WAVE).read_bytes(), (common / 'maps/ZBS_message.cso').read_bytes()
    actual = packet(wave, messages)
    check('independent fixed HUD dependency count', len(BASE) == 14 and len(DEADEND) == 15)
    run_reader('actual-deadend', actual, DEADEND)
    rows = list(csv.reader(io.StringIO(reference.decode(messages).decode('cp949'))))
    by_id = {int(row[0]) & 65535: row for row in rows[1:] if row and row[0]}
    image_ids = [13, 179, 181, 867]
    image_assets = set()
    for number in image_ids:
        row = by_id[number]
        check(f'actual-message-{number}:15 columns', len(row) == 15)
        image_assets.add(row[2].replace('\\', '/') + '.tga')
        if row[3]: image_assets.add('sound/' + row[3].replace('\\', '/'))
    for name in sorted(image_assets):
        check('actual image dependency exists: ' + name, (common / name).is_file())
        pinned.append(identity(common / name))

    def synthetic_wave(ids):
        return reference.encode((HEAD + ''.join(f'1,9999,0,0,0,9999,1,{number}\n' for number in ids)).encode('ascii'))

    run_reader('actual-images-four-channels-and-duplicates', packet(synthetic_wave(image_ids + image_ids), messages), BASE | image_assets)
    check('actual image channels10/11/12/17', {int(by_id[i][10] or 0) + 10 for i in image_ids} == {10,11,12,17})
    missing_id = next(i for i in range(65536) if i not in by_id)
    run_reader('missing-message-id-is-ignored', packet(synthetic_wave([missing_id]), messages), BASE)
    for name, raw in [('empty', b''), ('prefix1', b'\x01'), ('zero-wave', b'\0'*4),
            ('oversized-wave', struct.pack('<I',8388609)), ('truncated-wave', struct.pack('<I',len(wave))+wave[:-1]),
            ('missing-second-length', struct.pack('<I',len(wave))+wave),
            ('zero-message', struct.pack('<I',len(wave))+wave+b'\0'*4),
            ('oversized-message', struct.pack('<I',len(wave))+wave+struct.pack('<I',8388609)),
            ('truncated-message', actual[:-1]), ('trailing-byte', actual+b'\0')]:
        run_reader('framing-'+name, raw, diagnostic='Expected length-prefixed')
    run_reader('unaligned-wave-block', packet(b'1234567', messages), diagnostic='Invalid encrypted scenario wave table')
    run_reader('unaligned-message-block', packet(wave,b'1234567'), diagnostic='invalid encrypted scenario message table size')
    run_reader('bad-wave-header', packet(reference.encode(b'bad\n'), messages), diagnostic='waveID header is missing')
    run_reader('bad-message-csv', packet(wave,reference.encode(b'id,text\n1,"unterminated')), diagnostic='CSV')
    unsafe = reference.encode(b'id,text,image,sound\n13,,resource/../outside,\n')
    run_reader('unsafe-dialogue-path', packet(synthetic_wave([13]),unsafe), diagnostic='invalid scenario dialogue image path')
    unsafe_sound = reference.encode(b'id,text,image,sound\n13,,,../outside.wav\n')
    run_reader('unsafe-dialogue-sound', packet(synthetic_wave([13]),unsafe_sound), diagnostic='invalid scenario dialogue sound path')
    partial = reference.encode(b'id,text,image,sound\n13,,resource/first,\n14,,resource/../outside,\n')
    run_reader('later-row-failure-has-no-partial-output', packet(synthetic_wave([13,14]),partial), diagnostic='invalid scenario dialogue image path')

    selected, info = production.dialogue_contract(source,common,reader,WAVE)
    check('actual Python contract exact15 closure', selected == DEADEND and info['resources'] == sorted(DEADEND))
    check('contract ties exact wave bytes and compiled helper', info['wave'] == WAVE and info['readerSha256'] == identity(reader)['sha256'] and
          info['inputs'] == [dict(path=n, bytes=len(d), sha256=hashlib.sha256(d).hexdigest()) for n,d in [(WAVE,wave),('maps/ZBS_message.cso',messages)]])
    rejected('missing-reader-required',lambda:production.dialogue_contract(source,common,None,WAVE),'needs')
    rejected('missing-wave-required',lambda:production.dialogue_contract(source,common,reader,None),'needs')
    for index, bad in enumerate(['../maps/x.cso','maps/../x.cso','/maps/x.cso','resource/x.cso','maps/x.txt','maps//x.cso']):
        rejected(f'bad-wave-path-{index}',lambda:production.dialogue_contract(source,common,reader,bad))
    check('old frozen DLL ignores new optional inputs', production.dialogue_contract(args.old_source,common,None,None)==(set(),None))

    # Actual writer and dialogue collector; unrelated native catalog/monster
    # closures are explicit stable boundaries. No game files are copied.
    def invoke(name, root, resources, previous, old_mode=False):
        destination = out/name
        old_manifest = load(args.old_evidence/'manifest.json')
        def command(program,*values):
            external_calls.append(dict(case=name,arguments=list(map(str,values))))
            if values[0]=='--catalog': return dict(effectiveTypes=old_manifest['effectiveTypes'] if old_mode else [])
            if values[0]=='--plan':
                return load(args.old_evidence/'runtime-closure.json') if old_mode else {'uniqueDependencyFiles':load(Path(values[1]))['fileCount']}
            if old_mode: return load(args.old_collector)
            raise AssertionError('Unrelated collector unexpectedly invoked')
        argv=[str(tool),str(resources),str(reader),str(reader),str(previous),str(destination),'--source-root',str(root)]
        if not old_mode: argv += ['--dialogue-reader',str(reader),'--wave-resource',WAVE]
        stdout=io.StringIO()
        with contextlib.ExitStack() as stack:
            stack.enter_context(mock.patch.object(sys,'argv',argv))
            stack.enter_context(mock.patch.object(production,'command',command))
            if not old_mode:
                stack.enter_context(mock.patch.object(production,'boss_contract',lambda _:None))
                stack.enter_context(mock.patch.object(production,'skill_contract',lambda _:set()))
                stack.enter_context(mock.patch.object(production,'smoke_event_contract',lambda _:None))
                stack.enter_context(mock.patch.object(production,'elite_contract',lambda _:set()))
            with contextlib.redirect_stdout(stdout): production.main()
        write(out/(name+'.stdout.log'),stdout.getvalue())
        return load(destination/'resource-plan.json'),load(destination/'manifest.json')

    empty=out/'empty-plan.json';save(empty,dict(files=[],models=[],blocked=[],fileCount=0))
    plan,manifest=invoke('actual-plan',source,common,empty)
    check('actual writer exact15 files and added paths', plan['fileCount']==15 and {row['destination'] for row in plan['files']}==DEADEND and set(manifest['added'])==DEADEND)
    check('actual writer hashes every real resource', all(row['sha256']==identity(common/row['destination'])['sha256'] for row in plan['files']))
    check('plan only no physical copies or deployment', manifest['physicalCopies']==0 and not manifest['deployment'] and
          {p.name for p in (out/'actual-plan').iterdir()}=={'resource-plan.json','runtime-closure.json','manifest.json'})
    repeat,repeat_manifest=invoke('repeat',source,common,out/'actual-plan/resource-plan.json')
    check('repeat preserves complete plan and adds nothing',repeat==plan and repeat_manifest['added']==[])
    check('repeat is byte-identical', (out/'repeat/resource-plan.json').read_bytes()==(out/'actual-plan/resource-plan.json').read_bytes())
    check('old baseline has93 files',old['fileCount']==len(old['files'])==93)
    retained,_=invoke('old93',args.old_source,common,old_plan,True)
    check('old frozen source exact byte-stable93 plan',retained['files']==old['files'] and
          (out/'old93/resource-plan.json').read_bytes()==old_plan.read_bytes())
    check('old frozen source adds no dialogue closure','dialogueClosure' not in retained)

    # Separate tiny synthetic assets exercise absence/conflict without touching
    # real common, frozen sources, hardlinks or deployment destinations.
    fake=out/'synthetic-common'
    fake_wave=synthetic_wave([13])
    fake_messages=reference.encode(b'id,text,image,sound\n13,,resource/test-dialogue,events/test.wav\n')
    fake_paths=BASE|{'resource/test-dialogue.tga','sound/events/test.wav'}
    for name in fake_paths: write(fake/name,b'explicit tiny synthetic fixture\n')
    write(fake/WAVE,fake_wave);write(fake/'maps/ZBS_message.cso',fake_messages)
    fixture_plan,_=invoke('synthetic-plan',source,fake,empty)
    check('synthetic image/sound closure uses real helper',fixture_plan['fileCount']==len(fake_paths) and {r['destination'] for r in fixture_plan['files']}==fake_paths)
    missing=fake/'resource/test-dialogue.tga'
    missing.rename(fake/'resource/test-dialogue.saved')
    rejected('missing-dialogue-file',lambda:invoke('missing-output',source,fake,empty))
    check('missing file fails before writing plan',not(out/'missing-output').exists())
    (fake/'resource/test-dialogue.saved').rename(missing)
    foreign=out/'foreign-resource.fixture';write(foreign,b'previous distinct resource bytes')
    conflict=out/'conflict.json'
    save(conflict,dict(files=[dict(source=str(foreign),destination='resource/test-dialogue.tga',
        bytes=foreign.stat().st_size,sha256=identity(foreign)['sha256'])],models=[]))
    rejected('conflicting-existing-resource',lambda:invoke('conflict-output',source,fake,conflict),'Conflicting previous')
    check('conflict rejected before output creation',not(out/'conflict-output').exists())
    messages_path=fake/'maps/ZBS_message.cso';messages_path.rename(fake/'maps/message.saved')
    rejected('missing-input-table',lambda:production.dialogue_contract(source,fake,reader,WAVE))
    (fake/'maps/message.saved').rename(messages_path)
    wave_path=fake/WAVE;wave_path.rename(fake/'maps/wave.saved')
    rejected('missing-input-wave',lambda:production.dialogue_contract(source,fake,reader,WAVE))
    (fake/'maps/wave.saved').rename(wave_path)
    write(messages_path,b'')
    rejected('empty-input-table',lambda:production.dialogue_contract(source,fake,reader,WAVE),'Invalid dialogue input size')
    write(messages_path,fake_messages)
    altered=out/'mismatched-source'
    for name in ['native/csol/server/scenario/core/scenario_session.cpp','native/csol/scenario/core/scenario_round_hud.hpp']:
        write(altered/name,(source/name).read_bytes())
    header=altered/'native/csol/scenario/core/scenario_round_hud.hpp'
    write(header,header.read_text(encoding='utf8').replace('resource/font/df_gb_y9.ttf','resource/font/different.ttf'))
    rejected('helper-source-contract-mismatch',lambda:production.dialogue_contract(altered,common,reader,WAVE),'does not contain')
    check('all pinned real resources/tool inputs unchanged', all(identity(Path(row['path']))==row for row in pinned))
    save(out/'receipt.json',dict(checks=len(assertions),failures=0,assertions=assertions,inputs=pinned,nativeCalls=native_calls,
        controlledCatalogCollectorCalls=external_calls,realDeadendResources=15,realImageMessages=image_ids,
        gameAssetCopies=0,deployed=False,gameplay=False,
        boundary='Actual new native helper and actual Python dialogue contract/plan writer. Native catalog/monster collector outputs are recorded or controlled; no full DLL, HTTP download or gameplay.'))
    print(f'scenario_resources_cli checks={len(assertions)} failures=0')


if __name__=='__main__':main()
