# SPDX-License-Identifier: GPL-3.0-or-later
"""Compile actual SDK adapter and run real-model tests at controlled engine edges."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SDK = ROOT / 'external/ReGameDLL_CS/regamedll'
MODELS = (
    ('urban', '5a369b9657705a5283a918685ead66ee3ffed78bb463d18713cda0a3aa27dc8c'),
    # Authorized all-character unit repair; exact before/after deployment
    # receipt is recorded in model-evidence/character-unit-baseline-change.json.
    ('csol95', '20c9be8ba35270d6649ea00280a6d54c78965fb4bda6c9a2e885ad5e25bc04ca'),
    ('csol168', '1fa7b00afdcb043f5ab4846a01e1c19ce37f0ec02155887cfc6e9ce1b1d2ae9b'),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def extract(path, signature, body_only=False):
    text = path.read_text('utf-8-sig')
    start = text.index(signature)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[brace + 1:end - 1] if body_only else text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sdk-baseline', type=Path, default=ROOT / 'build/teddy-ground-sdk-20261111-09/receipt.json')
    parser.add_argument('--model-root', type=Path, default=ROOT / 'sandbox/cs-server/Half-Life/cstrike/models/player')
    args = parser.parse_args()
    candidate = args.output.absolute()
    for path in (candidate, *candidate.parents):
        if path.is_symlink() or (hasattr(os.path, 'isjunction') and os.path.isjunction(path)):
            raise ValueError('Output path traverses link or junction')
    output = candidate.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a new workspace build directory')
    models = [args.model_root / name / (name + '.mdl') for name, _ in MODELS]
    for model, (_, expected) in zip(models, MODELS):
        if sha(model) != expected:
            raise ValueError('Actual model SHA differs: ' + str(model))
    test_root = Path(__file__).resolve().parent
    production = ROOT / 'native/guest/cof/server/player_animation.cpp'
    actions = ROOT / 'native/guest/cof/player_actions.cpp'
    sources = [*sorted(test_root.glob('*.*')), production, production.with_suffix('.hpp'), actions,
               actions.with_suffix('.hpp'), ROOT / 'native/guest/cof/scenario_damage.hpp',
               SDK / 'dlls/player.cpp', SDK / 'dlls/animation.cpp', SDK / 'dlls/animating.cpp',
               ROOT / 'native/csol/client/player_animation.hpp', args.sdk_baseline, *models]
    # Real SDK include closure is hashed before compilation, without snapshots.
    for directory in [SDK, ROOT / 'native/include', ROOT / 'native/csol']:
        sources += sorted(p for p in directory.rglob('*') if p.is_file() and p.suffix in ('.h', '.hpp', '.inl', '.inc'))
    sources = list(dict.fromkeys(p.resolve() for p in sources))

    def identities(paths):
        return [dict(path=p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else str(p), sha256=sha(p)) for p in paths]

    before = identities(sources)
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    output.mkdir(parents=True)
    temporary = output / 'temp'; temporary.mkdir()
    environment.update(TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary), VSLANG='1033')
    for name, source, signature, body_only in (
        ('sdk_sequence_info.inc', SDK / 'dlls/animation.cpp', 'void GetSequenceInfo(', False),
        ('sdk_sequence_flags.inc', SDK / 'dlls/animation.cpp', 'int GetSequenceFlags(', False),
        ('sdk_reset_sequence.inc', SDK / 'dlls/animating.cpp', 'void EXT_FUNC CBaseAnimating::__API_HOOK(ResetSequenceInfo)()', True),
    ):
        (output / name).write_text(extract(source, signature, body_only), 'utf-8')
    flags = ['/nologo', '--target=i686-pc-windows-msvc', '/std:c++20', '/EHsc', '/MT', '/utf-8', '/O2',
             '/clang:-O3', '/clang:-flto=thin', '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise',
             '/I' + str(ROOT), '/I' + str(SDK / 'common')]
    baseline = json.loads(args.sdk_baseline.read_text('utf-8-sig'))['steps'][0]['command']
    # Preserve matched SDK includes/defines; discard unrelated source/output,
    # old architecture, debug and PCH choices. No source copies are made.
    sdk_flags = []
    for i, token in enumerate(baseline):
        if token.startswith('/I'):
            sdk_flags.append(token)
        elif token == '/D':
            sdk_flags += ['/D', baseline[i + 1]]
        elif token.startswith('/D') and len(token) > 2:
            sdk_flags.append(token)
    sdk_command = [str(args.compiler), *flags, *sdk_flags, '/Y-', '/showIncludes', '/c',
                   str(production), '/Fo' + str(output / 'actual-sdk.obj')]
    steps = []

    def run(name, command, timeout):
        process = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                                 text=True, encoding='utf-8', errors='replace', timeout=timeout)
        text = process.stdout + process.stderr
        (output / (name + '.log')).write_text(text, 'utf-8')
        steps.append(dict(name=name, command=command, exit=process.returncode))
        return process, text

    compiled, sdk_text = run('actual-sdk', sdk_command, 120)
    sdk_includes = list(dict.fromkeys(Path(p.strip()).resolve() for p in re.findall(r'^\s*Note: including file:\s*(.+)$', sdk_text, re.M)))
    tracked = {str(p).lower() for p in sources}
    unknown = [str(p) for p in sdk_includes if str(p).lower() not in tracked and p.is_relative_to(ROOT) and
               not p.is_relative_to(ROOT / 'build')]
    generated_inputs = identities([p for p in sdk_includes if p.is_relative_to(ROOT / 'build')])
    tested = None
    if compiled.returncode == 0 and not unknown:
        executable = output / 'cof-player-animation.exe'
        command = [str(args.compiler), *flags, '/I' + str(test_root), '/I' + str(output),
                   str(test_root / 'main.cpp'), str(production), str(actions),
                   '/Fe:' + str(executable), '/link', '/machine:x86', '/threads:1']
        fixture, fixture_text = run('controlled-edge-build', command, 120)
        if fixture.returncode == 0:
            tested, test_text = run('test', [str(executable), *map(str, models)], 30)
    after = identities(sources)
    unchanged = before == after
    generated_unchanged = all(sha(ROOT / row['path']) == row['sha256'] for row in generated_inputs)
    success = (compiled.returncode == 0 and tested is not None and tested.returncode == 0 and
               unchanged and generated_unchanged and not unknown)
    receipt = dict(success=success, revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                   compilerSha256=sha(args.compiler), environmentSha256=sha(args.environment), steps=steps,
                   before=before, after=after, unchanged=unchanged, generatedIncludes=generated_inputs,
                   generatedIncludesUnchanged=generated_unchanged, unknown=unknown, sourceCopies=0,
                   actualSDKIncludes=[str(p) for p in sdk_includes],
                   boundary='Actual SDK TU compile plus actual model/SDK reset at controlled engine edges; no server/gameplay/grip/live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((output / (steps[-1]['name'] + '.log')).read_text('utf-8'))
    if unknown: print('Untracked workspace includes:', unknown)
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
