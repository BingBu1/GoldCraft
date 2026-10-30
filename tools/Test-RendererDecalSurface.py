#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Exercise the actual Renderer decal owner guard with matched x86 SDK layouts."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import subprocess

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / 'external/MetaHookSv-20261007/Plugins/Renderer/src/gl_rsurf.cpp'
SDK = ROOT / 'external/MetaHookSv-20261007/MetaHook/include'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(source):
    begin = source.index('static bool R_IsDecalSurfaceReady(')
    opening = source.index('{', begin)
    depth = 0
    for i in range(opening, len(source)):
        depth += (source[i] == '{') - (source[i] == '}')
        if not depth:
            return source[begin:i + 1] + '\n'
    raise ValueError('Unclosed production function')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--clang', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--bsp', type=Path, default=ROOT / 'sandbox/cs-server/Half-Life/cstrike/maps/zs_deadend.bsp')
    args = parser.parse_args()
    output = args.output.resolve()
    if not output.is_relative_to(ROOT / 'build'):
        parser.error('Output must be below workspace build')
    for node in (output, *output.parents):
        if node.is_symlink() or (hasattr(node, 'is_junction') and node.is_junction()):
            parser.error('Linked output refused')
    output.mkdir(parents=True, exist_ok=False)
    fixture = ROOT / 'tests/renderer-decal-surface/surface.cpp'
    inputs = [SOURCE, SDK / 'HLSDK/common/com_model.h', SDK / 'metahook.h', args.bsp, fixture, Path(__file__).resolve()]
    before = {str(path.relative_to(ROOT)): digest(path) for path in inputs}
    production = extract(SOURCE.read_text(encoding='utf-8'))
    if 'sizeof(msurface_hl25_t)' not in production:
        raise ValueError('Production HL25 correction not present')
    legacy = production.replace('R_IsDecalSurfaceReady(', 'R_IsDecalSurfaceReady_Legacy(', 1)
    # Keep every check and dereference identical; undo only the tested stride.
    legacy = legacy.replace('g_iEngineType == ENGINE_GOLDSRC_HL25 ? sizeof(msurface_hl25_t) : sizeof(msurface_t)', 'sizeof(msurface_t)')
    (output / 'production_surface.inc').write_text(production, encoding='utf-8')
    (output / 'legacy_surface.inc').write_text(legacy, encoding='utf-8')
    bsp = args.bsp.read_bytes()
    if struct.unpack_from('<i', bsp)[0] != 30:
        raise ValueError('Expected GoldSrc BSP30')
    lumps = [struct.unpack_from('<ii', bsp, 4 + i * 8) for i in range(15)]
    offset, size = lumps[14]
    models = list(struct.iter_unpack('<9f7i', bsp[offset:offset + size]))
    face_count = lumps[7][1] // 20
    if models[0][-2] != 0 or models[0][-1] != 9219:
        raise ValueError('Unexpected world surface range')
    ranges = [(m[-2], m[-1]) for m in models]
    generated = f'constexpr int BspFaceCount={face_count}, BspWorldFaces={models[0][-1]};\n'
    generated += 'constexpr int BspModels[][2]={\n' + ''.join(f'    {{{first},{count}}},\n' for first, count in ranges) + '};\n'
    (output / 'bsp_models.inc').write_text(generated, encoding='utf-8')
    includes = [output, SDK, SDK / 'Interface', *(SDK / 'HLSDK' / name for name in ('cl_dll', 'common', 'engine', 'pm_shared', 'public')),
                SDK / 'SourceSDK', SDK / 'SourceSDK/tier0', SDK / 'SourceSDK/tier1']
    exe = output / 'decal-surface.exe'
    command = [str(args.clang), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20', '/EHsc', '/MT', '/UNDEBUG',
               '/W3', '/utf-8', '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
               '/DNOMINMAX', '/DNO_MALLOC_OVERRIDE', '/D_CRT_SECURE_NO_WARNINGS',
               *('/I' + str(path) for path in includes), str(fixture), '/Fe' + str(exe),
               '/Fo' + str(output / 'surface.obj'), '/link', '/MACHINE:X86']
    (output / 'command.json').write_text(json.dumps(command, indent=2), encoding='utf-8')
    compiled = subprocess.run(command, cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, encoding='utf-8')
    result = None if compiled.returncode else subprocess.run([str(exe)], cwd=output, capture_output=True, text=True, encoding='utf-8', errors='replace')
    after = {str(path.relative_to(ROOT)): digest(path) for path in inputs}
    receipt = dict(inputsBefore=before, inputsAfter=after, unchanged=before == after, compileExit=compiled.returncode,
                   testExit=None if result is None else result.returncode, output='' if result is None else result.stdout + result.stderr,
                   productionFunctionSha256=hashlib.sha256(production.encode()).hexdigest(), models=len(models), totalFaces=face_count,
                   boundary='Exact production function extraction, real x86 SDK92/108-byte structures, real BSP face/model ranges. CPU ownership/validity guard only; no OpenGL rendering or game operation.')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), encoding='utf-8')
    print(receipt['output'] or compiled.stdout + compiled.stderr)
    if compiled.returncode or result is None or result.returncode or before != after:
        raise SystemExit(1)


if __name__ == '__main__':
    main()
