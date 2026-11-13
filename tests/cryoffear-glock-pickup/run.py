#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile native inventory/box packing with the actual CoF weapon subclass."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT/'tests/cryoffear-glock-weapon'
SDK=ROOT/'external/ReGameDLL_CS/regamedll'
sys.path.insert(0,str(BASE))
spec=importlib.util.spec_from_file_location('glock_sdk',BASE/'run.py')
helper=importlib.util.module_from_spec(spec);spec.loader.exec_module(helper)

def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('sample','compiler','environment','output'):
        parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--full-touch',action='store_true',help='Execute the complete native box Touch body with controlled rules and engine edges')
    args=parser.parse_args();out=args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT/'build') or any(p.is_symlink() or p.is_junction() for p in (out,*out.parents)):
        raise ValueError('Fresh output inside workspace build required')
    prep=[sys.executable,str(BASE/'run.py'),'--sample',str(args.sample),'--compiler',str(args.compiler),
          '--environment',str(args.environment),'--output',str(out),'--prepare-only']
    prepared=subprocess.run(prep,cwd=ROOT,capture_output=True,text=True,check=True)
    (out/'prepare.log').write_text(prepared.stdout+prepared.stderr,'utf8')
    plan=json.loads((out/'sdk-build-plan.json').read_text())
    if any(sha(Path(path))!=expected for path,expected in plan['inputs'].items()):
        raise ValueError('SDK preparation inputs changed before pickup adapter generation')
    compiler_before=sha(args.compiler)
    if compiler_before!=plan['compilerSha256']:
        raise ValueError('Compiler changed after SDK preparation')
    prefix=(BASE/'main.cpp').read_text('utf-8-sig').split('int main(int argc,char** argv)',1)[0]
    (out/'shared-glock-sdk.inc').write_text(prefix,'utf8')
    actual=[];controls={Path(__file__).resolve(),ROOT/'tests/cryoffear-glock-pickup/main.cpp',BASE/'main.cpp',BASE/'run.py'}
    for path,signature,renamed in (
        (SDK/'dlls/player.cpp','BOOL EXT_FUNC CBasePlayer::__API_HOOK(AddPlayerItem)(', 'BOOL CBasePlayer::AddPlayerItem(CBasePlayerItem* pItem)'),
        (SDK/'dlls/player.cpp','BOOL EXT_FUNC CBasePlayer::__API_HOOK(RemovePlayerItem)(', 'BOOL CBasePlayer::RemovePlayerItem(CBasePlayerItem* pItem)')):
        actual.append(renamed+'{'+helper.body(path,signature,True)+'}');controls.add(path)
    for path,signature in (
        (SDK/'dlls/player.cpp','bool CBasePlayer::HasShield('),
        (SDK/'dlls/weapons.cpp','void CBasePlayerItem::CheckRespawn('),
        (SDK/'dlls/weapons.cpp','BOOL CWeaponBox::PackWeapon('),
        (SDK/'dlls/weapons.cpp','BOOL CWeaponBox::HasWeapon(')):
        actual.append(helper.body(path,signature));controls.add(path)
    if args.full_touch:
        actual.append(helper.body(SDK/'dlls/weapons.cpp','void CWeaponBox::Touch('))
        sound=SDK/'dlls/sound.cpp';controls.add(sound)
        actual.append(helper.body(sound,'void EMIT_SOUND_DYN('))
    (out/'pickup-native.inc').write_text('\n'.join(actual),'utf8')
    aborts=(out/'sdk-abort-virtuals.inc').read_text()
    replaced=('BOOL CBasePlayer::AddPlayerItem(','BOOL CBasePlayer::RemovePlayerItem(',
              'void CBasePlayer::ResetMaxSpeed(')
    aborts='\n'.join(line for line in aborts.splitlines() if not line.startswith(replaced))
    weapons=(SDK/'dlls/weapons.h').read_text('utf-8-sig');controls.add(SDK/'dlls/weapons.h')
    start=weapons.index('class CWeaponBox:');end=weapons.index('\n};',start)
    for sig in re.findall(r'virtual\s+([^;{}\n]+);',weapons[start:end]):
        sig=re.sub(r'\s*=\s*[^,)]+','',sig).strip()
        m=re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)',sig)
        if not m:raise ValueError('Unparsed weaponbox virtual: '+sig)
        if args.full_touch and m[2]=='Touch':continue
        aborts+='\n'+m[1]+'CWeaponBox::'+m[2]+m[3]+' { std::abort(); }'
    (out/'sdk-abort-virtuals.inc').write_text(aborts,'utf8')
    rules=SDK/'dlls/gamerules.h';text=rules.read_text('utf-8-sig');controls.add(rules)
    start=text.index('class CGameRules\n');end=text.index('\n};',start)
    definitions=['CGameRules::CGameRules() {}','CGameRules::~CGameRules() {}']
    derived=['class PickupRules final:public '+('CHalfLifeMultiplay' if args.full_touch else 'CGameRules')+' { public:']
    for sig in re.findall(r'virtual\s+([^;{}\n]+);',text[start:end]):
        if '~CGameRules' in sig:continue
        sig=re.sub(r'\s*=\s*[^,)]+','',sig).strip()
        m=re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)',sig)
        if not m:raise ValueError('Unparsed rules virtual: '+sig)
        operation={'PlayerGotWeapon':'++pickup::received;','WeaponShouldRespawn':'++pickup::respawns;return GR_WEAPON_RESPAWN_NO;',
                   'FShouldSwitchWeapon':'++pickup::switches;return FALSE;'}.get(m[2],'std::abort();')
        derived.append(sig+' override { '+operation+' }')
        definitions.append(m[1]+'CGameRules::'+m[2]+m[3]+' { std::abort(); }')
    if args.full_touch:
        definitions.append('CHalfLifeMultiplay::CHalfLifeMultiplay() {}')
        start=text.index('class CHalfLifeMultiplay:');end=text.index('\n};',start)
        for sig in re.findall(r'(?:virtual|VFUNC)\s+([^;{}\n]+);',text[start:end]):
            sig=re.sub(r'\s*=\s*[^,)]+','',sig).strip()
            m=re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)',sig)
            if not m:raise ValueError('Unparsed multiplayer rule virtual: '+sig)
            definitions.append(m[1]+'CHalfLifeMultiplay::'+m[2]+m[3]+' { std::abort(); }')
        api_player=SDK/'public/regamedll/API/CSPlayer.h';controls.add(api_player)
        api_text=api_player.read_text('utf-8-sig')
        for sig in re.findall(r'virtual\s+([^;{}\n]+);',api_text):
            sig=re.sub(r'\s*=\s*[^,)]+','',sig).strip()
            m=re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)',sig)
            if not m:raise ValueError('Unparsed player API virtual: '+sig)
            definitions.append(m[1]+'CCSPlayer::'+m[2]+m[3]+' { std::abort(); }')
    (out/'pickup-rules.inc').write_text('\n'.join(definitions+derived+['};']),'utf8')
    command=[str(ROOT/'tests/cryoffear-glock-pickup/main.cpp') if value.replace('\\','/').endswith('/tests/cryoffear-glock-weapon/main.cpp') else
             '/Fe:'+str(out/'pickup.exe') if value.startswith('/Fe:') else value for value in plan['command']]
    if args.full_touch:
        command.insert(1,'/DCOF_FULL_TOUCH=1')
        command.append('/errorlimit:0')
    inputs={Path(p) for p in plan['inputs']}|controls|set(out.glob('*.inc'))
    before={str(p):sha(p) for p in sorted(inputs)}
    env=os.environ|json.loads(args.environment.read_text('utf-8-sig'));env.update(TEMP=str(out/'temp'),TMP=str(out/'temp'))
    build=subprocess.run(command,cwd=out,env=env,capture_output=True,text=True,timeout=120)
    (out/'compile.log').write_text(build.stdout+build.stderr,'utf8')
    tested=None
    if not build.returncode:
        tested=subprocess.run([str(out/'pickup.exe'),plan['model']],cwd=ROOT,capture_output=True,text=True,timeout=30)
        (out/'test.log').write_text(tested.stdout+tested.stderr,'utf8')
    after={str(p):sha(p) for p in sorted(inputs)}
    compiler_after=sha(args.compiler)
    result=dict(success=build.returncode==0 and tested is not None and tested.returncode==0 and before==after and compiler_before==compiler_after,
                command=command,compiled=build.returncode,tested=tested.returncode if tested else None,
                before=before,after=after,inputsUnchanged=before==after,sourceCopies=0,
                compilerSha256=compiler_before,compilerUnchanged=compiler_before==compiler_after,
                fullTouch=args.full_touch,
                boundary='Actual native inventory Add/Remove and CWeaponBox Pack/Has'+('/Touch' if args.full_touch else '')+', real guest class; game-rule policy/engine effects are explicit doubles; no physics/hook/network/gameplay/performance acceptance')
    (out/'receipt.json').write_text(json.dumps(result,indent=2)+'\n','utf8')
    print((tested.stdout+tested.stderr) if tested else build.stdout+build.stderr)
    raise SystemExit(0 if result['success'] else 1)

if __name__=='__main__':main()
