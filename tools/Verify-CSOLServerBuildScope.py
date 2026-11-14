#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate consumed server inputs while preserving an unrelated broad rejection.

Actual compile commands must cover every linked object. Workspace libraries
are audited from their CMake compilation database or the recorded static-library
link. A dependency-only replay never compiles or overwrites the baseline objects.
"""
import argparse
import hashlib
from concurrent.futures import ThreadPoolExecutor
import importlib.util
import json
import os
from pathlib import Path
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
REGAMEDLL_REVISION = '4a50c42e85fd3778c2b7d24731c7d30c83bbab01'


def pinned_archive(path):
    """The source SDK supplies this prebuilt x86 library, not compiler jobs."""
    source = 'regamedll/lib/libacof32.lib'
    repo = ROOT / 'external/ReGameDLL_CS'
    path = Path(path).resolve(strict=True)
    if path != (repo / source).resolve(strict=True):
        raise ValueError('Unknown prebuilt SDK library: ' + str(path))
    original = subprocess.check_output(['git', 'show', REGAMEDLL_REVISION + ':' + source], cwd=repo)
    expected = hashlib.sha256(original).hexdigest()
    if len(original) != path.stat().st_size or hashlib.sha256(path.read_bytes()).hexdigest() != expected:
        raise ValueError('Prebuilt SDK library differs from the pinned Git object')
    return dict(path=str(path), bytes=len(original), sha256=expected,
                sourceRevision=REGAMEDLL_REVISION, sourcePath=source,
                boundary='Byte-identical prebuilt SDK archive; no claim of rebuilding its original objects')


def dependency_command(command, source, dependency, output):
    # CMake terminates its options with --. Dependency options must precede
    # that terminator; do not pass them to clang-cl as source filenames.
    flags = [value for value in command if not (value in ('--', '-c', '/c') or
             value.lower().endswith(('.cpp', '.c')) or
             value.lower().startswith(('/fo', '/fd', '/yu', '/yc', '/showincludes')) or
             (value.lower().startswith('/fp') and not value.lower().startswith('/fp:')))]
    return flags + ['/Y-', '-Wno-unused-command-line-argument', '/clang:-M', '/clang:-MF' + str(dependency),
                    '/clang:-MTserver-scope', '/Fo' + str(output), str(source)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline', required=True, type=Path)
    parser.add_argument('--history-log', action='append', type=Path, default=[])
    parser.add_argument('--common-database', required=True, type=Path)
    parser.add_argument('--environment', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('actual_sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
    sdk = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(sdk)
    base, output = sdk.checked(args.baseline), sdk.checked(args.output, exists=False)
    if not base.is_relative_to(ROOT / 'build') or not output.is_relative_to(ROOT / 'build') or output.exists():
        raise ValueError('Use a current workspace build and fresh receipt directory')
    original = sdk.read(base / 'receipt.json')
    if original['success'] or original['inputsUnchanged'] or not original['steps'] or \
            any(row['exit'] != 0 for row in original['steps']):
        raise ValueError('Only a completely linked, source-drift-rejected baseline is in scope')
    sdk.verify(original['artifact'])
    for row in original['controls']:
        sdk.verify(row)
    environment = sdk.checked(args.environment)
    if not any(sdk.checked(row['path']) == environment and sdk.digest(environment) == row['sha256']
               for row in original['controls']):
        raise ValueError('Use the baseline compiler environment')
    env_data = sdk.read(environment)
    env = dict(os.environ)
    if env.get('CL') or env.get('_CL_'):
        raise ValueError('Unexpected compiler argument environment')
    for key, value in env_data.items():
        for old in list(env):
            if old.casefold() == key.casefold():
                del env[old]
        env[key] = value
    compile_command, link_command = sdk.commands(base / 'msbuild.log')
    link_inputs = sdk.resolve_link_inputs(link_command, env_data)
    logs = [sdk.checked(path) for path in args.history_log] + [base / 'msbuild.log']
    controls = [sdk.identity(path) for path in (Path(__file__), ROOT / 'tests/csol-rebuy/run_sdk.py',
                environment, args.common_database, base / 'receipt.json', base / 'sources/source-inputs.json', *logs)]
    before = {str(sdk.checked(row['path'])).casefold(): row
              for row in sdk.read(base / 'sources/source-inputs.json')}
    # Concurrent new, unlinked source modules are inventoried separately. They
    # are never claimed compiled by this scoped server validation.
    candidate_spec = importlib.util.spec_from_file_location('actual_candidate', ROOT / 'tools/Build-CSOLCandidate.py')
    candidate = importlib.util.module_from_spec(candidate_spec)
    candidate_spec.loader.exec_module(candidate)
    added = [sdk.identity(path) for path in candidate.source_files()
             if str(sdk.checked(path)).casefold() not in before]
    changed = []
    for row in before.values():
        now = sdk.identity(row['path'])
        if now['sha256'] != row['sha256']:
            relative = Path(row['path']).relative_to(ROOT).as_posix()
            unrelated = relative == 'native/engine/csol_loader.cpp' or \
                relative.startswith(('native/csol/client/', 'native/guest/cof/client/', 'amxx/scenario/')) or \
                relative == 'native/csol/CMakeLists.txt'
            if not unrelated:
                raise ValueError('Unexplained build drift: ' + relative)
            changed.append(dict(before=row, after=now))
    if not changed:
            raise ValueError('Expected explicit unrelated build drift')

    jobs = {}
    compiler_roots = (sdk.SDK_CWD, ROOT / 'external/ReGameDLL_CS/dep/cppunitlite/msvc')
    for path in logs:
        for line in sdk.msbuild_text(path)[0].splitlines():
            start = re.search(r'[A-Za-z]:\\[^\r\n]*?clang-cl\.exe /c ', line)
            if not start:
                continue
            command = sdk.windows_arguments(line[start.start():])
            sources = [value for value in command if value.lower().endswith(('.cpp', '.c'))]
            outputs = [value[3:] for value in command if value.lower().startswith('/fo')]
            if len(outputs) != 1:
                raise ValueError('Missing actual object output')
            for source in sources:
                cwd = next((root for root in compiler_roots if (root / source).is_file()), None)
                if cwd is None:
                    raise ValueError('Unknown translation unit: ' + source)
                src = sdk.checked(cwd / source)
                obj = Path(outputs[0])
                if obj.suffix.lower() != '.obj':
                    obj /= src.with_suffix('.obj').name
                obj = sdk.checked(cwd / obj)
                jobs[str(obj).casefold()] = dict(source=src, object=obj, cwd=cwd, command=command, log=path)
    linked_objects = [row for row in link_inputs if Path(row['path']).suffix.lower() == '.obj']
    if any(str(sdk.checked(row['path'])).casefold() not in jobs for row in linked_objects):
        raise ValueError('Recorded commands do not cover every linked object')
    selected = [jobs[str(sdk.checked(row['path'])).casefold()] for row in linked_objects]
    libraries = [row for row in link_inputs if Path(row['path']).suffix.lower() == '.lib' and
                 Path(row['path']).is_relative_to(ROOT)]
    pinned_libraries = []
    for library in libraries:
        path = Path(library['path'])
        if path.name.casefold() == 'goldcraft_common.lib':
            database = sdk.read(args.common_database)
            common = [row for row in database if 'goldcraft_common.dir' in row.get('output', '')]
            if not common:
                raise ValueError('The actual common-library compilation database is empty')
            for row in common:
                command = sdk.windows_arguments(row['command'])
                selected.append(dict(source=sdk.checked(row['file']), object=sdk.checked(row['output']),
                    cwd=sdk.checked(row['directory']), command=command, log=args.common_database))
        elif path.name.casefold() == 'cppunitlite.lib':
            librarian = Path(link_command[0]).with_name('llvm-lib.exe')
            controls.append(sdk.identity(librarian))
            listing = subprocess.run([str(librarian), '/LIST', '/NOLOGO', str(path)],
                                     cwd=sdk.SDK_CWD, env=env, capture_output=True, timeout=30)
            if listing.returncode:
                raise ValueError('Cannot enumerate actual cppunitlite archive members')
            members = [value.strip() for value in listing.stdout.decode('utf8').splitlines() if value.strip()]
            if not members:
                raise ValueError('Empty cppunitlite member list')
            for name in members:
                obj = sdk.checked(name if Path(name).is_absolute() else sdk.SDK_CWD / name)
                job = jobs.get(str(obj).casefold())
                if job is None:
                    raise ValueError('Missing archive member compilation: ' + str(obj))
                selected.append(job)
        elif path == ROOT / 'external/ReGameDLL_CS/regamedll/lib/libacof32.lib':
            pinned_libraries.append(pinned_archive(path))
        else:
            raise ValueError('Uncovered workspace library: ' + str(path))
    output.mkdir(parents=True)
    started = time.monotonic()

    def scan(item):
        index, job = item
        dep = output / f'{index:03d}.d'
        flags = dependency_command(job['command'], job['source'], dep, output / f'{index:03d}.obj')
        response = output / f'{index:03d}.rsp'
        response.write_text(subprocess.list2cmdline(flags[1:]), 'utf8')
        done = subprocess.run([flags[0], '@' + str(response)], cwd=job['cwd'], env=env,
                              capture_output=True, timeout=120)
        (output / f'{index:03d}.log').write_bytes(done.stdout + done.stderr)
        if done.returncode or not dep.is_file() or (output / f'{index:03d}.obj').exists():
            raise RuntimeError('Dependency-only replay failed: ' + str(job['source']))
        text = dep.read_text('utf8').replace('\\\r\n', '').replace('\\\n', '')
        if not text.startswith('server-scope:'):
            raise ValueError('Unexpected dependency target')
        paths = {}
        for token in re.split(r'(?<!\\)\s+', text[len('server-scope:'):].strip()):
            # Retain every lexical dependency, including system headers. Check
            # each unique path after collecting the jobs, before accepting any
            # result. Repeating filesystem calls per TU adds no new evidence.
            path = Path(os.path.abspath(job['cwd'] / re.sub(r'\\([ #$])', r'\1', token)))
            paths[str(path).casefold()] = str(path)
        return dict(source=str(job['source']), object=str(job['object']), cwd=str(job['cwd']),
                    command=flags, originalCommand=job['command'], originLog=str(job['log']),
                    dependency=sdk.identity(dep), dependencyPaths=list(paths.values()))

    with ThreadPoolExecutor(max_workers=4) as executor:
        results = list(executor.map(scan, enumerate(selected)))
    dependency_paths = {}
    path_occurrences = 0
    for job in results:
        paths = job['dependencyPaths']
        path_occurrences += len(paths)
        dependency_paths.update((value.casefold(), value) for value in paths)
    resolved_dependencies = {}
    for key, value in dependency_paths.items():
        # checked still rejects links/junctions and resolves the final path.
        # No cached success escapes this single verification invocation.
        resolved_dependencies[key] = sdk.checked(value)
    for job in results:
        dependencies = {}
        for value in job.pop('dependencyPaths'):
            path = resolved_dependencies[value.casefold()]
            if path.is_relative_to(ROOT) and not path.is_relative_to(ROOT / 'build'):
                row = before.get(str(path).casefold())
                if not row:
                    raise ValueError('Unrecorded consumed workspace input: ' + str(path))
                dependencies[str(path).casefold()] = row
        job['workspaceInputs'] = list(dependencies.values())
    actual = {row['path'].casefold(): row for job in results for row in job['workspaceInputs']}
    if any(row['before']['path'].casefold() in actual for row in changed):
        raise ValueError('The changed engine source was consumed by this server')
    if any(row['path'].casefold() in actual for row in added):
        raise ValueError('New source files were consumed but not recorded before the build')
    for row in controls + link_inputs + list(actual.values()):
        sdk.verify(row)
    for row in pinned_libraries:
        if pinned_archive(row['path']) != row:
            raise ValueError('Pinned SDK archive identity changed during dependency audit')
    sdk.verify(original['artifact'])
    for row in changed:
        sdk.verify(row['after'])
    source_scope = [row for key, row in before.items() if key not in
                    {value['before']['path'].casefold() for value in changed}]
    for row in source_scope:
        sdk.verify(row)
    (output / 'source-inputs.json').write_text(json.dumps(source_scope, indent=2) + '\n', 'utf8')
    receipt = dict(original)
    receipt.update(success=True, inputsUnchanged=True, sourceCopies=0,
        originalBroadReceipt=sdk.identity(base / 'receipt.json'), originalBroadReceiptAccepted=False,
        validation='Full x86 server link with every linked object and workspace library dependency-replayed; only recorded unconsumed client/engine/Pawn drift excluded',
        excludedUnconsumedChanges=changed, addedUncompiledFiles=added, actualWorkspaceInputs=list(actual.values()),
        pinnedSdkLibraries=pinned_libraries,
        dependencyPathOccurrences=path_occurrences, uniqueDependencyPaths=len(dependency_paths),
        controls=controls, jobs=results, scanned=len(results), seconds=time.monotonic() - started,
        deployed=False, gameplay=False, performanceAccepted=False)
    path = output / 'receipt.json'
    path.write_text(json.dumps(receipt, indent=2) + '\n', 'utf8')
    seal = dict(receipt=sdk.identity(path), sourceInventory=sdk.identity(output / 'source-inputs.json'),
        log=sdk.identity(base / 'msbuild.log'), logEncoding=sdk.msbuild_text(base / 'msbuild.log')[1],
        environment=sdk.identity(environment), artifact=sdk.identity(original['artifact']['path']),
        compiler=sdk.identity(compile_command[0]), linker=sdk.identity(link_command[0]),
        compileCommand=compile_command, linkCommand=link_command, linkInputs=link_inputs)
    (output / 'link-inputs.json').write_text(json.dumps(seal, indent=2) + '\n', 'utf8')
    print(json.dumps(dict(success=True, scanned=len(results), actualWorkspaceInputs=len(actual),
                         excluded=len(changed), seconds=receipt['seconds'], originalBroadReceiptAccepted=False)))


if __name__ == '__main__':
    main()
