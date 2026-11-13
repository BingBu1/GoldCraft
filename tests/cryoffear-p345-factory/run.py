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
    a=p.parse_args();output=a.output.absolute()
    for path in (output,*output.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output path')
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace build directory')
    output.mkdir(parents=True)
    sources=[ROOT/'tests/cryoffear-p345-factory/main.cpp',ROOT/'native/csol/server/weapons/guest_cof_p345_factory.cpp',ROOT/'native/guest/cof/server/p345_weapon.cpp',
             ROOT/'native/guest/cof/magazine_actions.cpp',ROOT/'native/guest/cof/magazine_reload.cpp',
             ROOT/'native/guest/cof/spread.cpp',ROOT/'native/guest/cof/server/player_animation.cpp',
             ROOT/'native/guest/cof/player_actions.cpp']
    project=SDK/'msvc/ReGameDLL.vcxproj'
    model=ROOT/'sandbox/cs-server/Half-Life/cstrike/models/player/urban/urban.mdl'
    inputs=set(sources)|{project,model,a.environment.resolve(),Path(__file__).resolve()}
    original={'boundary':'No original oracle; real SDK GetClassPtr/OnCreate/CCSPlayerWeapon host adaptation'}
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
    receipt=dict(success=success,command=command,dependencyCommands=commands,dependencyBefore=before,dependencyAfter=after,
                 compilerSha256=compilerBefore,original=original,compiled=compiled.returncode,tested=tested.returncode if tested else None,
                 negative=negative,sourceCopies=0,pistol='p345',boundary='Actual GetClassPtr/OnCreate/CCSPlayerWeapon construction and real guest Precache; controlled engine private allocator and component edges; no registry/shop/transport/deployment/live acceptance')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print((tested.stdout+tested.stderr) if tested else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
