"""Exercise actual voxel cache retention and restore in an isolated NeoForge server."""
from pathlib import Path
from zipfile import ZipFile, ZIP_DEFLATED
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
parser = argparse.ArgumentParser()
parser.add_argument('--jar', type=Path, required=True)
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
work, jar = args.output.resolve(), args.jar.resolve()
if not work.is_relative_to(ROOT / 'build') or any(p.is_symlink() or p.is_junction() for p in [args.output, *args.output.parents]):
    raise ValueError('Test output must be an unlinked workspace build directory')
work.mkdir(parents=True, exist_ok=False)
java = ROOT / '.tools/java/jdk-21.0.12.1+1/bin/java.exe'
manifest = json.loads((ROOT / 'build/neoforge-production/runServer.json').read_text('utf-8'))
libs = ROOT / '.tools/neoforge-runtime/21.1.256/libraries'
game = libs / 'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-server.jar'
vanilla = libs / 'net/minecraft/server/1.21.1-20240808.144430/server-1.21.1-20240808.144430-srg.jar'
universal = libs / 'net/neoforged/neoforge/21.1.256/neoforge-21.1.256-universal.jar'
legacy = next(v.split('=', 1)[1] for v in manifest['jvm'] if v.startswith('-DlegacyClassPath='))
cp = ';'.join(map(str, [game, vanilla, universal, legacy, jar]))

def argfile(path, values):
    path.write_text('\n'.join('"' + str(v).replace('\\', '/').replace('"', '\\"') + '"' for v in values), 'utf-8')

classes = work / 'classes'
classes.mkdir()
probe = work / 'CarvedCacheProbe.java'
shutil.copyfile(ROOT / 'tests/carved-cache/CarvedCacheProbe.java', probe)
shutil.copyfile(ROOT / 'sandbox/cs-server/Half-Life/cstrike/maps/cs_assault.bsp', work / 'cs_assault.bsp')
argfile(work / 'javac.args', ['--release', '21', '-proc:none', '-encoding', 'UTF-8', '-cp', cp, '-d', classes, probe])
result = subprocess.run([str(java.with_name('javac.exe')), '@' + str(work / 'javac.args')], cwd=work, capture_output=True)
(work / 'compile.log').write_bytes(result.stdout + result.stderr)
if result.returncode:
    raise RuntimeError('Probe compilation failed: ' + str(work / 'compile.log'))
mods = work / 'mods'
mods.mkdir()
with ZipFile(mods / 'carved-cache-probe.jar', 'w', ZIP_DEFLATED) as archive:
    archive.writestr('META-INF/neoforge.mods.toml', 'modLoader="javafml"\nloaderVersion="[4,)"\nlicense="Test fixture"\n[[mods]]\nmodId="goldcraft_carved_cache_probe"\nversion="1.0.0"\n')
    for file in classes.rglob('*.class'):
        archive.write(file, file.relative_to(classes).as_posix())
shutil.copyfile(jar, mods / jar.name)
(work / 'eula.txt').write_text('eula=true\n', 'ascii')
(work / 'server.properties').write_text('server-ip=127.0.0.1\nserver-port=0\nonline-mode=true\nenable-rcon=false\nview-distance=2\nsimulation-distance=2\nlevel-type=minecraft:flat\ngenerate-structures=false\ngenerator-settings={"layers":[{"block":"minecraft:bedrock","height":1}],"biome":"minecraft:plains"}\n', 'ascii')
(work / 'tmp').mkdir()
classpath = ['-cp', ';'.join(manifest['classpath'])] if manifest['classpath'] else []
argfile(work / 'server.args', ['-Xms256m', '-Xmx1536m', *[v for v in manifest['jvm'] if not v.startswith(('-Xmx', '-Xms'))],
    f'-Djava.io.tmpdir={work / "tmp"}', *classpath, manifest['main'], *manifest['args']])
env = {k: v for k, v in os.environ.items() if not k.startswith('GOLDCRAFT_')}
for key in manifest.get('clearEnvironment', []):
    env.pop(key, None)
env.update(manifest.get('environment', {}))
with (work / 'runtime.log').open('wb') as log:
    process = subprocess.Popen([str(java), '@' + str(work / 'server.args')], cwd=work, env=env,
        stdin=subprocess.PIPE, stdout=log, stderr=subprocess.STDOUT, creationflags=subprocess.CREATE_NO_WINDOW)
    print(json.dumps({'pid': process.pid, 'fixture': str(work)}), flush=True)
    try:
        deadline = time.monotonic() + 150
        while process.poll() is None and not (work / 'carved-cache.json').exists():
            if time.monotonic() > deadline:
                raise TimeoutError('Carved cache fixture timed out')
            time.sleep(.25)
        if process.poll() is None:
            process.stdin.write(b'stop\n')
            process.stdin.flush()
        code = process.wait(timeout=30)
    except BaseException:
        process.terminate()
        process.wait(timeout=15)
        raise
report = json.loads((work / 'carved-cache.json').read_text('utf-8'))
report.update(exitCode=code, jarSha256=hashlib.sha256(jar.read_bytes()).hexdigest(),
    probeSha256=hashlib.sha256(probe.read_bytes()).hexdigest(),
    boundary='Real NeoForge and production voxel cache; synthetic authoritative edits, no graphical gameplay.')
(work / 'result.json').write_text(json.dumps(report, indent=2) + '\n', 'utf-8')
print(json.dumps(report, indent=2))
if code or not report['passed']:
    raise SystemExit(1)
