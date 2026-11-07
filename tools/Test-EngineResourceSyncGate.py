#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile real resource-sync and extracted wire/map-finalization paths, gate OFF/ON."""
from pathlib import Path
import argparse, hashlib, json, os, subprocess, time, shutil
ROOT=Path(__file__).resolve().parents[1]
def identity(path):
    path=Path(path).resolve(strict=True);data=path.read_bytes()
    return dict(path=str(path),bytes=len(data),sha256=hashlib.sha256(data).hexdigest())
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',default='clang-cl.exe')
    parser.add_argument('--environment-json',type=Path)
    parser.add_argument('--out',type=Path,default=ROOT/'build/engine-resource-sync-gate')
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--approval-only',action='store_true',help='check the optional Steam observation API gate instead')
    args=parser.parse_args();out=args.out.resolve()
    if not out.is_relative_to(ROOT/'build'):raise ValueError('Output must stay under workspace build')
    out.mkdir(parents=True,exist_ok=True)
    compiler=Path(shutil.which(args.compiler) or args.compiler).resolve(strict=True)
    env=dict(os.environ)
    if args.environment_json:env.update(json.loads(args.environment_json.read_text(encoding='utf-8-sig')))
    env['VSLANG']='1033'
    sources=[ROOT/'tests/engine-resource-sync-gate/fixture.cpp',ROOT/'native/engine/http_download_server.cpp',ROOT/'native/engine/launcher_http.cpp']
    sync=ROOT/'native/engine/resource_sync.cpp';sv=ROOT/'external/ReHLDS/rehlds/engine/sv_main.cpp';wire=ROOT/'native/engine/precache.cpp'
    raw=sv.read_bytes();start=raw.index(b'    if (GC_ResourceSyncEnabled())');end=raw.index(b'    GC_HttpResources();',start)+len(b'    GC_HttpResources();')
    (out/'map-list-tail.inc').write_bytes(b'static void CompleteResourceList() {\n'+raw[start:end]+b'\n}\n')
    (out/'resource-sync-under-test.inc').write_text(f'#include "{sync.as_posix()}"\n')
    data=wire.read_bytes();ws=data.index(b'bool GC_WriteResourceChunk(sizebuf_t* msg) {');we=data.index(b'void GC_PrecacheInit()',ws)
    (out/'resource_sync_wire_adapter.inc').write_bytes(data[ws:we])
    sdk=ROOT/'external/ReHLDS/rehlds'
    dirs=[out,ROOT/'native/engine',ROOT/'native/include']+[sdk/x for x in ('','metamod/include','public/rehlds','common','engine','public','pm_shared','rehlds','testsuite')]+[sdk.parent/'dep/bzip2/include',sdk.parent/'dep/cppunitlite/include']
    defines=['REHLDS_API','REHLDS_FIXES','REHLDS_SSE','REHLDS_OPT_PEDANTIC','REHLDS_SELF','REHLDS_CHECKS','HAVE_OPT_STRTOOLS','USE_BREAKPAD_HANDLER','DEDICATED','SWDS','NDEBUG','NOMINMAX','_CRT_SECURE_NO_WARNINGS']
    flags=['--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/Gy','/Gw','/fp:precise','/Y-','/showIncludes']
    includes=[f'/I{p}' for p in dirs];sdk_flags=[f'/D{d}' for d in defines]
    commands=[]
    for enabled in (0,1):
        objs=[]
        for i,src in enumerate(sources):
            obj=out/f'gate-{enabled}-{i}.obj';objs.append(str(obj))
            commands.append(dict(name=f'compile-{enabled}-{i}',argv=[str(compiler),*flags,*includes,*sdk_flags,f'/DGOLDCRAFT_RESOURCE_SYNC_V4={enabled}','/c',str(src),f'/Fo{obj}']))
        exe=out/f'gate-{enabled}.exe'
        commands.append(dict(name=f'link-{enabled}',argv=[str(compiler),'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objs,f'/Fe{exe}','/link','ws2_32.lib','bcrypt.lib','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1']))
        commands.append(dict(name=f'test-{enabled}',argv=[str(exe)]))
        src=ROOT/'tests/engine-resource-sync-gate/wire.cpp';obj=out/f'wire-{enabled}.obj';exe=out/f'wire-{enabled}.exe'
        commands.append(dict(name=f'compile-wire-{enabled}',argv=[str(compiler),*flags,*includes,f'/DGOLDCRAFT_RESOURCE_SYNC_V4={enabled}','/c',str(src),f'/Fo{obj}']))
        commands.append(dict(name=f'link-wire-{enabled}',argv=[str(compiler),'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(obj),f'/Fe{exe}','/link','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1']))
        commands.append(dict(name=f'test-wire-{enabled}',argv=[str(exe)]))
    if args.approval_only:
        commands=[]
        approval_sources=[ROOT/'tests/csol-steam-approval/gate.cpp',ROOT/'native/engine/steam_approval_registry.cpp']
        sources.extend(approval_sources)
        call=b'\tif (GC_SteamApprovalEnabled()) GC_SteamApprovalInit();'
        if raw.count(call)!=1:raise ValueError('Steam initialization boundary changed')
        (out/'steam-init-call.inc').write_bytes(b'static void InvokeApprovalFromServerInit() {\n'+call+b'\n}\n')
        for enabled in (0,1):
            objs=[]
            for i,src in enumerate(approval_sources):
                obj=out/f'approval-{enabled}-{i}.obj';objs.append(str(obj))
                commands.append(dict(name=f'compile-approval-{enabled}-{i}',argv=[str(compiler),*flags,*includes,*sdk_flags,f'/DGOLDCRAFT_STEAM_APPROVAL={enabled}','/c',str(src),f'/Fo{obj}']))
            exe=out/f'approval-{enabled}.exe'
            commands.append(dict(name=f'link-approval-{enabled}',argv=[str(compiler),'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',*objs,f'/Fe{exe}','/link','/OPT:REF','/OPT:ICF','/OPT:LLDLTO=3','/threads:1']))
            commands.append(dict(name=f'test-approval-{enabled}',argv=[str(exe)]))
    paths={Path(__file__).resolve(),sync,sv,wire,compiler,*sources,ROOT/'tests/engine-resource-sync-gate/wire.cpp',ROOT/'native/engine/GoldCraftEngine.targets'}
    if args.approval_only:paths.add(ROOT/'native/engine/steam_approval.cpp')
    for directory in (ROOT/'native/engine',ROOT/'native/include',sdk):
        paths.update(p for p in directory.rglob('*') if p.is_file() and p.suffix.lower() in ('.h','.hpp','.inc','.inl'))
    paths.update(out.glob('*.inc'))
    if args.environment_json:paths.add(args.environment_json.resolve())
    tracked=[identity(p) for p in sorted(paths)]
    plan=dict(inputs=tracked,commands=commands,extractions=[dict(source=identity(sv),start=start,end=end),dict(source=identity(wire),start=ws,end=we)],directWorkingTree=True,singleJob=True,liveGame=False)
    prefix='approval-' if args.approval_only else ''
    (out/(prefix+'plan.json')).write_text(json.dumps(plan,indent=2),encoding='utf8')
    if args.prepare_only:print('Prepared',out);return 0
    result=dict(success=False,results=[],liveGame=False);began=time.monotonic()
    try:
        for command in commands:
            start=time.monotonic();run=subprocess.run(command['argv'],cwd=ROOT,env=env,capture_output=True,text=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
            log=out/(command['name']+'.log');log.write_text(run.stdout+run.stderr,encoding='utf8')
            result['results'].append(dict(name=command['name'],exit=run.returncode,seconds=time.monotonic()-start,log=identity(log)))
            if run.returncode:raise RuntimeError(f"{command['name']} failed: {log}")
        result['success']=True
    except Exception as error:result['error']=str(error)
    finally:
        changed=[row['path'] for row in tracked if not Path(row['path']).is_file() or identity(row['path'])!=row]
        result['inputsUnchanged']=not changed;result['changed']=changed;result['success'] &= not changed;result['seconds']=time.monotonic()-began
        (out/(prefix+'result.json')).write_text(json.dumps(result,indent=2),encoding='utf8')
    print(json.dumps({k:result[k] for k in ('success','inputsUnchanged','seconds')}));return 0 if result['success'] else 1
if __name__=='__main__':raise SystemExit(main())
