"""Compile the exact production StudioEvent guard entry with the real GoldSrc SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    saved = json.loads((ROOT / 'build/melee-buy-20261103/environment.json').read_text(encoding='utf-8-sig'))
    env = os.environ.copy()
    env.update({key: saved[key] for key in ('INCLUDE', 'LIB', 'LIBPATH')})
    files = [Path(__file__).resolve(), ROOT / 'tests/csol-studio-event-guard/context.cpp',
             ROOT / 'native/csol/client/scene.cpp', ROOT / 'native/csol/client/studio_event_context.hpp',
             ROOT / 'native/csol/studio_event_guard.hpp']
    before = {str(path.relative_to(ROOT)): sha(path) for path in files}
    scene = files[2].read_text(encoding='utf-8')
    start = scene.index('void ObserveStudioEventSequence(const cl_entity_t* entity,model_t* model,int flags)')
    end = scene.index('    if(event && entity && entity->model && SkipStudioSound(', start)
    entry = scene[start:end]
    assert scene[scene.index('void ClearScene()'):].split('}', 1)[0].count('studioEventGuard.Clear();') == 1
    generated = output / 'entry.cpp'
    generated.write_text('#include "' + (ROOT / 'native/csol/client/studio_event_context.hpp').as_posix() + '"\n'
                         '#include "' + (ROOT / 'native/csol/studio_event_guard.hpp').as_posix() + '"\n'
                         'namespace csol::client { extern StudioEventRepeatGuard studioEventGuard; extern unsigned dispatched;\n'
                         + entry + '    ++dispatched;\n}\n}\n', encoding='utf-8')
    meta = ROOT / 'external/MetaHookSv-20261007'
    sdk = meta / 'MetaHook/include'
    includes = [sdk, sdk / 'Interface', sdk / 'HLSDK/cl_dll', sdk / 'HLSDK/common', sdk / 'HLSDK/engine',
                sdk / 'HLSDK/pm_shared', sdk / 'HLSDK/public', sdk / 'SourceSDK', sdk / 'SourceSDK/tier0',
                sdk / 'SourceSDK/tier1', meta / 'MetaHook/thirdparty/capstone_fork/include',
                meta / 'MetaHook/thirdparty/rapidjson/include', meta / 'thirdparty/glew_fork/include',
                meta / 'Plugins/Renderer/include/Interface']
    command = [saved['clang'], '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8',
               '/O2', '/clang:-O3', '/arch:AVX2', '/fp:precise', '/W3', '/DWIN32', '/D_WINDOWS', '/D_USRDLL',
               '/D_MBCS', '/DNO_MALLOC_OVERRIDE', '/DNOMINMAX', '/DGLEW_STATIC', '/DGLEW_NO_GLU', '/D_CRT_SECURE_NO_WARNINGS',
               *['/I' + str(p) for p in includes], str(files[1]), str(generated),
               '/Fe:' + str(output / 'context.exe'), '/Fo:' + str(output) + '\\']
    compiled = subprocess.run(command, cwd=output, env=env, capture_output=True, text=True, errors='replace')
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    ran = subprocess.run([str(output / 'context.exe')], capture_output=True, text=True) if compiled.returncode == 0 else None
    if ran:
        (output / 'test.log').write_text(ran.stdout + ran.stderr, encoding='utf-8')
    after = {str(path.relative_to(ROOT)): sha(path) for path in files}
    result = dict(success=compiled.returncode == 0 and ran.returncode == 0 and before == after,
                  inputs=before, unchanged=before == after, compileExit=compiled.returncode,
                  testExit=ran.returncode if ran else None, command=command,
                  entrySha256=hashlib.sha256(entry.encode()).hexdigest(),
                  boundary='Real SDK layout and exact production event guard entry; dispatch continuation is a counter. No gameplay/audio claim.')
    (output / 'receipt.json').write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(ran.stdout if ran else compiled.stdout + compiled.stderr)
    raise SystemExit(0 if result['success'] else 1)


if __name__ == '__main__':
    main()
