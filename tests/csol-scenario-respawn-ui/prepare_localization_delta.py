"""Freeze the parser correction and reuse the verified client TU; never compile."""
from pathlib import Path
import argparse
import copy
import json
import os
import shutil

from prepare import ROOT, identity, key, read, save


def verify(record):
    actual = identity(Path(record['path']))
    if actual['sha256'] != record['sha256'] or actual['bytes'] != record['bytes']:
        raise ValueError('Changed input: ' + record['path'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--previous', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--localization-sha', required=True)
    parser.add_argument('--prior-catalog', type=Path, help='Unchanged older full catalog for semantic regression')
    parser.add_argument('--previous-attempt', type=Path, help='Preserve a later failed parser attempt as evidence')
    args = parser.parse_args()
    previous, out = args.previous.resolve(), args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build') or not previous.is_relative_to(ROOT / 'build'):
        raise ValueError('Use existing previous and new workspace output directories')
    for path in (out, *out.parents, previous, *previous.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Reparse point in input/output path')
    plan = read(previous / 'proposal.json')
    result = read(previous / 'result.json')
    assert result['inputsUnchanged'] and result['failure'] == 'run-client unexpected exit 2'
    assert [(row['name'], row['exitCode']) for row in result['results']] == [
        ('compile-client', 0), ('link-client', 0), ('run-client', 2)]
    assert result['results'][0]['actualIncludes'] == 518 and not result['results'][0]['unknownIncludes']
    old_sources = read(previous / 'source-changes.json')
    include_records = read(previous / 'actual-includes.json')
    assert len(include_records) == 518
    by_frozen = {key(row['frozen']['path']): row for row in old_sources}
    working_client_sources = []
    for record in include_records:
        verify(record)
        if row := by_frozen.get(key(record['path'])):
            verify(row['original'])
            working_client_sources.append(row['original'])
    client_source = next(row for row in old_sources if row['relative'] == 'tests/csol-scenario-respawn-ui/client_main.cpp')
    verify(client_source['original']); verify(client_source['frozen'])
    working_client_sources.append(client_source['original'])
    production = ROOT / 'native/csol/scenario_localization.cpp'
    assert identity(production)['sha256'] == args.localization_sha.lower()
    assert key(production) not in {key(row['path']) for row in working_client_sources}
    old_localization = read(previous / 'reused-object.json')
    verify(old_localization['object']); verify(old_localization['source'])
    catalog = next(row for row in read(previous / 'resources.json') if row['path'].replace('\\', '/').endswith('/resource/cso_chn.txt'))
    verify(catalog)
    assert catalog['sha256'] == 'c5d675c7565b8694536fc2496741c8f6f38ed06f2a717d4476102b19400c6a4e'
    prior_catalog = identity(args.prior_catalog.resolve()) if args.prior_catalog else None
    if prior_catalog:
        assert prior_catalog['sha256'] == '520720d2a372995160356c71f5d1e789c046e497cfbfe646bf73752a6d79f68a'
    prior_attempt = identity(args.previous_attempt.resolve() / 'result.json') if args.previous_attempt else None
    if prior_attempt:
        attempt = read(Path(prior_attempt['path']))
        assert attempt['inputsUnchanged'] and attempt['failure'] == 'run-localization-new unexpected exit 1'
    client_object = identity(Path(plan['output']) / 'client.obj')
    out.mkdir(parents=True)
    source, output = out / 'source', out / 'output'
    changed = []
    for relative in ('native/csol/scenario_localization.cpp',
                     'tests/csol-scenario-respawn-ui/localization_main.cpp',
                     'tests/csol-scenario-respawn-ui/localization_catalog_expectations.hpp',
                     'tests/csol-scenario-respawn-ui/prepare_localization_delta.py'):
        original, target = ROOT / relative, source / relative
        for path in (original, *original.parents):
            assert not path.is_symlink() and not path.is_junction()
        before = identity(original)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(original, target)
        assert target.stat().st_nlink == 1 and not os.path.samefile(original, target)
        assert identity(target)['sha256'] == before['sha256']
        changed.append(dict(relative=relative, original=before, frozen=identity(target)))
    changed_by_relative = {row['relative']: row for row in changed}
    sources = [changed_by_relative.pop(row['relative'], row) for row in old_sources]
    sources.extend(changed_by_relative.values())
    save(out / 'source-changes.json', sources)
    aliases = {key(row['name']): row for row in read(Path(plan['overlay']['path']))['roots']}
    for row in sources:
        for virtual in (ROOT / row['relative'], source / row['relative'], previous / 'source' / row['relative']):
            aliases[key(virtual)] = dict(type='file', name=str(virtual), **{'external-contents': row['frozen']['path']})
    overlay = out / 'source-overlay.json'
    save(overlay, dict(version=0, **{'case-sensitive': False, 'use-external-names': False}, roots=list(aliases.values())))
    old_overlay, old_output = str(Path(plan['overlay']['path'])), str(Path(plan['output']))
    def remap(argument):
        return argument.replace(old_overlay, str(overlay)).replace(old_output, str(output))
    flags = [remap(arg) for arg in plan['compileFlags']]
    compiler = plan['compiler']
    linker = str(Path(compiler).with_name('lld-link.exe'))
    new_parser = output / 'localization.obj'
    fixture = output / 'localization-check.obj'
    jobs = []
    for name, relative, obj in (
        ('localization', 'native/csol/scenario_localization.cpp', new_parser),
        ('localization-check', 'tests/csol-scenario-respawn-ui/localization_main.cpp', fixture)):
        jobs.append(dict(name='compile-' + name, kind='compile', seconds=120, expectExit=0,
            command=[compiler, *flags, '/showIncludes', '-I' + str(source), '/Fo' + str(obj),
                '/Fd' + str(output / (name + '.compile.pdb')), '-c', '--', str(source / relative)]))
    libraries = ['kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'ole32.lib', 'advapi32.lib', 'bcrypt.lib']
    new_arguments = [catalog['path']] + ([prior_catalog['path']] if prior_catalog else [])
    new_count = 404 if prior_catalog else 251
    for name, parser_object, arguments, expected_exit, marker in (
        ('localization-new', new_parser, new_arguments, 0,
         'catalog semantic keys=140 commaValues=9\nscenario_localization checks=' + str(new_count) + ' failures=0'),
        ('localization-old', Path(old_localization['object']['path']), [catalog['path'], '--catalog-only'], 1,
         'catalog parse=1 before=1 title=0 unlimited=0\nscenario_localization checks=4 failures=2')):
        binary = output / (name + '.exe')
        jobs.append(dict(name='link-' + name, kind='link', seconds=180, expectExit=0,
            command=[linker, '/nologo', '/subsystem:console', '/machine:x86', '/debug', '/incremental:no', '/opt:ref',
                '/opt:icf', '/opt:lldltojobs=1', '/threads:1', '/out:' + str(binary), '/pdb:' + str(output / (name + '.pdb')),
                str(fixture), str(parser_object), *libraries]))
        jobs.append(dict(name='run-' + name, kind='run', seconds=60, expectExit=expected_exit,
            marker=marker, command=[str(binary), *arguments]))
        if name == 'localization-old' and prior_catalog:
            jobs.append(dict(name='run-localization-old-prior-catalog', kind='run', seconds=60, expectExit=1,
                marker=marker, command=[str(binary), prior_catalog['path'], '--catalog-only']))
    for old in plan['jobs']:
        if old['name'] == 'compile-client':
            continue
        job = copy.deepcopy(old)
        job['command'] = [remap(arg) for arg in old['command']]
        if old['name'] == 'link-client':
            job['command'] = [client_object['path'] if arg == str(output / 'client.obj') else
                              str(new_parser) if arg == old_localization['object']['path'] else arg
                              for arg in job['command']]
            assert str(new_parser) in job['command'] and client_object['path'] in job['command']
            assert old_localization['object']['path'] not in job['command']
        jobs.append(job)
    client_compile = next(job for job in plan['jobs'] if job['name'] == 'compile-client')
    save(out / 'reused-client.json', dict(object=client_object, originalCompile=client_compile,
        compilerResult=result['results'][0], actualIncludes=include_records, workingSources=working_client_sources,
        source=client_source, originalResult=identity(previous / 'result.json'),
        originalIncludeManifest=identity(previous / 'actual-includes.json')))
    save(out / 'localization-objects.json', dict(newSource=identity(production), newObject=str(new_parser),
        oldControl=old_localization, oldObjectAllowedOnlyIn='link-localization-old', catalog=catalog, priorCatalog=prior_catalog))
    shutil.copy2(previous / 'run.py', out / 'run.py')
    assert identity(out / 'run.py')['sha256'] == '855d9126ee15573b45ac44a45606d1d1d792fa07e1856d6787fce2f67cfe8d1c'
    direct = list(plan['inputs']) + [row['frozen'] for row in changed] + [client_object]
    direct.extend(include_records)
    if prior_catalog:
        direct.append(prior_catalog)
    if prior_attempt:
        direct.append(prior_attempt)
    direct.extend(identity(path) for path in (previous / 'proposal.json', previous / 'result.json',
        previous / 'actual-includes.json', out / 'source-changes.json', overlay, out / 'run.py',
        out / 'reused-client.json', out / 'localization-objects.json'))
    revised = copy.deepcopy(plan)
    revised.update(inputs=list({key(row['path']): row for row in direct}.values()), overlay=identity(overlay),
        jobs=jobs, maxSeconds=600, output=str(output), compileFlags=flags, selectedGroup='retry-budget-localization-delta',
        previousProposal=identity(previous / 'proposal.json'), previousFailure=identity(previous / 'result.json'),
        reusedClient=identity(out / 'reused-client.json'), newLocalization=identity(out / 'localization-objects.json'),
        preservedFailedParserAttempt=prior_attempt,
        compiled=False, deployed=False)
    save(out / 'proposal.json', revised)
    print(json.dumps(dict(proposal=identity(out / 'proposal.json'), changedIndependentSources=len(changed),
        retainedFrozenSources=len(sources)-len(changed), jobs=len(jobs), clientCompileReused=True,
        localizationRecompiled=True, oldObjectControlOnly=True, compilerInvoked=False), indent=2))


if __name__ == '__main__':
    main()
