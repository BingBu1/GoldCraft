"""Prepare one scene TU replacement against a verified deployed CRC-ON link.

This command hashes inputs, reads Ninja dependency metadata and writes a plan.
It never invokes a compiler/linker, deploys, or copies source trees.
"""
import argparse
import difflib
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('crc_build', ROOT/'tools/Build-CSOLCrcBaseline.py')
crc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(crc)


def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args()
    out = crc.safe_output(args.output, fresh=True)
    base = ROOT/'build/csol-client-crc/client'
    original_path = ROOT/'build/csol-client-crc-relink-20261107/plan.json'
    continuation_path = ROOT/'build/csol-client-crc-relink-20261107-continue/result.json'
    verification_path = base.parent/'verification.json'
    original, continuation, verified = map(load, (original_path, continuation_path, verification_path))
    if not continuation['passed'] or continuation['onRelinked'] or not continuation['on']['passed']:
        raise ValueError('Expected verified preserved ON, with OFF-only continuation')
    if not verified['passed'] or not verified['crcEnabled'] or verified['refreshEnabled']:
        raise ValueError('Base build is not verified CRC ON / refresh OFF')
    pinned = [*original['originalObjects'], *original['registeredLibraries'], original['environment'],
              *continuation['on']['products']]
    crc.verify(pinned)
    if len(original['originalObjects']) != 213 or len(original['registeredLibraries']) != 22:
        raise ValueError('Unexpected historical link input cardinality')
    on = next(v for v in original['variants'] if v['name'] == 'on')
    expected_dll = '1e9058441a5c024ce0bad588f2c6133a2afa577e4b058b46b20f46f8b7cfb13e'
    if next(p for p in continuation['on']['products'] if p['path'].lower().endswith('.dll'))['sha256'] != expected_dll:
        raise ValueError('Unexpected preserved ON DLL')
    database = load(base/'compile_commands.json')
    scene = ROOT/'native/csol/client/scene.cpp'
    record = [r for r in database if crc.key(r['file']) == crc.key(scene)]
    if len(record) != 1:
        raise ValueError('Expected exactly one scene compile record')
    record = record[0]
    argv = crc.split_windows(record['command'])
    for required in ('-DCSOL_OPTIMIZE_HOST_CRC=1', '-DCSOL_RESOURCE_REFRESH=0',
                     '--target=i686-pc-windows-msvc', '/clang:-O3', '/clang:-flto=thin',
                     '/arch:AVX2', '/fp:precise', '-std:c++20'):
        if argv.count(required) != 1:
            raise ValueError('Missing exact compile flag: '+required)
    compiler = crc.identity(argv[0])
    compiler_expected = load(base.parent/'inputs.json')['compiler']
    if compiler['sha256'] != compiler_expected['sha256']:
        raise ValueError('Compiler changed from historical build')
    old_obj = Path(record['output']).resolve()
    if sum(crc.key(a) == crc.key(old_obj) for a in on['argv'] if a.lower().endswith('.obj')) != 1:
        raise ValueError('Scene replacement is not one exact direct object')
    cache = (base/'CMakeCache.txt').read_text()
    ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M).group(1)
    dep_command = [ninja, '-C', str(base), '-t', 'deps', str(old_obj.relative_to(base))]
    dependency_text = subprocess.check_output(dep_command, text=True, encoding='utf-8')
    dependencies = [Path(line.strip()) for line in dependency_text.splitlines() if line.startswith('    ')]
    if len(dependencies) != 528:
        raise ValueError('Historical scene dependency metadata changed')
    expected = {crc.key(r['path']): r for r in verified['actualDependencies']}
    mapping_file = ROOT/'analysis/csol-source-layout-20261107/applied/mapping.tsv'
    mapping = {crc.key(ROOT/a): ROOT/b for a,b in
               (line.split('\t') for line in mapping_file.read_text(encoding='utf-8-sig').splitlines() if line)}
    archived = ROOT/'analysis/csol-source-layout-20261107/orig-view'

    def mapped(path):
        return mapping.get(crc.key(path), path)

    def normalized(data, logical_path):
        text = data.decode('utf-8-sig').replace('\r\n', '\n')
        def include(match):
            target = (logical_path.parent/match.group(2)).resolve()
            return match.group(1) + str(mapped(target)).replace('\\','/').casefold() + match.group(3)
        return re.sub(r'(^\s*#\s*include\s*")([^"\n]+)("[^\n]*$)', include, text, flags=re.M)

    source_inputs, moved, changed = [], [], []
    baseline_scene = None
    for old in dependencies:
        old = old.resolve()
        before = expected.get(crc.key(old))
        if before is None:
            raise ValueError('Dependency lacks original completion identity: '+str(old))
        current = mapped(old).resolve()
        now = crc.identity(current)
        source_inputs.append(now)
        if now['sha256'] == before['sha256']:
            if current != old:
                moved.append(dict(old=before, current=now, equality='exact bytes'))
            continue
        if not old.is_relative_to(ROOT):
            raise ValueError('System dependency changed: '+str(old))
        archive = archived/old.relative_to(ROOT)
        archive_id = crc.identity(archive)
        if archive_id['sha256'] != before['sha256']:
            raise ValueError('Archived pre-migration bytes are not historical build input: '+str(old))
        a, b = normalized(archive.read_bytes(), old), normalized(current.read_bytes(), current)
        if old == scene:
            baseline_scene = archive_id
            changed.append(dict(old=before, current=now, archive=archive_id))
            diff = ''.join(difflib.unified_diff(a.splitlines(True), b.splitlines(True),
                                             fromfile='historical scene (includes canonicalized)',
                                             tofile='current scene (includes canonicalized)'))
        elif a == b:
            moved.append(dict(old=before, current=now, archive=archive_id,
                              equality='only include path/newline migration; canonical includes identical'))
        else:
            raise ValueError('Non-scene ABI/source dependency differs: '+str(old))
    if len(changed) != 1 or baseline_scene is None:
        raise ValueError('Expected precisely one changed source, scene.cpp')
    compile_args = [('/Fo'+str(out/'scene.cpp.obj')) if a.startswith('/Fo') else
                    ('/Fd'+str(out/'scene-compile.pdb')) if a.startswith('/Fd') else a for a in argv]
    compile_args.insert(1, '/showIncludes')
    link_args = []
    for arg in on['argv']:
        if arg.lower().endswith('.obj') and crc.key(arg) == crc.key(old_obj):
            arg = str(out/'scene.cpp.obj')
        elif arg.startswith('--intdir='):
            arg = '--intdir='+str(out/'manifest')
        elif arg.lower().startswith(('/out:', '/implib:', '/pdb:')):
            flag, path = arg.split(':',1)
            arg = flag+':'+str(out/Path(path).name)
        link_args.append(arg)
    tools = [compiler, crc.identity(ninja)]
    for item in original['inputs']:
        if Path(item['path']).suffix.lower() == '.exe':
            crc.verify([item])
            tools.append(item)
    metadata = [crc.identity(p) for p in (original_path, continuation_path, verification_path,
                base/'compile_commands.json', mapping_file, Path(__file__),
                ROOT/'tools/Build-CSOLCrcBaseline.py', ROOT/'tools/Run-CSOLSceneBuild.py',
                ROOT/'tools/Inspect-MetaHookRuntime.py')]
    # Include only immutable identities and exact commands, never an environment dump.
    plan = dict(mode='single-scene-replacement', prepared=True, compileAuthorized=False,
                currentSourceBuild=False, sourceCopies=False, sourceRecompiled=str(scene),
                baseDllSha256=expected_dll, originalPlan=crc.identity(original_path),
                onContinuation=crc.identity(continuation_path), originalScene=changed[0],
                compiler=compiler, cwd=str(base), output=str(out), environment=original['environment'],
                environmentOverrides=original['environmentOverrides'], compile=compile_args, link=link_args,
                originalObjects=original['originalObjects'], registeredLibraries=original['registeredLibraries'],
                originalObjectCount=213, reusedObjectCount=212, directObjectCount=109,
                archivedObjectCount=104, originalDependencies=528, migration=moved,
                sourceInputs=source_inputs, tools=tools, metadata=metadata,
                retainedProducts=continuation['on']['products'], inputs=[*pinned,*source_inputs,*tools,*metadata],
                dependencyCommand=dep_command, flags=dict(crc=True,refresh=False),
                followup=['Recheck all input identities immediately before compile.',
                          'Compile once, record new object identity immediately; audit actual showIncludes.',
                          'Link sequentially with response file, preserving CMake manifest wrapper.',
                          'Record DLL/PDB/lib immediately after success, before log/PDB parsing.',
                          'Verify actual library reads, PE x86/PDB pair, CRC identifier and absent refresh interface.',
                          'Recheck source, retained objects/libraries and tools after link; no deployment implied.'])
    crc.verify(plan['inputs'])
    out.mkdir(parents=True)
    (out/'manifest').mkdir()
    crc.save(out/'plan.json', plan)
    (out/'scene-change.diff').write_text(diff, encoding='utf-8')
    (out/'historical-scene-dependencies.txt').write_text(dependency_text, encoding='utf-8')
    print(json.dumps(dict(plan=str(out/'plan.json'), objects=213, libraries=22,
                         dependencies=528, migrated=len(moved), compileStarted=False)))


if __name__ == '__main__':
    main()
