# SPDX-License-Identifier: GPL-3.0-or-later
"""Freeze the actual loader/collector/SDK and prepare a single-job native test."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import subprocess
import importlib.util

ROOT = Path(__file__).resolve().parents[2]

def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def identity(path):
    path=Path(path).resolve(strict=True)
    with path.open('rb') as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--candidate',type=Path,required=True)
    parser.add_argument('--baseline-candidate',type=Path,help='Candidate directory whose before loader is the original uncached baseline')
    parser.add_argument('--case',choices=['full','mixed'],default='full',help='Full regression or only mixed fingerprint-method regression')
    parser.add_argument('--compiler',type=Path,required=True)
    parser.add_argument('--linker',type=Path,required=True)
    parser.add_argument('--environment',type=Path,required=True)
    parser.add_argument('--common',type=Path)
    parser.add_argument('--reuse-collector',type=Path,help='Prior frozen plan with matching successful collector TU commands')
    args=parser.parse_args();out=args.output.resolve()
    assert out.is_relative_to(ROOT/'build') and not out.exists()
    out.mkdir(parents=True);frozen=out/'source';output=out/'output'
    manifest=read(args.candidate);expected={r['path']:r for r in manifest}
    inputs=[];seen=set()
    def copy(relative):
        relative=Path(relative).as_posix()
        if relative in seen:return
        seen.add(relative);source=ROOT/relative
        if relative in expected:
            source=args.candidate.resolve().parent/'after'/relative
            assert identity(source)['sha256']==expected[relative]['sha256']
        target=frozen/relative;target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(source.read_bytes());assert target.stat().st_nlink==1
        inputs.append(dict(relative=relative,original=identity(source),frozen=identity(target)))
    for relative in expected:copy(relative)
    copy('native/engine/precache.h')
    copy('tests/csol-dependency-cache/metadata.inc')
    baseline_candidate=(args.baseline_candidate or args.candidate).resolve()
    before=baseline_candidate.parent/'before/native/engine/csol_loader.cpp'
    before_expected=next(r for r in read(baseline_candidate.parent/'before.json') if r['path']=='native/engine/csol_loader.cpp')
    assert identity(before)['sha256']==before_expected['sha256']
    baseline=frozen/'native/engine/before_csol_loader.cpp';baseline.write_bytes(before.read_bytes())
    inputs.append(dict(relative='native/engine/before_csol_loader.cpp',original=identity(before),frozen=identity(baseline)))
    # Header-only SDK snapshot: no mutable source is linked to the root checkout.
    header_roots=['external/ReHLDS/rehlds','external/ReHLDS/dep/bzip2/include',
        'external/ReHLDS/dep/cppunitlite/include','external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include',
        'native/include','native/csol']
    for prefix in header_roots:
        for path in (ROOT/prefix).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('.h','.hpp','.inc','.inl'):copy(path.relative_to(ROOT))
    units=['tests/csol-dependency-cache/loader.cpp']+['native/csol/'+name+'.cpp' for name in
        ('dependencies','model_codec','sprite_codec','texture_model','model_json','events','model_source','sequences')]
    for unit in units:copy(unit)
    helper=ROOT/'tests/common/cpp_function.py'
    spec=importlib.util.spec_from_file_location('cpp_function',helper);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    source=frozen/'external/ReHLDS/rehlds/engine/sv_main.cpp';raw=source.read_bytes()
    start,end,body=module.function(raw,b'void PrecacheModelSpecifiedFiles()')
    extracted=frozen/'tests/csol-dependency-cache/batch_function.inc'
    extracted.write_bytes(body+b'\n')
    inputs.append(dict(relative='tests/csol-dependency-cache/batch_function.inc',original=identity(source),frozen=identity(extracted),start=start,end=end,bodySha256=hashlib.sha256(body).hexdigest()))
    env=read(args.environment);env.update(TEMP=str(output/'temp'),TMP=str(output/'temp'))
    (out/'environment.json').write_text(json.dumps(env,indent=2),encoding='utf-8')
    include=['native/engine','native/include','external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include']
    include+=['external/ReHLDS/rehlds/'+p for p in ('','metamod/include','public/rehlds','common','engine','public','pm_shared','rehlds','testsuite')]
    include+=['external/ReHLDS/dep/bzip2/include','external/ReHLDS/dep/cppunitlite/include']
    compiler=args.compiler.resolve();linker=args.linker.resolve()
    flags=['/nologo','/c','/TP','/std:c++20','/EHsc','/MT','/Z7','/utf-8','/Gy',
        '/arch:AVX2','/fp:precise','/clang:-O3','/clang:-flto=thin','/clang:-ffp-contract=off',
        '--target=i686-pc-windows-msvc','/showIncludes','/D_CRT_SECURE_NO_WARNINGS','/DNOMINMAX']
    flags+=['/D'+x for x in ('REHLDS_API','REHLDS_FIXES','REHLDS_SSE','REHLDS_OPT_PEDANTIC','REHLDS_SELF','REHLDS_CHECKS','HAVE_OPT_STRTOOLS','USE_BREAKPAD_HANDLER','DEDICATED','SWDS','_MBCS')]
    flags+=['/I'+str(frozen/p) for p in include]
    commands=[];objects=[];reused=[]
    previous=read(args.reuse_collector/'plan.json') if args.reuse_collector else None
    previous_result=read(args.reuse_collector/'output/result.json') if previous else None
    for unit in units:
        name=Path(unit).stem
        command=dict(name=name,seconds=180,argv=[str(compiler),*flags,str(frozen/unit),'/Fo'+str(output/(name+'.obj'))])
        if previous and name!='loader':
            old=next(c for c in previous['commands'] if c['name']==name)
            assert any(s['name']==name and s['exit']==0 for s in previous_result['steps'])
            normalized=[a.replace(str(args.reuse_collector.resolve()/'source'),str(frozen)).replace(str(args.reuse_collector.resolve()/'output'),str(output)) for a in old['argv']]
            assert normalized==command['argv'],name
            step=next(s for s in previous_result['steps'] if s['name']==name)
            old_log=Path(step['log']['path'])
            assert identity(old_log)==step['log']
            registry={os.path.normcase(r['path']):r for r in previous_result['actualIncludes']}
            actual_paths=set(re.findall(r'Note: including file:\s*(.+)',old_log.read_text(encoding='utf-8',errors='replace')))
            for include_path in actual_paths:
                row=registry[os.path.normcase(str(Path(include_path.strip()).resolve()))]
                p=Path(row['path'])
                if p.is_relative_to(args.reuse_collector.resolve()/'source'):
                    other=frozen/p.relative_to(args.reuse_collector.resolve()/'source')
                    assert identity(other)['sha256']==row['sha256'],str(other)
            oldsource=next(r['frozen'] for r in previous['inputs'] if r['relative']==unit)
            assert identity(frozen/unit)['sha256']==oldsource['sha256']
            obj=identity(args.reuse_collector/'output'/(name+'.obj'));reused.append(obj);objects.append(obj['path'])
        else:commands.append(command);objects.append(str(output/(name+'.obj')))
    commands.append(dict(name='link',seconds=180,argv=[str(linker),'/nologo','/machine:x86','/debug','/opt:ref','/opt:icf',
        '/out:'+str(output/'dependency_cache.exe'),'/pdb:'+str(output/'dependency_cache.pdb'),
        *objects,str(ROOT/'external/ReHLDS/rehlds/lib/libacof32.lib')]))
    commands.append(dict(name='synthetic',seconds=120,argv=[str(output/'dependency_cache.exe')]+(['--mixed-only'] if args.case=='mixed' else [])))
    if args.common and args.case=='full':commands.append(dict(name='common',seconds=180,argv=[str(output/'dependency_cache.exe'),'--common-only',str(args.common.resolve())]))
    # Snapshot system input hashes before compilation; actualIncludes must match this registry.
    system={}
    resource=subprocess.check_output([str(compiler),'-print-resource-dir'],text=True).strip()
    for folder in [*env['INCLUDE'].split(';'),str(Path(resource)/'include')]:
        if not folder:continue
        for path in Path(folder).rglob('*'):
            if path.is_file() and path.suffix.lower() in ('','.h','.hpp','.inc','.inl'):
                system[os.path.normcase(str(path.resolve()))]=identity(path)
    plan=dict(inputs=inputs,system=list(system.values()),candidate=identity(args.candidate),
        environment=identity(out/'environment.json'),tools=[identity(compiler),identity(linker)],
        libraries=[identity(ROOT/'external/ReHLDS/rehlds/lib/libacof32.lib'),*reused],commands=commands,
        extractionHelper=identity(helper),reusedCollectorObjects=len(reused),
        boundary='Actual SDK, complete loader and real collector with FS/console/CNG-failure boundary doubles; no DLL deployment, VFS provider or gameplay test.')
    (out/'plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(output=str(out),frozen=len(inputs),system=len(system),commands=len(commands))))

if __name__=='__main__':main()
