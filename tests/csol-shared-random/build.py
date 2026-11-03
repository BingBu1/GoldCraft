"""Build/run the standalone native shared-RNG adapter test with an explicit SDK.

Run in a Visual Studio developer shell so INCLUDE/LIB identify the selected x86
toolchain. Generated sources, objects and reports stay in a new output folder.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,subprocess,time
from prepare_native import prepare

ROOT=Path(__file__).resolve().parents[2]


def identity(path):
    path=Path(path).resolve()
    with path.open('rb')as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
    return dict(path=str(path),bytes=path.stat().st_size,sha256=digest)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler',type=Path,required=True,help='Path to clang-cl with the matching lld linker')
    parser.add_argument('--sdk-root',type=Path,required=True,help='Bootstrapped project MetaHookSv SDK root')
    parser.add_argument('--binary',type=Path,required=True,help='Identity-matched original client.dll, read only')
    parser.add_argument('--catalog',type=Path,default=ROOT/'native/client/shared_random_10210.json')
    parser.add_argument('--output',type=Path,required=True,help='New directory; existing results are never replaced')
    args=parser.parse_args()
    compiler,sdk,binary,catalog,out=(p.resolve()for p in (args.compiler,args.sdk_root,args.binary,args.catalog,args.output))
    assert os.name=='nt'and compiler.is_file()and sdk.is_dir()
    assert not out.exists()
    for p in (out,*out.parents):assert not p.is_symlink()and not p.is_junction()
    out.mkdir(parents=True);source=out/'source';generated=out/'generated';temp=out/'temp';temp.mkdir()
    names=['native/csol/client/'+n for n in ('shared_random.cpp','shared_random.hpp','runtime.hpp')]
    names+=['tests/csol-shared-random/'+n for n in ('native_adapter.cpp','native_image.hpp')]
    before=[]
    for name in names:
        original=ROOT/name;target=source/name;target.parent.mkdir(parents=True,exist_ok=True)
        before.append(identity(original))
        with target.open('xb')as stream:stream.write(original.read_bytes())
        assert target.stat().st_nlink==1;before.append(identity(target))
    before+=[identity(p)for p in (binary,catalog,compiler,Path(__file__),Path(__file__).with_name('prepare_native.py'))]
    prepare(binary,catalog,generated)
    before+=[identity(p)for p in generated.iterdir()]
    include_paths=['MetaHook/include','MetaHook/include/Interface','MetaHook/include/HLSDK/cl_dll',
        'MetaHook/include/HLSDK/common','MetaHook/include/HLSDK/engine','MetaHook/include/HLSDK/pm_shared',
        'MetaHook/include/HLSDK/public','MetaHook/include/SourceSDK','MetaHook/include/SourceSDK/tier0',
        'MetaHook/include/SourceSDK/tier1','MetaHook/thirdparty/capstone_fork/include',
        'MetaHook/thirdparty/rapidjson/include','thirdparty/glew_fork/include','Plugins/Renderer/include/Interface']
    flags=['--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/O2',
        '/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise','/showIncludes',
        '/DNOMINMAX','/DGLEW_NO_GLU','/DGLEW_STATIC','/DNO_MALLOC_OVERRIDE','/D_CRT_SECURE_NO_WARNINGS']
    flags+=['/I'+str(sdk/p)for p in include_paths]+['/I'+str(generated)]
    commands=[]
    for name,relative in [('adapter','native/csol/client/shared_random.cpp'),('fixture','tests/csol-shared-random/native_adapter.cpp')]:
        commands.append((name,[str(compiler),*flags,'/c',str(source/relative),'/Fo'+str(out/(name+'.obj'))]))
    commands.append(('link',[str(compiler),'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(out/'fixture.obj'),
        str(out/'adapter.obj'),'/Fe'+str(out/'native.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']))
    commands.append(('test',[str(out/'native.exe'),str(binary)]))
    env=os.environ.copy();env.update(TEMP=str(temp),TMP=str(temp),VSLANG='1033')
    result=dict(success=False,commands=[],directInputs=before,actualIncludes=[],error=None,
        boundary='Original function bits and SDK adapter only; no DLL entry point, full plugin, UI or gameplay.')
    actual={}
    try:
        for name,argv in commands:
            print('Starting '+name,flush=True);started=time.monotonic()
            step=subprocess.run(argv,cwd=out,env=env,capture_output=True,timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
            log=out/(name+'.log');log.write_bytes(step.stdout+step.stderr);text=(step.stdout+step.stderr).decode('utf8','replace')
            result['commands'].append(dict(name=name,argv=argv,exitCode=step.returncode,seconds=time.monotonic()-started,log=identity(log)))
            for include in re.findall(r'^\s*Note: including file:\s*(.+?)\s*$',text,re.M):
                actual[str(Path(include).resolve())]=identity(include)
            if step.returncode or name=='test':print('\n'.join(text.splitlines()[-25:]),flush=True)
            if step.returncode:raise RuntimeError(name+' failed; original log preserved')
        result['success']=True
    except BaseException as error:result['error']=repr(error)
    finally:
        changed=[r['path']for r in [*before,*actual.values()]if identity(r['path'])!=r]
        result.update(inputsUnchanged=not changed,changed=changed,actualIncludes=list(actual.values()))
        result['success']=result['success']and not changed
        with (out/'result.json').open('x',encoding='utf8')as stream:json.dump(result,stream,indent=2)
    return 0 if result['success']else 1


if __name__=='__main__':raise SystemExit(main())
