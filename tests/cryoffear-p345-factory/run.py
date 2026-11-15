# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual P345 GetClassPtr/CCSPlayerWeapon factory lifecycle."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[2]
SDK=ROOT/'external/ReGameDLL_CS/regamedll'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def body(path,signature,only=False):
    text=path.read_text('utf-8-sig');start=text.index(signature);at=text.index('{',start);end=at+1;depth=1
    while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[at+1:end-1] if only else text[start:end]

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('compiler','environment','output'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--weapon',choices=('p345','revolver','vp70','tmp','mp5','m16'),default='p345',help='Actual native factory/subclass to exercise')
    p.add_argument('--sample',type=Path,help='Required original CoF MP for M16 pre-edit proof')
    a=p.parse_args();output=a.output.absolute()
    gitBefore=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    for path in (output,*output.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output path')
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace build directory')
    output.mkdir(parents=True)
    sources=[ROOT/('tests/cryoffear-'+a.weapon+'-factory/main.cpp'),ROOT/('native/csol/server/weapons/guest_cof_'+a.weapon+'_factory.cpp'),ROOT/('native/guest/cof/server/'+a.weapon+'_weapon.cpp'),
             ROOT/'native/guest/cof/magazine_actions.cpp',ROOT/'native/guest/cof/magazine_reload.cpp',
             ROOT/'native/guest/cof/spread.cpp',ROOT/'native/guest/cof/server/player_animation.cpp',
             ROOT/'native/guest/cof/player_actions.cpp']
    if a.weapon in ('tmp','mp5'):sources.append(ROOT/'native/guest/cof/automatic_sound.cpp')
    if a.weapon=='mp5':
        sources.extend((ROOT/'native/guest/cof/server/tmp_weapon.cpp',
                        ROOT/'native/guest/cof/mp5_primary.cpp',
                        ROOT/'native/csol/server/weapons/guest_cof_tmp_factory.cpp'))
    if a.weapon=='m16':
        sources.extend((ROOT/'native/guest/cof/m16_control.cpp',ROOT/'native/guest/cof/m16_shot.cpp'))
    project=SDK/'msvc/ReGameDLL.vcxproj'
    model=ROOT/'sandbox/cs-server/Half-Life/cstrike/models/player/urban/urban.mdl'
    inputs=set(sources)|{project,model,a.environment.resolve(),Path(__file__).resolve()}
    original={'boundary':'No original oracle; real SDK GetClassPtr/OnCreate/CCSPlayerWeapon host adaptation'}
    if a.weapon=='m16':
        if a.sample is None:p.error('--weapon m16 requires --sample')
        proof=ROOT/'analysis/cof-weapons-20261111/m16-integration-20261115/PREEDIT-factory.json'
        value=json.loads(proof.read_text('utf8'));data=a.sample.read_bytes()
        if not value['beforeImplementation'] or sha(a.sample)!=value['sampleSha256']:raise ValueError('M16 original factory identity differs')
        nt=int.from_bytes(data[60:64],'little');opt=nt+24
        base=int.from_bytes(data[opt+28:opt+32],'little');section=opt+int.from_bytes(data[nt+20:nt+22],'little')
        for row in value['regions']:
            rva=int(row['va'],16)-base;found=False
            for i in range(int.from_bytes(data[nt+6:nt+8],'little')):
                at=section+i*40;va=int.from_bytes(data[at+12:at+16],'little');size=int.from_bytes(data[at+16:at+20],'little');raw=int.from_bytes(data[at+20:at+24],'little')
                if va<=rva and rva+row['size']<=va+size:
                    start=raw+rva-va;found=hashlib.sha256(data[start:start+row['size']]).hexdigest()==row['sha256'];break
            if not found:raise ValueError('Original factory region differs: '+row['va'])
        inputs.update((proof,a.sample.resolve()))
        for name,digest in value['archives'].items():
            path=proof.parent/name
            if sha(path)!=digest:raise ValueError('Original factory archive differs')
            inputs.add(path)
        original.update(sampleSha256=value['sampleSha256'],proofSha256=sha(proof),verifiedRegions=len(value['regions']))

    for name,path,sig,only in (
        ('sdk-sequence-info.inc',SDK/'dlls/animation.cpp','void GetSequenceInfo(',False),
        ('sdk-sequence-flags.inc',SDK/'dlls/animation.cpp','int GetSequenceFlags(',False),
        ('sdk-reset-sequence.inc',SDK/'dlls/animating.cpp','void EXT_FUNC CBaseAnimating::__API_HOOK(ResetSequenceInfo)()',True)):
        inputs.add(path);(output/name).write_text(body(path,sig,only),'utf-8')
    create=SDK/'dlls/cbase.cpp';inputs.add(create)
    (output/'sdk-on-create.inc').write_text(body(create,'void CBaseEntity::OnCreate()'),'utf-8')
    lifecycle=[]
    for path,sig in (
        (SDK/'dlls/weapons.cpp','void CBasePlayerWeapon::Spawn()'),
        (SDK/'dlls/weapons.cpp','void CBasePlayerItem::AttachToPlayer('),
        (SDK/'dlls/weapons.cpp','int CBasePlayerWeapon::AddToPlayer('),
        (SDK/'dlls/weapons.cpp','int CBasePlayerItem::AddToPlayer('),
        (SDK/'dlls/weapons.cpp','int CBasePlayerWeapon::AddDuplicate('),
        (SDK/'dlls/player.cpp','int CBasePlayer::GetAmmoIndex('),
        (SDK/'dlls/player.cpp','bool CBasePlayer::HasPlayerItem('),
        (SDK/'dlls/weapons.cpp','const char *CBasePlayerItem::pszAmmo1('),
        (SDK/'dlls/weapons.cpp','const char *CBasePlayerItem::pszAmmo2('),
        (SDK/'dlls/API/CSPlayerItem.cpp','EXT_FUNC void CCSPlayerItem::SetItemInfo('),
        (SDK/'dlls/API/CSPlayerItem.cpp','EXT_FUNC int CCSPlayerItem::GetItemInfo('),
        (SDK/'dlls/util.cpp','void UTIL_SetOrigin('),
        (SDK/'dlls/util.cpp','void UTIL_SetSize(')):
        inputs.add(path);lifecycle.append(body(path,sig).replace('m_ItemInfoEx','CSPlayerItem()->m_ItemInfo'))
    (output/'sdk-world-lifecycle.inc').write_text('\n'.join(lifecycle),'utf-8')
    aborts=[]
    for header,classes in (('cbase.h',('CBaseEntity','CBaseDelay','CBaseAnimating','CBaseToggle')),
                           ('basemonster.h',('CBaseMonster',)),('player.h',('CBasePlayer',)),
                           ('weapons.h',('CBasePlayerItem','CBasePlayerWeapon'))):
        path=SDK/'dlls'/header;inputs.add(path);text=path.read_text('utf-8-sig')
        for cls in classes:
            match=re.search(r'class '+cls+r'\s*(?::[^{};]+)?\s*{',text);at=match.end();end=at;depth=1
            while depth:depth+=(text[end]=='{')-(text[end]=='}');end+=1
            for sig in re.findall(r'virtual\s+([^;{}\n]+);',text[at:end-1]):
                sig=re.sub(r'\s*=\s*[^,)]+','',sig);sig=re.sub(r'\bEXPORT\b','',sig).strip()
                m=re.fullmatch(r'(.*?)([A-Za-z_][A-Za-z_0-9]*)(\s*\(.*)',sig)
                if not m:raise ValueError('SDK declaration not parsed: '+sig)
                aborts.append(m[1]+cls+'::'+m[2]+m[3]+' { std::abort(); }')
    replaced=('void CBaseEntity::OnCreate(', 'void CBasePlayerWeapon::Spawn(','void CBasePlayerItem::AttachToPlayer(',
              'int CBasePlayerWeapon::AddToPlayer(','int CBasePlayerItem::AddToPlayer(',
              'int CBasePlayerWeapon::AddDuplicate(')
    aborts=[v for v in aborts if not v.startswith(replaced)]
    (output/'sdk-abort-virtuals.inc').write_text('\n'.join(aborts),'utf-8')
    includes=re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',project.read_text('utf-8-sig'))[1].split(';')
    includes=[(project.parent/v[len('$(ProjectDir)'):].replace('\\','/').lstrip('/')).resolve() for v in includes if v.startswith('$(ProjectDir)')]
    defs='REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3',
           '/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','/Gy','/Gw',
           *('/D'+d for d in defs),*('/I'+str(v) for v in includes),'/I'+str(ROOT),'/I'+str(output),
           '/I'+str(ROOT/'native/include'),
           '/I'+str(ROOT/'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
    env=os.environ|json.loads(a.environment.read_text('utf-8-sig'));temp=output/'temp';temp.mkdir();env.update(TEMP=str(temp),TMP=str(temp))
    deps=set(inputs);commands=[]
    for i,source in enumerate(sources):
        dep=output/f'{i}.d';command=[str(a.compiler),*flags,'/clang:-M','/clang:-MF','/clang:'+str(dep),str(source)]
        commands.append(command);run=subprocess.run(command,cwd=output,env=env,capture_output=True,text=True,timeout=60)
        (output/f'{i}-headers.log').write_text(run.stdout+run.stderr,'utf-8')
        if run.returncode:raise RuntimeError('SDK dependency discovery failed')
        make=dep.read_text().replace(chr(92)+chr(10),' ')
        deps.update(Path(v.replace(chr(92)+' ',' ')).resolve() for v in re.findall(r'(?:\\ |[^\s])+',make.split(':',1)[1]))
    deps.update(output.glob('*.inc'))
    before={str(v):sha(v) for v in sorted(deps)};compilerBefore=sha(a.compiler)
    exe=output/'factory.exe';command=[str(a.compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref']
    compiled=subprocess.run(command,cwd=output,env=env,capture_output=True,text=True,timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8');tested=None
    if not compiled.returncode:
        tested=subprocess.run([str(exe),str(model)],cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    negative=[]
    after={str(v):sha(v) for v in sorted(deps)}
    success=not compiled.returncode and tested is not None and not tested.returncode and before==after and compilerBefore==sha(a.compiler) and all(v['passed'] for v in negative)
    receipt=dict(success=success,gitBefore=gitBefore,gitAfter=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),command=command,dependencyCommands=commands,dependencyBefore=before,dependencyAfter=after,
                 compilerSha256=compilerBefore,original=original,compiled=compiled.returncode,tested=tested.returncode if tested else None,
                 negative=negative,sourceCopies=0,pistol=a.weapon,boundary='Actual GetClassPtr/OnCreate/CCSPlayerWeapon construction and real guest Precache; controlled engine private allocator and component edges; no registry/shop/transport/deployment/live acceptance')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print((tested.stdout+tested.stderr) if tested else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
