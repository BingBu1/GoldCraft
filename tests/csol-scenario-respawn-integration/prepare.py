"""Freeze actual revival writers and failure transition for a sequential SDK check."""
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


def read(path):
    return json.loads(path.read_text('utf-8-sig'))


def save(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def key(path):
    return os.path.normcase(str(Path(path).resolve()))


def identity(path):
    path = path.resolve()
    with path.open('rb') as stream:
        sha = hashlib.file_digest(stream, 'sha256').hexdigest()
    return dict(path=str(path), bytes=path.stat().st_size, sha256=sha)


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


def extract(text, token):
    """Return a complete unchanged C++ body; quoted braces/comments are skipped."""
    assert text.count(token) == 1, token
    start = text.index(token)
    opening = text.index('{', start)
    depth = 0
    state = 'code'
    at = opening
    while at < len(text):
        char = text[at]
        following = text[at:at + 2]
        if state == 'line':
            if char == '\n':
                state = 'code'
        elif state == 'block':
            if following == '*/':
                state = 'code'
                at += 1
        elif state in ('"', "'"):
            if char == '\\':
                at += 1
            elif char == state:
                state = 'code'
        elif following in ('//', '/*'):
            state = 'line' if following == '//' else 'block'
            at += 1
        elif char in ('"', "'"):
            state = char
        elif char == '{':
            depth += 1
        elif char == '}':
            depth -= 1
            if depth == 0:
                return text[start:at + 1]
        at += 1
    raise ValueError('Unterminated actual C++ body: ' + token)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--only', choices=('retry_messages', 'precache'), help='Execute only the selected retry contract')
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    for path in (out, *out.parents):
        if path.is_symlink() or path.is_junction():
            raise ValueError('Reparse output path')
    out.mkdir(parents=True)
    source = out / 'source'
    test_root = source / 'tests/csol-scenario-respawn-integration'
    methods = ['Register', 'SendRoundOutcome', 'SendConsumableState', 'SendConsumableVisibility',
               'SendRespawnText', 'SendRespawnCountdown', 'SendRespawnUnavailable', 'SendRevivalHint',
               'SendRespawnReloadWait', 'SendRetryCountdown', 'SendRetryAnnouncement']
    producer = ROOT / 'native/csol/server/scenario/core/scenario_messages.cpp'
    rules = ROOT / 'native/csol/server/scenario/core/game_rules_gate.cpp'
    pending = [ROOT / ('tests/csol-scenario-respawn-integration/' + name) for name in
               ('messages.cpp', 'rules.cpp', 'retry_messages.cpp', 'precache.cpp', 'prepare.py')]
    # Production TUs are copied whole for provenance, but only unchanged named
    # method bodies execute. Their unrelated include closure is not compiled.
    frozen, copied = [], set()
    def copy(path):
        path = path.resolve()
        if path in copied:
            return
        assert path.is_relative_to(ROOT) and path.is_file() and not path.is_symlink() and not path.is_junction()
        target = source / path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        original = identity(path)
        shutil.copy2(path, target)
        current = identity(target)
        assert original['sha256'] == current['sha256'] and target.stat().st_nlink == 1 and not os.path.samefile(path, target)
        frozen.append(dict(relative=path.relative_to(ROOT).as_posix(), original=original, frozen=current))
        copied.add(path)
    while pending:
        path = pending.pop().resolve()
        if path in copied:
            continue
        copy(path)
        if path.suffix in ('.cpp', '.hpp', '.h'):
            for include in re.findall(r'^\s*#\s*include\s*"([^"\n]+)"', path.read_text('utf-8-sig'), re.M):
                if include in ('precompiled.h', 'actual_messages.inc', 'actual_rules.inc', 'actual_precache.inc'):
                    continue
                candidate = next((item for item in (path.parent / include, ROOT / include) if item.is_file()), None)
                assert candidate is not None, (str(path), include)
                pending.append(candidate)
    copy(producer)
    copy(rules)
    text = (source / producer.relative_to(ROOT)).read_text('utf-8-sig')
    blocks = [extract(text, 'namespace\n{')]
    blocks.extend(extract(text, 'bool CCSOLScenarioMessages::' + method + '(') for method in methods)
    message_inc = test_root / 'actual_messages.inc'
    message_inc.write_text('\n\n'.join(blocks) + '\n', encoding='utf-8')
    rules_inc = test_root / 'actual_rules.inc'
    rules_block = extract((source / rules.relative_to(ROOT)).read_text('utf-8-sig'), 'void ScenarioCheckWinConditions()')
    rules_inc.write_text(rules_block + '\n', encoding='utf-8')
    session = ROOT / 'native/csol/server/scenario/core/scenario_session.cpp'
    copy(session)
    session_text = (source / session.relative_to(ROOT)).read_text('utf-8-sig')
    precache_spans = []
    for first, following in (
        ('    for (const auto& image : csol::scenario_respawn_ui::Outcomes)', '    csol::scenario::DialogueResources dialogue;'),
        ('    for (const auto& path : hudPaths)', '    const std::string upgradeSound'),
    ):
        assert session_text.count(first) == session_text.count(following) == 1
        start, end = session_text.index(first), session_text.index(following)
        assert start < end
        block = session_text[start:end]
        precache_spans.append(dict(firstLine=session_text.count('\n', 0, start) + 1,
            lastLine=session_text.count('\n', 0, end), text=block,
            sha256=hashlib.sha256(block.encode()).hexdigest()))
    precache_inc = test_root / 'actual_precache.inc'
    precache_inc.write_text(''.join(span['text'] for span in precache_spans), encoding='utf-8')
    save(out / 'extraction.json', dict(producer=identity(source / producer.relative_to(ROOT)),
        rules=identity(source / rules.relative_to(ROOT)), methods=methods,
        blocks=[dict(sha256=hashlib.sha256(block.encode()).hexdigest(), bytes=len(block.encode())) for block in blocks],
        messageInclude=identity(message_inc), ruleInclude=identity(rules_inc),
        session=identity(source / session.relative_to(ROOT)), precacheInclude=identity(precache_inc),
        precacheSpans=[{name:value for name,value in span.items() if name != 'text'} for span in precache_spans],
        bodyRewrites=False))
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
    commands_path = BASE / 'revision-02/output/client/compile_commands.json'
    origin = next(row for row in read(commands_path) if row['file'].replace('\\', '/').endswith('/client/scenario_round_hud_client.cpp'))
    raw = split(origin['command'])
    compiler = raw.pop(0)
    # Keep target/optimizer/CRT/debug options. Server ABI includes and defines
    # come from the existing SDK message test, not client include search order.
    flags = []
    for arg in raw:
        if arg in ('-c', '--', origin['file']) or arg.startswith(('/Fo', '/Fd', '-I', '-D', '/D')):
            continue
        if arg == '/clang:' + str(BASE / 'source-overlay-final.json'):
            arg = '/clang:' + str(overlay)
        flags.append(arg)
    for required in ('--target=i686-pc-windows-msvc', '/clang:-O3', '/clang:-flto=thin', '/arch:AVX2', '/fp:precise', '-std:c++20', '-MT'):
        assert required in flags
    sdk = ROOT / 'external/ReGameDLL_CS/regamedll'
    includes = ['', 'hookers', 'common', 'dlls', 'engine', 'public', 'public/regamedll', 'pm_shared', 'regamedll', 'testsuite']
    definitions = 'REGAMEDLL_ADD REGAMEDLL_API REGAMEDLL_FIXES REGAMEDLL_SSE REGAMEDLL_SELF REGAMEDLL_CHECKS UNICODE_FIXES BUILD_LATEST BUILD_LATEST_FIXES CLIENT_WEAPONS USE_QSTRING _CRT_SECURE_NO_WARNINGS NOMINMAX'.split()
    sdk_flags = ['-I' + str(sdk / name) for name in includes] + ['-D' + name for name in definitions]
    output = out / 'output'
    linker = str(Path(compiler).with_name('lld-link.exe'))
    jobs = []
    groups = [('messages', 'respawn integration writer checks='), ('rules', 'respawn failure transition checks=')]
    if args.only:
        groups = [(args.only, 'scenario retry precache checks=' if args.only == 'precache' else 'retry integration writer checks=')]
    for name, marker in groups:
        obj = output / (name + '.obj')
        binary = output / (name + '.exe')
        jobs.append(dict(name='compile-' + name, kind='compile', seconds=180, expectExit=0,
            command=[compiler, *flags, *(sdk_flags if name in ('messages','retry_messages','precache') else []), '/showIncludes',
                '/Fo' + str(obj), '/Fd' + str(output / (name + '.compile.pdb')), '-c', '--', str(test_root / (name + '.cpp'))]))
        jobs.append(dict(name='link-' + name, kind='link', seconds=180, expectExit=0,
            command=[linker, '/nologo', '/subsystem:console', '/machine:x86', '/debug', '/incremental:no', '/opt:ref',
                '/opt:icf', '/opt:lldltojobs=1', '/threads:1', '/out:' + str(binary), '/pdb:' + str(output / (name + '.pdb')),
                str(obj), 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib', 'ole32.lib', 'advapi32.lib']))
        jobs.append(dict(name='run-' + name, kind='run', seconds=60, expectExit=0, marker=marker, command=[str(binary)]))
    runner = ROOT / 'build/csol-character-skills-completion-20261031/status-buy-narrow-20261101-02/run.py'
    shutil.copy2(runner, out / 'run.py')
    save(out / 'source-changes.json', frozen)
    direct = [row['frozen'] for row in frozen]
    direct.extend(identity(path) for path in (overlay, message_inc, rules_inc, precache_inc, out / 'run.py', out / 'extraction.json',
        out / 'source-changes.json', out / 'external-inputs.json', BASE / 'source-manifest-final.json', BASE / 'environment.json',
        commands_path, ROOT / 'tests/csol-scenario-messages/CMakeLists.txt'))
    save(out / 'proposal.json', dict(inputs=list({key(row['path']): row for row in direct}.values()),
        productionManifest=identity(BASE / 'source-manifest-final.json'), externalManifest=identity(out / 'external-inputs.json'),
        environment=identity(BASE / 'environment.json'), overlay=identity(overlay), jobs=jobs, maxSeconds=600,
        output=str(output), compiler=compiler, originalCompile=origin, compileFlags=flags, sdkFlags=sdk_flags,
        compiled=False, deployed=False))
    print(json.dumps(dict(proposal=identity(out / 'proposal.json'), independentSources=len(frozen),
        jobs=len(jobs), compilerInvoked=False), indent=2))


if __name__ == '__main__':
    main()
