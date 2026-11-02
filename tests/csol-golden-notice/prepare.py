"""Freeze a narrow actual-client Golden notice test; this command never compiles."""
from pathlib import Path
import argparse
import copy
import hashlib
import json
import os
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]


def read(path):
    return Path(path).read_text('utf-8-sig')


def identity(path):
    path = Path(path).resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=digest)


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--previous', type=Path, default=ROOT / 'build/csol-retry-budget-ui-20261102/prepare-03')
    args = parser.parse_args()
    out, previous = args.output.resolve(), args.previous.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a new workspace build directory')
    for path in (out, *out.parents, previous, *previous.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Reparse point in input/output path')
    plan = json.loads(read(previous / 'proposal.json'))
    result = json.loads(read(previous / 'result.json'))
    assert result['success'] and result['inputsUnchanged']
    out.mkdir(parents=True)
    source, output = out / 'source', out / 'output'
    pending = [ROOT / 'tests/csol-golden-notice/client.cpp', ROOT / 'tests/csol-golden-notice/prepare.py',
               ROOT / 'tests/csol-golden-notice/README.md', ROOT / 'native/csol/scenario_localization.cpp']
    sources, copied = [], set()
    while pending:
        original = pending.pop().resolve()
        if original in copied:
            continue
        if not original.is_relative_to(ROOT) or not original.is_file():
            raise ValueError('Source outside workspace or missing: ' + str(original))
        for path in (original, *original.parents):
            if path.is_symlink() or path.is_junction():
                raise ValueError('Reparse point in source path')
        relative = original.relative_to(ROOT)
        target = source / relative
        before = identity(original)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        assert target.stat().st_nlink == 1 and not os.path.samefile(original, target)
        assert identity(target)['sha256'] == before['sha256']
        sources.append(dict(relative=relative.as_posix(), original=before, frozen=identity(target)))
        copied.add(original)
        if original.suffix in ('.cpp', '.hpp', '.h'):
            for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', read(original), re.M):
                candidate = next((p for p in (original.parent / name, ROOT / name) if p.is_file()), None)
                if candidate is None:
                    raise ValueError('Unresolved quoted include: ' + name)
                pending.append(candidate)
    aliases = {key(row['name']): row for row in json.loads(read(plan['overlay']['path']))['roots']}
    for row in sources:
        originals = (ROOT / row['relative'], source / row['relative'])
        # Existing aliases may point to a previous freeze of the same project
        # file. Replace those aliases too so no relative include sees old UI.
        suffix = '\\' + row['relative'].replace('/', '\\').lower()
        for name in list(aliases):
            if name.lower().endswith(suffix):
                aliases[name]['external-contents'] = row['frozen']['path']
        for name in originals:
            aliases[key(name)] = dict(type='file', name=str(name), **{'external-contents': row['frozen']['path']})
    overlay = out / 'source-overlay.json'
    save(overlay, dict(version=0, **{'case-sensitive': False, 'use-external-names': False}, roots=list(aliases.values())))
    save(out / 'source-changes.json', sources)
    flags = [arg.replace(plan['overlay']['path'], str(overlay)) for arg in plan['compileFlags']]
    assert '/clang:' + str(overlay) in flags
    compiler = plan['compiler']
    linker = str(Path(compiler).with_name('lld-link.exe'))
    jobs = []
    for name, relative in (('client', 'tests/csol-golden-notice/client.cpp'),
                           ('localization', 'native/csol/scenario_localization.cpp')):
        jobs.append(dict(name='compile-' + name, kind='compile', seconds=120, expectExit=0,
            command=[compiler, *flags, '/showIncludes', '-I' + str(source), '/Fo' + str(output / (name + '.obj')),
                '/Fd' + str(output / (name + '.compile.pdb')), '-c', '--', str(source / relative)]))
    old_link = next(job for job in plan['jobs'] if job['name'] == 'link-client')
    libraries = [arg for arg in old_link['command'] if arg.lower().endswith('.lib')]
    binary = output / 'golden-notice.exe'
    jobs.append(dict(name='link-client', kind='link', seconds=180, expectExit=0,
        command=[linker, '/nologo', '/subsystem:console', '/machine:x86', '/debug', '/incremental:no', '/opt:ref',
            '/opt:icf', '/opt:lldltojobs=1', '/threads:1', '/out:' + str(binary), '/pdb:' + str(output / 'golden-notice.pdb'),
            str(output / 'client.obj'), str(output / 'localization.obj'), *libraries]))
    common = next(job for job in plan['jobs'] if job['name'] == 'run-client')['command'][1]
    catalog = identity(Path(common) / 'resource/cso_chn.txt')
    assert catalog['sha256'] == 'c5d675c7565b8694536fc2496741c8f6f38ed06f2a717d4476102b19400c6a4e'
    jobs.append(dict(name='run-client', kind='run', seconds=60, expectExit=0,
        marker='golden_notice_client checks=', command=[str(binary), common]))
    shutil.copy2(previous / 'run.py', out / 'run.py')
    assert identity(out / 'run.py')['sha256'] == '855d9126ee15573b45ac44a45606d1d1d792fa07e1856d6787fce2f67cfe8d1c'
    direct = list(plan['inputs']) + [row['frozen'] for row in sources] + [catalog]
    direct.extend(identity(path) for path in (overlay, out / 'source-changes.json', out / 'run.py',
        previous / 'proposal.json', previous / 'result.json', previous / 'actual-includes.json'))
    revised = copy.deepcopy(plan)
    revised.update(inputs=list({key(row['path']): row for row in direct}.values()), overlay=identity(overlay),
        jobs=jobs, maxSeconds=480, output=str(output), compileFlags=flags,
        selectedGroup='golden-center-notice', previousProposal=identity(previous / 'proposal.json'),
        previousResult=identity(previous / 'result.json'), reusedObjects=0, compiled=False, deployed=False)
    for name in ('reusedClient', 'newLocalization', 'preservedFailedParserAttempt', 'previousFailure'):
        revised.pop(name, None)
    save(out / 'proposal.json', revised)
    print(json.dumps(dict(proposal=identity(out / 'proposal.json'), independentSources=len(sources),
        jobs=len(jobs), compilerInvoked=False, reusedObjects=0), indent=2))


if __name__ == '__main__':
    main()
