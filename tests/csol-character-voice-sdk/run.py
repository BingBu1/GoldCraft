#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the in-place emotion event fixture with the actual client's SDK flags."""
import argparse, ctypes, hashlib, json, os, subprocess
from pathlib import Path


def identity(path):
    path = Path(path).resolve()
    return {'path': str(path), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands', type=Path, required=True)
    parser.add_argument('--environment', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--sentences', type=Path, required=True)
    parser.add_argument('--oracle', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    rows = json.loads(args.compile_commands.read_text('utf-8-sig'))
    row = next(r for r in rows if Path(r['file']).name == 'emotion_voice.cpp')
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
    split.restype = ctypes.POINTER(ctypes.c_wchar_p)
    count = ctypes.c_int()
    ptr = split(row['command'], ctypes.byref(count))
    if not ptr:
        raise ctypes.WinError()
    try:
        original = [ptr[i] for i in range(count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ctypes.cast(ptr, ctypes.c_void_p))
    common = []
    for arg in original:
        if arg == '--':
            break
        if arg == '-c' or arg.startswith(('/Fo', '/Fd')):
            continue
        common.append(arg)
    env = dict(os.environ)
    env.update(json.loads(args.environment.read_text('utf-8-sig')))
    paths = [root/'native/csol/character_voice.hpp', root/'native/csol/client/emotion_voice.cpp',
             root/'native/csol/client/emotion_voice.hpp', root/'tests/csol-character-voice-sdk/client.cpp',
             Path(__file__), args.sentences, args.oracle]
    before = [identity(p) for p in paths]
    commands = [
        common + ['/c', str(root/'tests/csol-character-voice-sdk/client.cpp'),
                  '/Fo'+str(out/'fixture.obj'), '/Fd'+str(out/'fixture.pdb')],
        [original[0], '--target=i686-pc-windows-msvc', '-fuse-ld=lld', '/MT',
         str(out/'fixture.obj'), '/Fe'+str(out/'client.exe'), '/link', '/OPT:REF', '/threads:1'],
        [str(out/'client.exe'), str(args.sentences.resolve()), str(args.oracle.resolve())]]
    result = {'inputs': before, 'commands': commands, 'steps': [], 'success': False}
    try:
        for label, command in zip(('compile', 'link', 'test'), commands):
            with (out/(label+'.log')).open('w') as log:
                run = subprocess.run(command, cwd=row['directory'], env=env,
                                     stdout=log, stderr=subprocess.STDOUT, timeout=180)
            result['steps'].append({'name': label, 'exit': run.returncode})
            print(label, run.returncode, flush=True)
            if run.returncode:
                break
        result['success'] = len(result['steps']) == 3 and all(s['exit'] == 0 for s in result['steps'])
    finally:
        result['unchanged'] = before == [identity(p) for p in paths]
        result['success'] &= result['unchanged']
        (out/'result.json').write_text(json.dumps(result, indent=2), encoding='utf8')
    return 0 if result['success'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
