#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Check the actual server-package music closure with its production reader."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--reader-test', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    out = args.output.resolve()
    if out.exists() or not out.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace output')
    module_path = ROOT / 'tools/csol_server_table_closure.py'
    spec = importlib.util.spec_from_file_location('closure', module_path)
    closure = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(closure)
    prior = json.loads((args.reader_test / 'receipt.json').read_text('utf8'))
    assert prior['success'] and prior['inputsUnchanged']
    reader = Path(prior['reader']['path'])
    assert hashlib.sha256(reader.read_bytes()).hexdigest() == prior['reader']['sha256']
    out.mkdir(parents=True)
    source = closure.SourceTree(ROOT)
    evidence = {str(path): closure.identity(path) for path in (module_path, Path(__file__))}
    common = args.reader_test / 'common'
    base = closure.music_paths(source, common, 'music_fixture', reader, evidence)
    assert 'bgm/Play_BGM_Rex.mp3' not in base
    original = json.loads((args.reader_test / 'gameplay.json').read_text('utf8'))
    config = out / 'gameplay.json'
    checks = 1
    for enabled in (False, True):
        original['features']['time_cycle']['enabled'] = enabled
        config.write_text(json.dumps(original, ensure_ascii=False), 'utf8')
        current_evidence = dict(evidence)
        paths = closure.music_paths(source, common, 'music_fixture', reader, current_evidence, config)
        assert paths == (base | {'bgm/Play_BGM_Rex.mp3','bgm/BGM_Rex_loop.mp3','bgm/Zsh_nightbgm.mp3','bgm/BGM_zombie_attack.mp3'} if enabled else base)
        assert str(config.absolute()) in current_evidence
        checks += 2
    config.write_text('{"version":99}', 'utf8')
    rejected = False
    try:
        closure.music_paths(source, common, 'music_fixture', reader, dict(evidence), config)
    except subprocess.CalledProcessError:
        rejected = True
    assert rejected
    checks += 1
    for row in evidence.values():
        assert closure.identity(Path(row['path'])) == row
    result = dict(success=True, inputsUnchanged=True, checks=checks,
                  inputs=list(evidence.values()), reader=prior['reader'], deployed=False,
                  boundary='Actual server-package closure and production config reader over explicit map fixtures')
    (out / 'receipt.json').write_text(json.dumps(result, indent=2), 'utf8')
    print(json.dumps(dict(success=True, inputsUnchanged=True, checks=checks)))


if __name__ == '__main__':
    main()
