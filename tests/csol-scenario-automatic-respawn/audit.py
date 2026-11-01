"""Record frozen inputs, actual compiler dependencies and x86 executable identity."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('build', type=Path)
args = parser.parse_args()
build = args.build.resolve()


def identity(path):
    return {'path': str(path), 'bytes': path.stat().st_size,
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


inputs = json.loads((build / 'inputs.json').read_text('utf-8'))
frozen_mismatch = []
source_changes = []
for item in inputs:
    frozen = identity(Path(item['frozen']))
    original = identity(Path(item['source']))
    if frozen['sha256'] != item['sha256']:
        frozen_mismatch.append(frozen)
    if original['sha256'] != item['sha256']:
        source_changes.append(original)
cache = (build / 'out/CMakeCache.txt').read_text('utf-8')
ninja = re.search(r'^CMAKE_MAKE_PROGRAM:FILEPATH=(.+)$', cache, re.M).group(1)
result = subprocess.run([ninja, '-C', str(build / 'out'), '-t', 'deps'],
                        check=True, capture_output=True, text=True)
(build / 'actual-dependencies.txt').write_text(result.stdout, encoding='utf-8')
dependencies = []
for line in result.stdout.splitlines():
    if not line.startswith('    '):
        continue
    path = Path(line.strip())
    if not path.is_absolute():
        path = build / 'out' / path
    path = path.resolve()
    dependencies.append(identity(path))
assert dependencies and not frozen_mismatch
commands = json.loads((build / 'out/compile_commands.json').read_text('utf-8'))
assert len(commands) == 1
command = commands[0]['command']
for flag in ('--target=i686-pc-windows-msvc', '-std:c++20', '/clang:-O3',
             '/clang:-flto=thin', '/arch:AVX2', '/fp:precise'):
    assert flag in command, flag
executable = build / 'out/csol_scenario_automatic_respawn_tests.exe'
data = executable.read_bytes()
pe = struct.unpack_from('<I', data, 0x3c)[0]
assert data[pe:pe+4] == b'PE\0\0'
machine = struct.unpack_from('<H', data, pe+4)[0]
assert machine == 0x14c
run = (build / 'run.log').read_text('utf-8-sig')
assert '0 failures;' in run
record = {'frozenInputs': len(inputs), 'frozenMismatch': frozen_mismatch,
          'currentSourceChangesSinceFreeze': source_changes,
          'actualDependencies': dependencies, 'compileCommand': command,
          'executable': identity(executable), 'machine': 'x86',
          'logs': [identity(build / name) for name in ('configure.log', 'build.log', 'run.log')],
          'result': run.strip(), 'fullSdkOrLiveGame': False}
(build / 'audit.json').write_text(json.dumps(record, indent=2), encoding='utf-8')
print(json.dumps({key: record[key] for key in ('frozenInputs', 'frozenMismatch', 'result')} |
                 {'dependencies': len(dependencies), 'changedCurrentSources': len(source_changes),
                  'executableSha256': record['executable']['sha256']}))
