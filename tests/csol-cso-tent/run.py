"""Build the actual CsoMonFX client consumer against a configured client SDK.

Run from Git Bash with a current --client-build and captured --environment.
No production source is copied. Outputs and concise receipts stay in build/.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser()
p.add_argument('output', type=Path)
p.add_argument('--client-build', type=Path, required=True)
p.add_argument('--environment', type=Path, required=True)
a = p.parse_args()
out = a.output.resolve()
if not out.is_relative_to(ROOT / 'build') or out.exists():
    raise SystemExit('A fresh output directory under build is required')
basis = a.client_build.resolve()
text = (basis / 'build.ninja').read_text(encoding='utf-8')
block = next(b for b in text.split('\n\n') if 'build CMakeFiles' in b and 'cso_tent.cpp.obj:' in b)
rules = (basis / 'CMakeFiles/rules.ninja').read_text(encoding='utf-8')
compiler = re.search(r'command = .*?([^\s{}]+clang-cl.exe)', rules).group(1)
# Parse native Windows quoting without interpreting backslashes as escapes.
parse = ctypes.windll.shell32.CommandLineToArgvW
parse.argtypes = [ctypes.c_wchar_p, ctypes.POINTER(ctypes.c_int)]
parse.restype = ctypes.POINTER(ctypes.c_wchar_p)
ctypes.windll.kernel32.LocalFree.argtypes = [ctypes.c_void_p]
def split(value):
    count = ctypes.c_int()
    ptr = parse('program ' + value, ctypes.byref(count))
    if not ptr:
        raise OSError('CommandLineToArgvW failed')
    try:
        return [ptr[i] for i in range(1, count.value)]
    finally:
        ctypes.windll.kernel32.LocalFree(ptr)
flags = []
for key in ('DEFINES', 'INCLUDES', 'FLAGS'):
    flags += split(re.search(r'^  ' + key + r' = (.+)$', block, re.M).group(1).strip())
env = os.environ.copy()
saved = json.loads(a.environment.read_text(encoding='utf-8-sig'))
for key in ('INCLUDE', 'LIB', 'LIBPATH'):
    if key in saved:
        env[key] = saved[key]
sources = [ROOT / 'tests/csol-cso-tent/main.cpp', ROOT / 'native/csol/client/cso_tent.cpp']
inputs = sources + [ROOT / 'native/csol/cso_tent.hpp', ROOT / 'native/csol/client/cso_tent.hpp',
                   ROOT / 'native/csol/client/runtime.hpp', Path(__file__).resolve()]
def hashes():
    return {str(f): hashlib.sha256(f.read_bytes()).hexdigest() for f in inputs}
before = hashes()
out.mkdir(parents=True)
exe = out / 'cso-tent.exe'
command = [compiler, '/nologo', '-fuse-ld=lld', *flags, *map(str, sources), '/Fe:' + str(exe),
           '/Fo:' + str(out) + os.sep, '/link', '/threads:2']
result = dict(command=command, sourceInputs=before, clientBuild=str(basis))
try:
    build = subprocess.run(command, cwd=out, env=env, capture_output=True, text=True, errors='replace', timeout=120)
    (out / 'compile.log').write_text(build.stdout + build.stderr, encoding='utf-8')
    result['compileExit'] = build.returncode
    if build.returncode == 0:
        run = subprocess.run([str(exe)], cwd=out, capture_output=True, text=True, errors='replace', timeout=30)
        result.update(runExit=run.returncode, output=run.stdout + run.stderr,
                      executableSHA256=hashlib.sha256(exe.read_bytes()).hexdigest())
    else:
        result['output'] = (build.stdout + build.stderr)[-6000:]
finally:
    result['inputsUnchanged'] = before == hashes()
    result['success'] = result.get('compileExit') == 0 and result.get('runExit') == 0 and result['inputsUnchanged']
    (out / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
print(json.dumps({k: result[k] for k in ('success', 'inputsUnchanged', 'output') if k in result}))
raise SystemExit(0 if result['success'] else 1)
