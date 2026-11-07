#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze a fresh money-HUD regression proposal without invoking a compiler."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import re
import shutil

from prepare import ROOT, RUNNER_SHA, extract, identity, key, read, safe, save


def functions(path, signatures):
    """Preserve exact original functions, including simple one-line accessors."""
    raw = Path(path).read_bytes()
    pieces, spans = [], []
    for signature in signatures:
        found = list(re.finditer(rb'(?m)^' + re.escape(signature.encode('utf-8')), raw))
        if len(found) != 1:
            raise ValueError('Ambiguous production function: '+signature)
        start = found[0].start()
        line_end = raw.find(b'\n',start)
        if line_end < 0:
            line_end = len(raw)-1
        line = raw[start:line_end+1]
        if b'{' in line and line.rstrip().endswith(b'}'):
            section = line
            spans.append(dict(signature=signature,byteStart=start,byteEnd=line_end+1,
                firstLine=raw[:start].count(b'\n')+1,lastLine=raw[:line_end+1].count(b'\n'),
                bytes=len(section),sha256=hashlib.sha256(section).hexdigest()))
        else:
            section, rows = extract(path,[signature])
            spans.extend(rows)
        pieces.append(section)
    return b'\n'.join(pieces), spans


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path, default=ROOT/'build/csol-hud-style-20261102/prepare-02-final')
    parser.add_argument('--resources', type=Path, default=ROOT/'build/csol-hud-style-20261102/resources-06')
    args = parser.parse_args()
    out, previous, resources = map(safe, (args.output, args.previous, args.resources))
    if out.exists() or not out.is_relative_to(ROOT/'build') or out == ROOT/'build':
        raise ValueError('Use a fresh workspace build directory')
    plan = json.loads(read(previous/'proposal.json'))
    result = json.loads(read(previous/'result.json'))
    if not result['success'] or not result['inputsUnchanged']:
        raise ValueError('Previous HUD regression must have passed with unchanged inputs')
    if identity(previous/'run.py')['sha256'] != RUNNER_SHA:
        raise ValueError('Regression runner identity changed')

    # Validate the whole fresh resource set before creating a proposal folder.
    audit_path = resources.with_name(resources.name+'-manifest.json')
    audit = json.loads(read(audit_path))
    resource_inputs = []
    expected = {row['path'].casefold(): row for row in audit['files']}
    if len(expected) != len(audit['files']):
        raise ValueError('Case-colliding resource names')
    actual = set()
    for path in sorted(resources.rglob('*')):
        safe(path)
        if not path.is_file():
            continue
        name = path.relative_to(resources).as_posix().casefold()
        row = identity(path)
        if name not in expected or (row['bytes'], row['sha256']) != (expected[name]['bytes'], expected[name]['sha256']):
            raise ValueError('Resource identity mismatch: '+name)
        row['links'] = path.stat().st_nlink
        if row['links'] != 1:
            raise ValueError('Resource preparation requires independent outputs: '+name)
        actual.add(name)
        resource_inputs.append(row)
    money_paths = {'resource/csol/hud/v1/money_'+name+'.tga' for name in ('bg','digits','dollar','plus','minus')}
    font_paths = {'resource/font/sarasa_ui_sc.ttf','resource/csol/hud/v1/csolhudscheme.res'}
    if actual != set(expected) or len(actual) != 237 or not (money_paths | font_paths).issubset(actual):
        raise ValueError('Expected the complete 237-file money and original-font resource set')
    resource_inputs.extend(identity(path) for path in (audit_path, resources.with_name(resources.name+'-validation.json')))

    out.mkdir(parents=True)
    source, output = out/'source', out/'output'
    source.mkdir()
    roots = ['tests/csol-hud-style/pure.cpp','tests/csol-hud-style/client.cpp',
        'tests/csol-hud-style/contracts.cpp','tests/csol-hud-style/level.cpp',
        'tests/csol-hud-style/weapon_state.cpp','tests/csol-hud-style/prepare.py',
        'tests/csol-hud-style/prepare_money.py','tests/csol-hud-style/README.md',
        'native/csol/hud_style.cpp','native/csol/image_codec.cpp',
        'native/csol/client/weapons/weapons.cpp','native/csol/client/weapons/beamgun.cpp',
        'native/csol/client/weapons/weapon_inventory.cpp','native/csol/client/weapons/k3.cpp','native/csol/client/weapons/cannon.cpp']
    pending = [ROOT/name for name in roots]
    sources, copied = [], set()

    def freeze(original):
        original = safe(original)
        if not original.is_relative_to(ROOT) or not original.is_file():
            raise ValueError('Source missing or outside workspace: '+str(original))
        relative = original.relative_to(ROOT)
        target = source/relative
        before = identity(original)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        if target.stat().st_nlink != 1 or os.path.samefile(original,target) or identity(target)['sha256'] != before['sha256']:
            raise ValueError('Independent source copy failed: '+str(relative))
        sources.append(dict(relative=relative.as_posix(),original=before,frozen=identity(target)))
        copied.add(original)

    while pending:
        original = safe(pending.pop())
        if original in copied:
            continue
        freeze(original)
        if original.suffix in ('.cpp','.hpp','.h'):
            for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"',read(original),re.M):
                if name.startswith('generated/'):
                    continue
                candidate = next((p for p in (original.parent/name,ROOT/name) if p.is_file()),None)
                if candidate is None:
                    raise ValueError('Unresolved quoted include: '+name)
                pending.append(candidate)
    freeze(ROOT/'native/csol/server/characters.cpp')

    generated = source/'tests/csol-hud-style/generated'
    generated.mkdir()
    spans, generated_inputs = [], []
    native_path = source/'native/csol/client/weapons/weapons.cpp'
    raw = native_path.read_bytes()
    start = raw.index(b'struct NativeVitalRun\r\n') if b'struct NativeVitalRun\r\n' in raw else raw.index(b'struct NativeVitalRun\n')
    end = raw.index(b'int nativeAmmoWeapon{};',start)
    section = raw[start:end]
    state_path = generated/'native_state.inc'
    state_path.write_bytes(section)
    generated_inputs.append(identity(state_path))
    spans.append(dict(source=identity(native_path),functions=[dict(signature='NativeVitalRun through nativeAmmoDivider declarations',
        byteStart=start,byteEnd=end,firstLine=raw[:start].count(b'\n')+1,lastLine=raw[:end].count(b'\n'),
        bytes=len(section),sha256=hashlib.sha256(section).hexdigest())],generated=identity(state_path)))
    for name,relative,signatures in [
        ('native_draw.inc','native/csol/client/weapons/weapons.cpp',
            ['bool Matches(', 'bool NativeMoneySprite(', 'int MoneySpriteY(', 'bool Inside(',
                'void PrepareStyledNativeAmmo(', 'bool HideStyledSprite(', 'void DrawSprite(', 'void Fill(']),
        ('server_precache.inc','native/csol/server/characters.cpp',['std::string Load(', 'void PrecacheHudStyle(']),
        ('beam_hud.inc','native/csol/client/weapons/beamgun.cpp',['void DrawBeamgunHUD(']),
        ('inventory_state.inc','native/csol/client/weapons/weapon_inventory.cpp',
            ['std::span<const std::byte> Bytes(', 'int Definition(', 'int Current(',
             'void ObserveLegacyWeaponDefinition(', 'void ObserveSelectionAmmo(', 'void ObserveSelectionClip(',
             'int WeaponReserveAmmo(', 'const WeaponDefinition* FindWeaponDefinition(',
             'const WeaponCurrent& CurrentExtendedWeapon(', 'bool ExtendedWeaponActive(']),
        ('weapon_state.inc','native/csol/client/weapons/weapons.cpp',
            ['bool Custom(', 'bool K3Active(', 'void ApplyNativeWeaponVisibility(', 'int AmmoTotal(',
             'int CurWeapon(', 'int Ammo(', 'void ReceiveExtendedWeaponDefinition(',
             'void ReceiveExtendedWeaponState(', 'hud_style::WeaponState CurrentWeaponHudState(']),
        ('k3_state.inc','native/csol/client/weapons/k3.cpp',
            ['void ResetK3(', 'void ReceiveK3State(', 'int K3PredictedAmmo(', 'int K3PredictedReserve(']),
        ('cannon_state.inc','native/csol/client/weapons/cannon.cpp',
            ['void ReceiveCannonState(', 'int CannonPredictedAmmo('])]:
        section, function_rows = functions(source/relative,signatures)
        path = generated/name
        path.write_bytes(section)
        generated_inputs.append(identity(path))
        spans.append(dict(source=identity(source/relative),functions=function_rows,generated=identity(path)))
    save(out/'production-spans.json',spans)
    aliases = {key(row['name']):row for row in json.loads(read(plan['overlay']['path']))['roots']}
    for row in sources:
        suffix = '\\'+row['relative'].replace('/','\\').lower()
        for name in list(aliases):
            if name.lower().endswith(suffix):
                aliases[name]['external-contents'] = row['frozen']['path']
        for name in (ROOT/row['relative'],source/row['relative']):
            aliases[key(name)] = dict(type='file',name=str(name),**{'external-contents':row['frozen']['path']})
    overlay = out/'source-overlay.json'
    save(overlay,dict(version=0,**{'case-sensitive':False,'use-external-names':False},roots=list(aliases.values())))
    save(out/'source-changes.json',sources)
    save(out/'resource-inputs.json',resource_inputs)
    flags = [arg.replace(plan['overlay']['path'],str(overlay)) for arg in plan['compileFlags']]
    if '/clang:'+str(overlay) not in flags:
        raise ValueError('Frozen include overlay is not passed to the compiler')
    compiler = plan['compiler']
    linker = str(Path(compiler).with_name('lld-link.exe'))
    jobs = []
    for name,relative in [('pure','tests/csol-hud-style/pure.cpp'),('client','tests/csol-hud-style/client.cpp'),
        ('contracts','tests/csol-hud-style/contracts.cpp'),('style','native/csol/hud_style.cpp'),
        ('codec','native/csol/image_codec.cpp'),('weapons-full-tu','native/csol/client/weapons/weapons.cpp'),
        ('beamgun-full-tu','native/csol/client/weapons/beamgun.cpp'),('level','tests/csol-hud-style/level.cpp'),
        ('weapon_state','tests/csol-hud-style/weapon_state.cpp'),('inventory-full-tu','native/csol/client/weapons/weapon_inventory.cpp')]:
        jobs.append(dict(name='compile-'+name,kind='compile',seconds=120,expectExit=0,
            command=[compiler,*flags,'/showIncludes','-I'+str(source),'/Fo'+str(output/(name+'.obj')),
                '/Fd'+str(output/(name+'.compile.pdb')),'-c','--',str(source/relative)]))
    old_link = next(job for job in plan['jobs'] if job['name']=='link-client')
    libraries = [arg for arg in old_link['command'] if arg.lower().endswith('.lib')]
    for name,objects,arguments in [('pure',['pure','style'],[]),('client',['client','style','codec'],[str(resources)]),
        ('contracts',['contracts','style'],[]),('level',['level','codec'],[]),('weapon_state',['weapon_state'],[])]:
        binary = output/('hud-'+name+'.exe')
        jobs.append(dict(name='link-'+name,kind='link',seconds=180,expectExit=0,
            command=[linker,'/nologo','/subsystem:console','/machine:x86','/debug','/incremental:no','/opt:ref',
                '/opt:icf','/opt:lldltojobs=1','/threads:1','/out:'+str(binary),'/pdb:'+str(output/('hud-'+name+'.pdb')),
                *[str(output/(obj+'.obj')) for obj in objects],*libraries]))
        jobs.append(dict(name='run-'+name,kind='run',seconds=90,expectExit=0,
            marker='hud_style_'+name+' checks=',command=[str(binary),*arguments]))
    shutil.copy2(previous/'run.py',out/'run.py')
    direct = list(plan['inputs'])+[row['frozen'] for row in sources]+generated_inputs+resource_inputs
    direct.extend(identity(path) for path in (overlay,out/'source-changes.json',out/'production-spans.json',
        out/'resource-inputs.json',out/'run.py',previous/'proposal.json',previous/'result.json',previous/'actual-includes.json'))
    revised = copy.deepcopy(plan)
    revised.update(inputs=list({key(row['path']):row for row in direct}.values()),overlay=identity(overlay),jobs=jobs,
        maxSeconds=900,output=str(output),compileFlags=flags,selectedGroup='hud-money',
        previousProposal=identity(previous/'proposal.json'),previousResult=identity(previous/'result.json'),
        resourceRoot=str(resources),resourceInputs=identity(out/'resource-inputs.json'),productionSpans=identity(out/'production-spans.json'),
        fullTranslationUnits=['client/hud_style.cpp','client/weapons/weapons.cpp','client/weapons/beamgun.cpp','client/weapons/weapon_inventory.cpp',
            'client/scenario/core/scenario_upgrades.cpp','scenario_upgrades.cpp','weapon_slots.cpp'],
        serverBoundary='Exact Load/PrecacheHudStyle spans with controlled engine callbacks; not full server DLL',
        reusedObjects=0,compiled=False,deployed=False)
    for name in ('reusedClient','newLocalization','preservedFailedParserAttempt','previousFailure',
                 'reusedObjectManifest','expectedOnlyProductionChange'):
        revised.pop(name,None)
    save(out/'proposal.json',revised)
    for row in sources:
        if identity(row['original']['path'])['sha256'] != row['original']['sha256']:
            raise ValueError('Source changed during preparation: '+row['relative'])
    print(json.dumps(dict(proposal=identity(out/'proposal.json'),independentSources=len(sources),
        jobs=len(jobs),compilerInvoked=False,reusedObjects=0,resources=len(actual)),indent=2))


if __name__ == '__main__':
    main()
