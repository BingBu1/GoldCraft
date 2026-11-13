# SPDX-License-Identifier: GPL-3.0-or-later
"""Independent actual Glock emergency adapter SDK; no full DLL or deployment."""
import argparse, importlib.util, json, os, re, subprocess, time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--seal', type=Path, required=True)
p.add_argument('--lock', type=Path, required=True)
p.add_argument('--host-object', type=Path, required=True)
p.add_argument('--adapter-dir', type=Path, required=True)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
spec = importlib.util.spec_from_file_location('sdk', ROOT / 'tests/csol-rebuy/run_sdk.py')
sdk = importlib.util.module_from_spec(spec); spec.loader.exec_module(sdk)
seal, lock = sdk.read(a.seal), sdk.read(a.lock)
out = sdk.checked(a.output, exists=False)
assert out.is_relative_to(ROOT / 'build') and not out.exists()
support = [x for x in lock['support'] if Path(x['path']).name.casefold() != 'guest_cof_glock_weapon.obj']
assert len(support) == 429
controls = [sdk.identity(x) for x in (a.seal, a.lock, a.host_object, Path(__file__),
    a.adapter_dir / 'engine_edge_names.inc', a.adapter_dir / 'engine_edge_defaults.inc')]
inputs = {x['path'].casefold(): x for x in controls}
env = os.environ.copy()
for key, value in sdk.read(seal['environment']['path']).items():
    for old in list(env):
        if old.casefold() == key.casefold(): del env[old]
    env[key] = value
for row in support + controls + [seal['compiler'], seal['linker'], seal['environment']]: sdk.verify(row)
out.mkdir()
receipt = dict(success=False, fullDLL=False, sourceCopies=0, deployed=False, gameplay=False,
    livePerformance=False, support=support, controls=controls, steps=[], products=[],
    boundary='Actual Glock over full SDK world; controlled overlay ownership, model/effect transport; generic kick integration is separate')

def save():
    receipt['inputsBefore'] = list(inputs.values())
    (out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')

def verify():
    for row in support + list(inputs.values()) + receipt['products']: sdk.verify(row)

def run(name, command, compile=False, link=False):
    verify(); receipt['activeStep'] = name; save(); started = time.monotonic()
    job = command
    if compile or link:
        rsp = out / (name + '.rsp'); rsp.write_text(subprocess.list2cmdline(command[1:]), encoding='utf8')
        job = [command[0], '@' + str(rsp)]
    q = subprocess.run(job, cwd=sdk.SDK_CWD, env=env, capture_output=True, timeout=1200)
    text = (q.stdout + q.stderr).decode('utf8', 'replace')
    (out / (name + '.log')).write_bytes(q.stdout + q.stderr)
    row = dict(name=name, command=command, exit=q.returncode, seconds=time.monotonic() - started)
    receipt['steps'].append(row)
    if compile and q.returncode == 0:
        for path in re.findall(r'Note: including file:\s*([^\r\n]+)', text):
            identity = sdk.identity(sdk.SDK_CWD / path.strip())
            previous = inputs.setdefault(identity['path'].casefold(), identity)
            assert previous == identity, 'header changed between compiler invocations'
    if q.returncode == 0 and (compile or link):
        prefix = '/fo' if compile else '/out:'
        receipt['products'].append(sdk.identity(next(x[len(prefix):] for x in command if x.lower().startswith(prefix))))
    if not compile and not link:
        result = re.findall(r'^behaviorChecks=(\d+) checks=(\d+) failures=(\d+)\s*$', text, re.M)
        assert len(result) == 1
        row['behaviorChecks'], row['sdkChecks'], row['failures'] = map(int, result[0])
        assert row['behaviorChecks'] > 0 and row['sdkChecks'] > 0
    receipt.pop('activeStep', None); save(); print(name, q.returncode, flush=True)
    if q.returncode or not (compile or link): print(text[-10000:], flush=True)
    assert q.returncode == 0, name + ' failed'

try:
    base = seal['compileCommand']
    flags = [x for x in base if not x.lower().endswith('.cpp') and not x.lower().startswith(('/fo', '/fp', '/yu', '/yc', '/fd'))]
    flags += ['/fp:precise', '/showIncludes', '/clang:-ffp-contract=off', '/I' + str(ROOT),
        '/I' + str(a.adapter_dir.resolve()), '/I' + str(ROOT / 'external/MetaHookSv-20261007/MetaHook/thirdparty/rapidjson/include')]
    for name, path in [('fixture', ROOT / 'tests/csol-cof-kick/emergency.cpp'),
                       ('glock', ROOT / 'native/guest/cof/server/glock_weapon.cpp')]:
        identity = sdk.identity(path); inputs[identity['path'].casefold()] = identity
        run('compile-' + name, flags + ['/Fo' + str(out / (name + '.obj')), str(path)], compile=True)
    remove = ('/out:', '/pdb:', '/implib:', '/def:', '/ltcgout:', '/manifest', '/natvis:', '/subsystem:', '/threads:', '/opt:lldltojobs=', '/lldltocache:')
    names = {'csol_kick_human_kick.obj', 'guest_cof_glock_weapon.obj'}
    link = [x for x in seal['linkCommand'] if x.lower() != '/dll' and not x.lower().startswith(remove) and Path(x).name.casefold() not in names]
    exe = out / 'emergency.exe'
    run('link', link + ['/SUBSYSTEM:CONSOLE', '/OUT:' + str(exe), '/PDB:' + str(out / 'emergency.pdb'),
        '/threads:2', '/opt:lldltojobs=2', str(a.host_object.resolve()), str(out / 'fixture.obj'), str(out / 'glock.obj')], link=True)
    for group in ('reload-pending', 'reload-elapsed', 'aim-attack', 'callbacks', 'cancel', 'life-undo',
                  'observer-retry', 'loaded-attack'):
        run(group, [str(exe), group])
    verify(); receipt['success'] = receipt['inputsUnchanged'] = True
except Exception as error:
    receipt['error'] = str(error); raise
finally: save()
