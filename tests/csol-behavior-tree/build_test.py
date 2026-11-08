"""Build the pure behavior-tree test with the configured x86 clang-cl and run it
against the original-instruction oracle output.

usage: python -I build_test.py <out-dir> <behavior-tree-oracle.txt>
"""
import json, subprocess, sys
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = Path(sys.argv[1]).resolve(); oracle = Path(sys.argv[2]).resolve()
out.mkdir(parents=True, exist_ok=True)
tool = json.loads((root / '.tools/clang-toolchain.json').read_text(encoding='utf-8'))
clang = Path(tool['root']) / 'bin' / 'clang-cl.exe'
vcvars = Path(r'C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvars32.bat')
source = root / 'tests' / 'csol-behavior-tree' / 'main.cpp'
exe = out / 'csol-behavior-tree.exe'
lines = [
    '@echo off',
    f'call "{vcvars}" >nul || exit /b 1',
    f'"{clang}" --target=i686-pc-windows-msvc /nologo /std:c++20 /EHsc /O2 /fp:precise /arch:AVX2 /W4 '
    f'"{source}" /Fo"{out}{chr(92)}{chr(92)}" /Fe"{exe}" /link /INCREMENTAL:NO',
]
script = out / 'build.cmd'
script.write_bytes(('\r\n'.join(lines) + '\r\n').encode('utf-8'))
build = subprocess.run(['cmd', '/d', '/c', str(script)], capture_output=True, text=True)
(out / 'build.log').write_text(build.stdout + build.stderr, encoding='utf-8')
if build.returncode:
    print(build.stdout, build.stderr); sys.exit(build.returncode)
run = subprocess.run([str(exe), str(oracle)], capture_output=True, text=True)
(out / 'run.log').write_text(run.stdout + run.stderr, encoding='utf-8')
print(run.stdout.strip().splitlines()[-1] if run.stdout.strip() else run.stderr, 'exit', run.returncode)
sys.exit(run.returncode)
