# SPDX-License-Identifier: GPL-3.0-or-later
"""Build x86 player-action bindings and validate pinned actual character models."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MODELS = (
    ('urban', '5a369b9657705a5283a918685ead66ee3ffed78bb463d18713cda0a3aa27dc8c'),
    # Exact authorized unit-repair deployment identities; see character-unit-
    # baseline-change.json in private model evidence, never disable SHA checks.
    ('csol95', '20c9be8ba35270d6649ea00280a6d54c78965fb4bda6c9a2e885ad5e25bc04ca'),
    ('csol168', '1fa7b00afdcb043f5ab4846a01e1c19ce37f0ec02155887cfc6e9ce1b1d2ae9b'),
)


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--model-root', type=Path,
                        default=ROOT / 'sandbox/cs-server/Half-Life/cstrike/models/player')
    args = parser.parse_args()
    candidate = args.output.absolute()
    for part in (candidate, *candidate.parents):
        if part.is_symlink() or (hasattr(os.path, 'isjunction') and os.path.isjunction(part)):
            raise ValueError('Output paths must not traverse links or junctions')
    output = candidate.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a new directory under workspace build')
    models = [args.model_root / name / (name + '.mdl') for name, _ in MODELS]
    for model, (_, expected) in zip(models, MODELS):
        if sha(model) != expected:
            raise ValueError('Required actual model identity differs: ' + str(model))
    sources = [Path(__file__).resolve(), ROOT / 'tests/cryoffear-actions/main.cpp',
               ROOT / 'native/guest/cof/player_actions.cpp', ROOT / 'native/guest/cof/player_actions.hpp',
               ROOT / 'external/ReGameDLL_CS/regamedll/engine/studio.h',
               ROOT / 'external/ReGameDLL_CS/regamedll/common/studio_event.h',
               ROOT / 'external/ReGameDLL_CS/regamedll/dlls/animation.cpp',
               ROOT / 'external/ReGameDLL_CS/regamedll/public/strtools.h']

    def identities():
        return [dict(path=p.relative_to(ROOT).as_posix() if p.is_relative_to(ROOT) else str(p), sha256=sha(p))
                for p in sources + models]

    before = identities()
    revision = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    environment = os.environ | json.loads(args.environment.read_text('utf-8-sig'))
    output.mkdir(parents=True)
    temporary = output / 'temp'
    temporary.mkdir()
    environment.update(TEMP=str(temporary), TMP=str(temporary), TMPDIR=str(temporary))
    executable = output / 'cof-actions.exe'
    command = [str(args.compiler), '/nologo', '--target=i686-pc-windows-msvc', '/std:c++20',
               '/EHsc', '/MT', '/utf-8', '/O2', '/clang:-O3', '/clang:-flto=thin',
               '/clang:-fuse-ld=lld', '/arch:AVX2', '/fp:precise', '/I' + str(ROOT),
               '/I' + str(ROOT / 'external/ReGameDLL_CS/regamedll/common'),
               str(sources[1]), str(sources[2]), '/Fe:' + str(executable),
               '/link', '/machine:x86', '/threads:1']
    compiled = subprocess.run(command, cwd=output, env=environment, capture_output=True,
                              text=True, encoding='utf-8', errors='replace', timeout=120)
    (output / 'compile.log').write_text(compiled.stdout + compiled.stderr, 'utf-8')
    tested = None
    if compiled.returncode == 0:
        tested = subprocess.run([str(executable), *map(str, models)], cwd=output, capture_output=True,
                                text=True, encoding='utf-8', errors='replace', timeout=30)
        (output / 'test.log').write_text(tested.stdout + tested.stderr, 'utf-8')
    after = identities()
    success = compiled.returncode == 0 and tested is not None and tested.returncode == 0 and before == after
    receipt = dict(revision=revision, command=command, compilerSha256=sha(args.compiler),
                   environmentSha256=sha(args.environment), compiled=compiled.returncode,
                   tested=tested.returncode if tested is not None else None,
                   before=before, after=after, unchanged=before == after, sourceCopies=0, success=success,
                   boundary='Custom host action binding only; no runtime wiring, gameplay, grip or live performance acceptance')
    (output / 'receipt.json').write_text(json.dumps(receipt, indent=2), 'utf-8')
    print((tested.stdout + tested.stderr) if tested is not None else (compiled.stdout + compiled.stderr))
    raise SystemExit(0 if success else 1)


if __name__ == '__main__':
    main()
