#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile and execute the real pinned GameData parser against old/new catalogs."""
import argparse
import hashlib
import json
import os
import re
import subprocess
import time
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    path = Path(path).resolve(strict=True)
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--sdk-project', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--old-catalog', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    assert output.is_relative_to(ROOT / 'build') and not output.exists()
    output.mkdir()
    (output / 'temp').mkdir()
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    environment['TEMP'] = environment['TMP'] = str(output / 'temp')
    project = ET.parse(args.sdk_project)
    directories = project.find('.//{*}ClCompile/{*}AdditionalIncludeDirectories').text
    includes = ['/I' + token for token in directories.split(';') if not token.startswith('%(')]
    inputs = [identity(args.compiler), identity(args.environment), identity(args.sdk_project), identity(__file__)]
    relative_sources = ['tests/csol-shared-random/catalog_loader.cpp',
                        'external/MetaHookSv-20261007/MetaHook/src/GameData.cpp',
                        'external/MetaHookSv-20261007/MetaHook/src/GameData.h']
    sources = []
    for relative in relative_sources:
        source, frozen = ROOT / relative, output / 'source' / relative
        frozen.parent.mkdir(parents=True, exist_ok=True)
        frozen.write_bytes(source.read_bytes())
        sources.append(dict(original=identity(source), frozen=identity(frozen)))
    for name, source in [('new', args.catalog), ('old', args.old_catalog)]:
        destination = output / name
        destination.mkdir()
        for filename in ('index.json', 'cs-client-10210-goldcraft.json'):
            frozen = destination / filename
            frozen.write_bytes((source / filename).read_bytes())
            inputs.append(identity(frozen))
    steps = []

    def execute(name, command):
        start = time.monotonic()
        result = subprocess.run(command, cwd=output, env=environment, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=180)
        log = output / (name + '.log')
        log.write_bytes(result.stdout)
        actual = sorted(set(re.findall(r'Note: including file:\s*(.+)', result.stdout.decode('utf-8', 'replace'))))
        steps.append(dict(name=name, command=command, exit=result.returncode, seconds=time.monotonic() - start,
                          log=identity(log), includes=[identity(p.strip()) for p in actual]))
        save(output / 'steps.json', steps)
        print(name, result.returncode, flush=True)
        if result.returncode:
            print(result.stdout.decode('utf-8', 'replace')[-6000:])
            raise RuntimeError(name + ' failed')

    obj, executable = output / 'catalog.obj', output / 'catalog.exe'
    execute('compile', [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '/nologo', '/std:c++20',
        '/EHsc', '/MT', '/O2', '/utf-8', '/UNDEBUG', '/DWIN32_LEAN_AND_MEAN', '/DNOMINMAX',
        '/D_CRT_SECURE_NO_WARNINGS', '/showIncludes', *includes, '/c', '/Fo' + str(obj),
        str(output / 'source' / relative_sources[0])])
    execute('link', [str(args.compiler.resolve()), '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
                    str(obj), '/Fe' + str(executable), '/link', '/OPT:REF', '/threads:1', 'user32.lib'])
    execute('old-rejected', [str(executable), str(output / 'old'), 'rejected'])
    execute('new-accepted', [str(executable), str(output / 'new'), 'valid'])
    actual = {row['path']: row for step in steps for row in step['includes']}
    frozen = [entry['frozen'] for entry in sources]
    changed = [row['path'] for row in inputs + frozen + list(actual.values()) if identity(row['path']) != row]
    save(output / 'result.json', dict(success=not changed, sources=sources, inputs=inputs,
        actualIncludes=list(actual.values()), steps=steps, changed=changed, deployed=False, liveGameplay=False,
        boundary='Actual pinned x86 GameData parser/query and seven missing-field negatives; no host game execution.'))
    assert not changed


if __name__ == '__main__':
    main()
