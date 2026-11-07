#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Run complete client/server BALROG11 TUs with verified SDK object baselines."""
import argparse, hashlib, json, os, re, subprocess, time
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
def read(p):return json.loads(Path(p).read_text('utf-8-sig'))
def ident(p):
    p=Path(p).resolve();data=p.read_bytes();return dict(path=str(p),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def write(p,v):Path(p).write_text(json.dumps(v,ensure_ascii=False,indent=2),encoding='utf-8')
def key(p):return os.path.normcase(os.path.abspath(p))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    for name in ['sdk','native-client','output']:ap.add_argument('--'+name,type=Path,required=True)
    ap.add_argument('--old-ref',required=True);ap.add_argument('--reuse-client',type=Path);args=ap.parse_args()
    out=args.output.resolve();assert out.is_relative_to(ROOT/'build') and not out.exists();out.mkdir()
    sdk=read(args.sdk/'prepared.json');native=read(args.native_client/'proposal.json')
    assert read(args.sdk/'result.json')['success'] and read(args.native_client/'result.json')['success']
    known={};sources=[];steps=[];excluded=[]
    server_template=sdk['jobs'][0]['command']
    old_overlay=Path(server_template[server_template.index('/clang:-ivfsoverlay')+1][len('/clang:'):])
    aliases={key(x['name']):Path(x['external-contents']) for x in read(old_overlay)['roots']}
    def keep(p):row=ident(p);known[key(p)]=row;return row
    def validate(row):
        # Verify actual compiler reads below. Historical dependency inventories
        # also contain unrelated mutable sources that these TUs do not read.
        known.setdefault(key(row['path']),row)
    for row in sdk['inputs']:validate(row)
    for row in native['inputs']:validate(row)
    for p in [args.sdk/'prepared.json',args.sdk/'result.json',args.native_client/'proposal.json',args.native_client/'result.json',Path(__file__)]:keep(p)
    def freeze(relative,folder='source',old=False):
        source=ROOT/relative
        data=subprocess.check_output(['git','show',args.old_ref+':'+relative],cwd=ROOT) if old else source.read_bytes()
        destination=out/folder/relative;destination.parent.mkdir(parents=True,exist_ok=True);destination.write_bytes(data);keep(destination)
        sources.append(dict(relative=relative,original=ident(source) if not old else dict(gitRef=args.old_ref),frozen=ident(destination)))
        return destination
    env=os.environ|sdk['environment'];env['TMP']=env['TEMP']=str(out/'temp');(out/'temp').mkdir()
    def execute(name,command,expected=0):
        keep(command[0]);started=time.monotonic()
        if 'link' in name:
            rsp=out/(name+'.rsp');rsp.write_text(subprocess.list2cmdline(command[1:]),encoding='utf-8');command=[command[0],'@'+str(rsp)]
        process=subprocess.run(command,cwd=ROOT,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=240)
        log=out/(name+'.log');log.write_bytes(process.stdout)
        includes=sorted(set(x.strip() for x in re.findall(r'Note: including file:\s*(.+)',process.stdout.decode('utf-8','replace'))))
        unknown=[p for p in includes if key(p) not in known]
        changed=[p for p in includes if key(p) in known and ident(p)['sha256']!=known[key(p)]['sha256']]
        for p in includes:
            if p not in changed:keep(p)
        row=dict(name=name,exit=process.returncode,expected=expected,seconds=time.monotonic()-started,log=ident(log),includes=includes,unknown=unknown,changed=changed)
        steps.append(row);write(out/'steps.json',steps);print(name,process.returncode,'unknown',len(unknown),'changed',len(changed),flush=True)
        if process.returncode!=expected or unknown or changed:
            print(process.stdout.decode('utf8','replace')[-6000:]);raise RuntimeError(name+' failed')
    # Client fixture executes the unchanged host random function adapter.
    pending=['tests/csol-skull-balrog/native_client.cpp','native/csol/client/shared_random.cpp','native/csol/client/weapons/weapon_module.cpp']
    seen=set()
    while pending:
        relative=pending.pop()
        if relative in seen:continue
        seen.add(relative);p=freeze(relative,'client-source')
        for token in re.findall(r'^\s*#include\s+"([^"]+)"',p.read_text('utf-8-sig'),re.M):
            child=(ROOT/Path(relative).parent/token).resolve()
            if child.is_file() and child.is_relative_to(ROOT) and not child.is_relative_to(ROOT/'external'):
                pending.append(child.relative_to(ROOT).as_posix())
    for p in (args.native_client/'generated').iterdir():keep(p)
    client_template=next(x['argv'] for x in native['commands'] if x['name']=='fixture')
    def client_compile(label,relative,old=False):
        command=[]
        for arg in client_template:
            if arg.startswith('/Fo'):arg='/Fo'+str(out/(label+'.obj'))
            elif arg.startswith('/Fd'):arg='/Fd'+str(out/(label+'.pdb'))
            elif arg==client_template[-1]:arg=str(out/'client-source'/relative)
            command.append(arg)
        if old:
            original=ROOT/'native/csol/client/weapons/balrog11.cpp';frozen=out/'client-source/native/csol/client/weapons/balrog11.cpp'
            previous=freeze('native/csol/client/weapons/balrog11.cpp','old',True)
            overlay=dict(version=0,**{'case-sensitive':False,'use-external-names':False},roots=[
                dict(type='file',name=str(p),**{'external-contents':str(previous)}) for p in [original,frozen]])
            ov=out/'client-old-overlay.json';write(ov,overlay);keep(ov)
            at=command.index('--');command[at:at]=['/clang:-ivfsoverlay','/clang:'+str(ov)]
        execute('compile-'+label,command)
    if not args.reuse_client:
        client_compile('adapter','native/csol/client/shared_random.cpp')
        client_compile('registry','native/csol/client/weapons/weapon_module.cpp')
    native_test=next(x['argv'] for x in native['commands'] if x['name']=='test')
    for old in [False,True]:
        if args.reuse_client:continue
        label='client-old' if old else 'client-new';client_compile(label,'tests/csol-skull-balrog/native_client.cpp',old)
        command=[client_template[0],'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(out/(label+'.obj')),str(out/'adapter.obj'),str(out/'registry.obj'),'/Fe'+str(out/(label+'.exe')),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']
        execute('link-'+label,command);execute('run-'+label,[str(out/(label+'.exe')),*native_test[1:]],1 if old else 0)
    reused_client=[]
    if args.reuse_client:
        for relative in seen:
            assert ident(out/'client-source'/relative)['sha256']==ident(args.reuse_client/'client-source'/relative)['sha256']
        reused_client=[s for s in read(args.reuse_client/'steps.json') if 'server-' not in s['name']]
        assert len(reused_client)==8 and all(s['exit']==s['expected'] and not s['unknown'] and not s['changed'] for s in reused_client)
        for s in reused_client:
            assert ident(s['log']['path'])['sha256']==s['log']['sha256'];keep(s['log']['path'])
        keep(args.reuse_client/'steps.json')
    # Complete server weapon TU is included in the SDK fixture; replace its old object.
    fixture='tests/csol-skull-balrog/server_events.cpp';frozen_fixture=freeze(fixture)
    weapon='native/csol/server/weapons/wpn_balrog11.cpp';frozen_weapon=freeze(weapon)
    for old in [False,True]:
        label='server-old' if old else 'server-new';overlay=read(old_overlay)
        selected=freeze(weapon,'old',True) if old else frozen_weapon
        for row in overlay['roots']:
            if row['name'].replace('\\','/').lower().endswith('/'+weapon):row['external-contents']=str(selected)
        for row in list(overlay['roots']):
            logical=Path(row['name'])
            if logical.is_relative_to(ROOT) and not logical.is_relative_to(ROOT/'build'):
                overlay['roots'].append(dict(row,name=str(out/'source'/logical.relative_to(ROOT))))
        overlay['roots'] += [dict(type='file',name=str(p),**{'external-contents':str(v)}) for p,v in [(ROOT/weapon,selected),(out/'source'/weapon,selected),(ROOT/fixture,frozen_fixture)]]
        ov=out/(label+'-overlay.json');write(ov,overlay);keep(ov)
        command=[]
        for arg in server_template:
            if arg.startswith('/clang:') and arg.endswith('overlay.json'):arg='/clang:'+str(ov)
            elif arg.startswith('/Fo'):arg='/Fo'+str(out/(label+'.obj'))
            elif arg==server_template[-2]:arg=str(frozen_fixture)
            command.append(arg)
        execute('compile-'+label,command)
        link=[]
        for arg in sdk['jobs'][-1]['command']:
            if arg.lower().endswith('.obj') and ('wpn_balrog11_' in arg or Path(arg).name=='lifecycle_sdk.obj'):continue
            if arg.upper().startswith('/OUT:'):arg='/OUT:'+str(out/(label+'.exe'))
            elif arg.upper().startswith('/PDB:'):arg='/PDB:'+str(out/(label+'.pdb'))
            if arg.endswith(('.obj','.lib')) and Path(arg).is_file():
                expected=known.get(key(arg));actual_object=ident(arg)
                if expected:assert actual_object['sha256']==expected['sha256'],arg
                keep(arg)
            link.append(arg)
        link.extend([str(out/(label+'.obj')),'/lldltocache:'+str(out.parent/'balrog-sdk-lto-cache')]);execute('link-'+label,link)
        for group in ['events','lifecycle']:execute('run-'+label+'-'+group,[str(out/(label+'.exe')),group],1 if old else 0)
    actual={key(p) for s in steps for p in s['includes']}
    actual.update(key(s['frozen']['path']) for s in sources)
    actual.update(key(p) for p in link if p.endswith(('.obj','.lib')) and Path(p).is_file())
    changed=[known[k]['path'] for k in actual if k in known and ident(known[k]['path'])['sha256']!=known[k]['sha256']]
    write(out/'result.json',dict(success=not changed,steps=steps,reusedClientSteps=reused_client,sources=sources,inputs=[known[k] for k in actual if k in known],changed=changed,
        deployed=False,liveGameplay=False,boundary='Actual client prediction/event and complete server weapon TU with real SDK objects; engine movement clipping supplied at engine boundary. No live gameplay claim.'))
    assert not changed

if __name__=='__main__':main()
