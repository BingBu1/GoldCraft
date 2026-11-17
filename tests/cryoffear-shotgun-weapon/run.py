# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual SDK 870 lifecycle, verifying frozen originals and real headers."""
from oracle import generate
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
    for name in ('sample','proof','spread-proof','compiler','environment','output'):p.add_argument('--'+name,type=Path,required=True)
    a=p.parse_args();output=a.output.absolute()
    gitBefore=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    for path in (output,*output.parents):
        if path.is_symlink() or os.path.isjunction(path):raise ValueError('Linked output path')
    if output.exists() or not output.resolve().is_relative_to(ROOT/'build'):raise ValueError('Use fresh workspace build directory')
    output.mkdir(parents=True)
    sources=[ROOT/'tests/cryoffear-shotgun-weapon/main.cpp',ROOT/'native/guest/cof/server/shotgun_weapon.cpp',
        ROOT/'native/guest/cof/shotgun_control.cpp',ROOT/'native/guest/cof/shotgun_shot.cpp',
        ROOT/'native/guest/cof/shotgun_aim.cpp',ROOT/'native/guest/cof/shotgun_frame.cpp',
        ROOT/'native/guest/cof/m16_control.cpp',ROOT/'native/guest/cof/server/player_animation.cpp',
        ROOT/'native/guest/cof/player_actions.cpp',ROOT/'native/guest/cof/spread.cpp']
    project=SDK/'msvc/ReGameDLL.vcxproj'
    model=ROOT/'sandbox/cs-server/Half-Life/cstrike/models/player/urban/urban.mdl'
    inputs=set(sources)|{project,model,a.environment.resolve(),Path(__file__).resolve(),a.proof.resolve(),a.sample.resolve()}
    inputs.add(Path(__file__).with_name('oracle.py'))
    inputs.add(ROOT/'tests/cryoffear-magazine-actions/run.py')
    proof=json.loads(a.proof.read_text('utf-8-sig'))
    sampleSha=sha(a.sample)
    if sampleSha!=proof['sampleSha256'] or not proof['beforeImplementation']:raise ValueError('Original/pre-edit mismatch')
    image=a.sample.read_bytes()
    def region_sha(va,length):
        nt=int.from_bytes(image[60:64],'little');opt=nt+24;base=int.from_bytes(image[opt+28:opt+32],'little')
        sections=opt+int.from_bytes(image[nt+20:nt+22],'little');rva=int(va,16)-base
        for i in range(int.from_bytes(image[nt+6:nt+8],'little')):
            at=sections+i*40;virtual=int.from_bytes(image[at+12:at+16],'little');size=int.from_bytes(image[at+16:at+20],'little')
            if virtual<=rva and rva+length<=virtual+size:
                offset=int.from_bytes(image[at+20:at+24],'little')+rva-virtual
                return hashlib.sha256(image[offset:offset+length]).hexdigest()
        raise ValueError('Original region is not file-backed')
    verified=[]
    def verify(value,directory):
        if value.get('sampleSha256',sampleSha)!=sampleSha or not value.get('beforeImplementation',True):
            raise ValueError('Inherited original mismatch')
        for group in ('regions','additionalRegions','newRegions','sharedInventory'):
            for region in value.get(group,[]):
                if region_sha(region['va'],region['size'])!=region['sha256']:raise ValueError('Original region mismatch: '+region['va'])
                verified.append(region)
        archives=value.get('archives',{})
        if isinstance(archives,list):archives={v['path']:v['sha256'] for v in archives}
        for rel,digest in archives.items():
            path=(directory/rel).resolve();inputs.add(path)
            if sha(path)!=digest:raise ValueError('Original archive mismatch: '+str(path))
    verify(proof,a.proof.parent)
    for inherited in proof['inherited']:
        path=(a.proof.parent/inherited['path']).resolve();inputs.add(path)
        if sha(path)!=inherited['sha256']:raise ValueError('Inherited pre-edit changed')
        verify(json.loads(path.read_text('utf-8-sig')),path.parent)
    inputs.add(a.spread_proof.resolve());spread_proof=json.loads(a.spread_proof.read_text('utf8'))
    verify(spread_proof,a.spread_proof.parent)
    shared=ROOT/proof['sharedProof'];inputs.add(shared)
    if sha(shared)!=proof['sharedProofSha256']:raise ValueError('Shared lifecycle proof changed')
    with (output/'cases.txt').open('w') as cases,(output/'expected.txt').open('w') as expected:
        replay=generate(a.sample,cases,expected)
    covered=[(int(v['va'],16),int(v['va'],16)+v['size']) for v in verified]
    if any(not any(start<=int(at,16)<end for start,end in covered) for at in replay['originalInstructions']):
        raise ValueError('Executed original instruction lacks frozen evidence')
    inputs.update((output/'cases.txt',output/'expected.txt'))
    original=dict(sampleSha256=sampleSha,verifiedRegions=len(verified),proofSha256=sha(a.proof),fidget=replay)
    spread_oracle=ROOT/'tests/cryoffear-spread/oracle.py';inputs.add(spread_oracle)
    spec=importlib.util.spec_from_file_location('original_spread',spread_oracle);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    with (output/'spread.txt').open('w') as rows:spread_replay=module.generate(a.sample,rows,shotgun_trace=True)
    if any(not any(start<=int(at,16)<end for start,end in covered)for at in spread_replay['originalInstructions']):raise ValueError('Executed spread instruction lacks frozen pre-edit evidence')
    original['spread']=spread_replay;original['spreadProofSha256']=sha(a.spread_proof);inputs.add(output/'spread.txt')
    scheduler=actionOwnership=None
    for name,path,sig,only in (
        ('sdk-sequence-info.inc',SDK/'dlls/animation.cpp','void GetSequenceInfo(',False),
        ('sdk-sequence-flags.inc',SDK/'dlls/animation.cpp','int GetSequenceFlags(',False),
        ('sdk-reset-sequence.inc',SDK/'dlls/animating.cpp','void EXT_FUNC CBaseAnimating::__API_HOOK(ResetSequenceInfo)()',True)):
        inputs.add(path);(output/name).write_text(body(path,sig,only),'utf-8')
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
    replaced=('void CBasePlayerWeapon::Spawn(','void CBasePlayerItem::AttachToPlayer(',
              'int CBasePlayerWeapon::AddToPlayer(','int CBasePlayerItem::AddToPlayer(',
              'int CBasePlayerWeapon::AddDuplicate(')
    aborts=[v for v in aborts if not v.startswith(replaced)]
    (output/'sdk-abort-virtuals.inc').write_text('\n'.join(aborts),'utf-8')
    includes=re.search(r'<AdditionalIncludeDirectories>(.*?)</AdditionalIncludeDirectories>',project.read_text('utf-8-sig'))[1].split(';')
    includes=[(project.parent/v[len('$(ProjectDir)'):].replace('\\','/').lstrip('/')).resolve() for v in includes if v.startswith('$(ProjectDir)')]
    defs='REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NDEBUG'.split()
    flags=['/nologo','--target=i686-pc-windows-msvc','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3',
           '/clang:-flto=thin','/clang:-fuse-ld=lld','/arch:AVX2','/fp:precise','/clang:-ffp-contract=off','-Wno-overriding-option','/Gy','/Gw',
           *('/D'+d for d in defs),*('/I'+str(v) for v in includes),'/I'+str(ROOT),'/I'+str(output)]
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
    exe=output/'shotgun.exe';command=[str(a.compiler),*flags,*map(str,sources),'/Fe:'+str(exe),'/link','/machine:x86','/threads:1','/opt:ref']
    compiled=subprocess.run(command,cwd=output,env=env,capture_output=True,text=True,timeout=120)
    (output/'compile.log').write_text(compiled.stdout+compiled.stderr,'utf-8');tested=None
    if not compiled.returncode:
        testArgs=[str(exe),str(model),str(output/'cases.txt'),str(output/'expected.txt'),str(output/'spread.txt')]
        if scheduler is not None:testArgs.extend((str(output/'cases.txt'),str(output/'expected.txt')))
        tested=subprocess.run(testArgs,cwd=output,capture_output=True,text=True,timeout=30)
        (output/'test.log').write_text(tested.stdout+tested.stderr,'utf-8')
    negative=[]
    if tested is not None and not tested.returncode:
        text=(output/'expected.txt').read_text();lines=text.splitlines(keepends=True)
        wrong=lines.copy();wrong[0]='999999\n'
        for name,value in (('empty',''),('truncated',''.join(lines[:-1])),('extra',text+'0\n'),('wrong',''.join(wrong))):
            path=output/('bad-'+name+'.txt');path.write_text(value)
            run=subprocess.run([str(exe),str(model),str(output/'cases.txt'),str(path),str(output/'spread.txt')],cwd=output,capture_output=True,text=True,timeout=30)
            (output/('bad-'+name+'.log')).write_text(run.stdout+run.stderr,'utf-8')
            negative.append(dict(name=name,exit=run.returncode,passed=run.returncode==2))
    after={str(v):sha(v) for v in sorted(deps)}
    success=not compiled.returncode and tested is not None and not tested.returncode and before==after and compilerBefore==sha(a.compiler) and all(v['passed'] for v in negative)
    gitAfter=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    receipt=dict(success=success,gitBefore=gitBefore,gitAfter=gitAfter,command=command,dependencyCommands=commands,dependencyBefore=before,dependencyAfter=after,
                 compilerSha256=compilerBefore,original=original,compiled=compiled.returncode,tested=tested.returncode if tested else None,
                 negative=negative,scheduler=scheduler,actionOwnership=actionOwnership,sourceCopies=0,boundary='Actual SDK lifecycle composition with controlled engine stage/ballistics/pushback/audio/transport; no registry wiring, deployment, gameplay or live performance acceptance')
    (output/'receipt.json').write_text(json.dumps(receipt,indent=2),'utf-8')
    print((tested.stdout+tested.stderr) if tested else compiled.stdout+compiled.stderr)
    raise SystemExit(0 if success else 1)
if __name__=='__main__':main()
