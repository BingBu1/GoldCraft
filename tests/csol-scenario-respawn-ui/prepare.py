"""Prepare independent revival HUD sources and sequential checks; never compile."""
from pathlib import Path
import argparse
import ctypes
import hashlib
import json
import os
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
BASE = ROOT / 'build/csol-frozen-death-20261101/fresh-combination-01'
CLIENT = BASE / 'revision-02/output/client'


def read(path):
    return json.loads(path.read_text('utf-8-sig'))


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def identity(path):
    path = path.resolve()
    with path.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), sha256=digest, bytes=path.stat().st_size)


def split(command):
    fn = ctypes.windll.shell32.CommandLineToArgvW
    fn.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    fn.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    result = fn(command, ctypes.byref(count))
    if not result:
        raise ctypes.WinError()
    try:
        return [result[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
        ctypes.windll.kernel32.LocalFree(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--passed-core', type=Path, help='Reuse an unchanged successful core check; only client and routing execute')
    parser.add_argument('--only', choices=('round-text', 'retry-ui', 'retry-budget'),
                        help='Select round text, retry UI, or client/budget/continuation checks')
    args = parser.parse_args()
    if args.only == 'retry-budget' and args.passed_core:
        raise ValueError('The budget batch has its own changed client and panel checks')
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a new workspace build directory')
    for path in (out, *out.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Reparse point in output path')
    out.mkdir(parents=True)
    source = out / 'source'
    audit = read(BASE / 'revision-02/dependency-audit-02.json')
    assert audit['success'] and audit['sourceConsumptionComplete']
    entries = ('client_main.cpp', 'retry_budget_main.cpp', 'continuation_main.cpp', 'prepare.py') if args.only == 'retry-budget' else (
        'main.cpp', 'client_main.cpp', 'slot_main.cpp', 'round_text_main.cpp', 'retry_budget_main.cpp', 'prepare.py')
    pending = [ROOT / f'tests/csol-scenario-respawn-ui/{name}' for name in entries]
    if args.only == 'retry-budget':
        pending.extend(ROOT / path for path in ('native/csol/server/scenario_consumables.cpp', 'native/csol/server/persistence/types.cpp'))
    pending.append(ROOT / 'native/csol/scenario_localization.cpp')
    frozen = []
    copied = set()
    while pending:
        original = pending.pop().resolve()
        if original in copied:
            continue
        assert original.is_relative_to(ROOT) and original.is_file()
        assert not original.is_symlink() and not original.is_junction()
        relative = original.relative_to(ROOT)
        target = source / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        before = identity(original)
        shutil.copy2(original, target)
        assert target.stat().st_nlink == 1 and not os.path.samefile(original, target)
        after = identity(target)
        assert before['sha256'] == after['sha256']
        frozen.append(dict(relative=relative.as_posix(), original=before, frozen=after))
        copied.add(original)
        if original.suffix in ('.cpp', '.hpp', '.h'):
            for name in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', original.read_text('utf-8-sig'), re.M):
                candidate = next((p for p in (original.parent / name, ROOT / name) if p.is_file()), None)
                assert candidate is not None, (str(original), name)
                pending.append(candidate)
    production = read(BASE / 'source-manifest-final.json')
    aliases = {key(row['name']): row for row in read(BASE / 'source-overlay-final.json')['roots']}
    for row in production['files']:
        target = source / row['relative']
        replacement = target if target.exists() else Path(row['frozen']['path'])
        for virtual in (target, Path(row['original']['path']), Path(row['frozen']['path'])):
            aliases[key(virtual)] = dict(type='file', name=str(virtual), **{'external-contents': str(replacement)})
    for row in frozen:
        for virtual in (ROOT / row['relative'], source / row['relative']):
            aliases[key(virtual)] = dict(type='file', name=str(virtual), **{'external-contents': row['frozen']['path']})
    overlay = out / 'source-overlay.json'
    save(overlay, {'version': 0, 'case-sensitive': False, 'use-external-names': False, 'roots': list(aliases.values())})
    external = read(BASE / 'external-inputs-final.json')
    excluded = [row for row in external['files'] if Path(row['path']).resolve().is_relative_to(ROOT / 'external/ReHLDS')]
    save(out / 'external-inputs.json', dict(origin=identity(BASE / 'external-inputs-final.json'),
        files=[row for row in external['files'] if row not in excluded], excludedUnconsumedReHLDS=excluded))
    commands = read(CLIENT / 'compile_commands.json')
    origin = next(row for row in commands if row['file'].replace('\\', '/').endswith('/client/scenario_round_hud_client.cpp'))
    flags = []
    raw = split(origin['command'])
    compiler = raw.pop(0)
    for arg in raw:
        if arg in ('-c', '--', origin['file']) or arg.startswith(('/Fo', '/Fd')):
            continue
        if arg == '/clang:' + str(BASE / 'source-overlay-final.json'):
            arg = '/clang:' + str(overlay)
        flags.append(arg)
    for required in ('--target=i686-pc-windows-msvc', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '-std:c++20', '-MT'):
        assert required in flags
    data = next(row for row in commands if row['file'].replace('\\', '/').endswith('/native/csol/scenario_localization.cpp'))
    for name in ('scenario_localization.cpp', 'scenario_localization.hpp'):
        relative = 'native/csol/' + name
        assert identity(source / relative)['sha256'] == identity(BASE / 'source' / relative)['sha256']
    reused = CLIENT / data['output']
    save(out / 'reused-object.json', dict(object=identity(reused), source=identity(Path(data['file'])), compileCommand=data))
    slot_objects = []
    slot_record = {}
    if args.only != 'retry-budget':
        slots = next(row for row in commands if row['file'].replace('\\', '/').endswith('/native/csol/weapon_slots.cpp'))
        slot_objects.append(CLIENT / slots['output'])
        for name in ('weapon_slots.cpp', 'weapon_slots.hpp'):
            relative = 'native/csol/' + name
            assert identity(ROOT / relative)['sha256'] == identity(BASE / 'source' / relative)['sha256']
        slot_record = dict(object=identity(slot_objects[0]), source=identity(Path(slots['file'])), compileCommand=slots)
    save(out / 'reused-slots.json', slot_record)
    round_objects = []
    round_records = []
    round_inputs = () if args.only in ('retry-ui', 'retry-budget') else ('scenario_round_hud.cpp', 'scenario/config.cpp', 'scenario/spawn_points.cpp',
        'scenario/message_table.cpp', 'scenario/message_resource.cpp', 'scenario/dialogue_resources.cpp', 'scenario/wave_events.cpp')
    for name in round_inputs:
        relative = 'native/csol/' + name
        assert identity(ROOT / relative)['sha256'] == identity(BASE / 'source' / relative)['sha256']
        entry = next(row for row in commands if row['file'].replace('\\', '/').endswith('/' + relative))
        obj = CLIENT / entry['output']
        round_objects.append(obj)
        round_records.append(dict(object=identity(obj), source=identity(Path(entry['file'])), compileCommand=entry))
    save(out / 'reused-round.json', round_records)
    asset_paths = ('resource/cso_chn.txt',) if args.only == 'retry-budget' else ('resource/cso_chn.txt', 'resource/Hud/Hud_string_bg.tga',
        'resource/Hud/ZBS/roundclear.tga', 'resource/Hud/ZBS/roundfail.tga', 'sprites/zbs_level.spr')
    assets = [args.common / path for path in asset_paths]
    contract = '' if args.only == 'retry-budget' else (source / 'native/csol/scenario_respawn_resources.hpp').read_text('utf-8')
    import struct
    for path, width, height in re.findall(r'\{"([^"]+\.tga)", (\d+), (\d+)\}', contract):
        resource = args.common / path
        assert struct.unpack_from('<HH', resource.read_bytes(), 12) == (int(width), int(height)), path
        if resource not in assets:
            assets.append(resource)
    for path in re.findall(r'"(sound/[^"]+\.wav)"', contract):
        resource = args.common / path
        data = resource.read_bytes()
        assert data[:4] == b'RIFF' and data[8:12] == b'WAVE' and len(data) > 44, path
        assets.append(resource)
    save(out / 'resources.json', [identity(path) for path in assets])
    output = out / 'output'
    jobs = []
    linker = str(Path(compiler).with_name('lld-link.exe'))
    groups = [
        ('core', 'main.cpp', [], [], 'scenario_respawn_ui checks='),
        ('client', 'client_main.cpp', [reused], [args.common.resolve()], 'scenario_respawn_client checks='),
        ('slots', 'slot_main.cpp', [reused, *slot_objects], [], 'scenario_respawn_slots checks='),
        ('round-text', 'round_text_main.cpp', [reused, *round_objects], [args.common.resolve()], 'scenario_respawn_round_text newChecks='),
        ('retry-budget', 'retry_budget_main.cpp', [], [], 'scenario_retry_budget checks=')]
    previous_core = []
    if args.passed_core:
        previous = args.passed_core.resolve()
        result = read(previous / 'result.json')
        assert any(row['name'] == 'run-core' and row['exitCode'] == 0 for row in result['results'])
        for relative in ('tests/csol-scenario-respawn-ui/main.cpp', 'native/csol/client/scenario_respawn_ui.hpp',
                         'native/csol/scenario_respawn_resources.hpp'):
            assert identity(source / relative)['sha256'] == identity(previous / 'source' / relative)['sha256']
        previous_core = [identity(previous / 'result.json'), identity(previous / 'run-core.log')]
        groups = groups[1:]
    for name, entry, others, arguments, marker in groups:
        if args.only == 'round-text' and name != args.only:
            continue
        if args.only == 'retry-ui' and name == 'round-text':
            continue
        if args.only == 'retry-budget' and name not in ('client', 'retry-budget'):
            continue
        obj = output / (name + '.obj')
        binary = output / (name + '.exe')
        jobs.append(dict(name='compile-' + name, kind='compile', seconds=120, expectExit=0,
            command=[compiler, *flags, '/showIncludes', '-I' + str(source), '/Fo' + str(obj),
                '/Fd' + str(output / (name + '.compile.pdb')), '-c', '--', str(source / 'tests/csol-scenario-respawn-ui' / entry)]))
        jobs.append(dict(name='link-' + name, kind='link', seconds=180, expectExit=0,
            command=[linker, '/nologo', '/subsystem:console', '/machine:x86', '/debug', '/incremental:no', '/opt:ref',
                '/opt:icf', '/opt:lldltojobs=1', '/threads:1', '/out:' + str(binary), '/pdb:' + str(output / (name + '.pdb')),
                str(obj), *map(str, others), 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'ole32.lib', 'advapi32.lib']))
        jobs.append(dict(name='run-' + name, kind='run', seconds=60, expectExit=0, marker=marker, command=[str(binary), *map(str, arguments)]))
    if args.only == 'retry-budget':
        continuation_objects = []
        for name, relative in (
            ('continuation', 'tests/csol-scenario-respawn-ui/continuation_main.cpp'),
            ('consumables', 'native/csol/server/scenario_consumables.cpp'),
            ('persistence-types', 'native/csol/server/persistence/types.cpp')):
            obj = output / (name + '.obj')
            continuation_objects.append(obj)
            jobs.append(dict(name='compile-' + name, kind='compile', seconds=120, expectExit=0,
                command=[compiler, *flags, '/showIncludes', '-I' + str(source), '/Fo' + str(obj),
                    '/Fd' + str(output / (name + '.compile.pdb')), '-c', '--', str(source / relative)]))
        binary = output / 'continuation.exe'
        jobs.append(dict(name='link-continuation', kind='link', seconds=180, expectExit=0,
            command=[linker, '/nologo', '/subsystem:console', '/machine:x86', '/debug', '/incremental:no', '/opt:ref',
                '/opt:icf', '/opt:lldltojobs=1', '/threads:1', '/out:' + str(binary), '/pdb:' + str(output / 'continuation.pdb'),
                *map(str, continuation_objects), 'kernel32.lib', 'bcrypt.lib']))
        jobs.append(dict(name='run-continuation', kind='run', seconds=60, expectExit=0,
            marker='scenario_retry_continuation checks=', command=[str(binary)]))
    runner = ROOT / 'build/csol-character-skills-completion-20261031/status-buy-narrow-20261101-02/run.py'
    runner_text = runner.read_text('utf-8')
    for old, new in (
        ("            if time.monotonic() - started > plan['maxSeconds']:\n",
         "            remaining = plan['maxSeconds'] - (time.monotonic() - started)\n            if remaining <= 0:\n"),
        ("process.communicate(timeout=job['seconds'])", "process.communicate(timeout=min(job['seconds'], remaining))")):
        if runner_text.count(old) != 1:
            raise ValueError('Unexpected source runner deadline contract')
        runner_text = runner_text.replace(old, new, 1)
    (out / 'run.py').write_text(runner_text, encoding='utf-8')
    save(out / 'source-changes.json', frozen)
    direct = [row['frozen'] for row in frozen] + [identity(path) for path in assets] + [identity(reused)] + previous_core
    direct.extend(identity(path) for path in slot_objects)
    direct.extend(identity(path) for path in round_objects)
    direct.extend(identity(path) for path in (overlay, out / 'run.py', out / 'source-changes.json', out / 'resources.json',
        out / 'external-inputs.json', out / 'reused-object.json', out / 'reused-slots.json', out / 'reused-round.json', BASE / 'source-manifest-final.json', BASE / 'environment.json',
        BASE / 'revision-02/dependency-audit-02.json', CLIENT / 'compile_commands.json'))
    save(out / 'proposal.json', dict(inputs=list({key(row['path']): row for row in direct}.values()),
        productionManifest=identity(BASE / 'source-manifest-final.json'), externalManifest=identity(out / 'external-inputs.json'),
        environment=identity(BASE / 'environment.json'), overlay=identity(overlay), jobs=jobs, maxSeconds=600,
        output=str(output), compiler=compiler, originalCompile=origin, compileFlags=flags, previousCore=previous_core,
        selectedGroup=args.only, compiled=False, deployed=False))
    print(json.dumps(dict(proposal=identity(out / 'proposal.json'), independentSources=len(frozen),
        jobs=len(jobs), reusedObjects=1+len(slot_objects)+len(round_objects), compilerInvoked=False), indent=2))


if __name__ == '__main__':
    main()
