"""Freeze the real automatic-respawn helper and its independent test inputs."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
output = args.output.resolve()
if output.exists() or not output.is_relative_to(root / 'build'):
    raise ValueError('Use a fresh workspace build directory')
for parent in (output, *output.parents):
    if parent.is_symlink() or parent.is_junction():
        raise ValueError('Do not follow reparse paths')
inputs = ['native/csol/scenario/automatic_respawn.hpp',
          'tests/csol-scenario-automatic-respawn/main.cpp',
          'tests/csol-scenario-automatic-respawn/CMakeLists.txt',
          'cmake/clang-x86.cmake', 'cmake/cxx20.cmake']
records = []
for relative in inputs:
    source = root / relative
    target = output / 'source' / relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source, target)
    sha = hashlib.sha256(source.read_bytes()).hexdigest()
    if hashlib.sha256(target.read_bytes()).hexdigest() != sha:
        raise RuntimeError('Input changed during freeze: ' + relative)
    records.append({'source': str(source), 'frozen': str(target), 'sha256': sha})
(output / 'inputs.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
print(json.dumps({'output': str(output), 'frozenInputs': len(records)}))
