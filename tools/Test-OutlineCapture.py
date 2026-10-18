"""Verify the actual outline accessor targets in development and production mappings."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, required=True)
parser.add_argument('--production-jar', type=Path, required=True)
parser.add_argument('--source-root', type=Path, default=ROOT)
parser.add_argument('--phase', choices=['all', 'transform', 'routing'], default='all')
args = parser.parse_args()
args.source_root = args.source_root.resolve()
args.production_jar = args.production_jar.resolve()
out = args.output.resolve()
if not out.is_relative_to(ROOT / 'build') or any(p.is_symlink() or p.is_junction() for p in (out, *out.parents)):
    raise ValueError('Use a plain workspace build directory')
out.mkdir(parents=True, exist_ok=True)
java = ROOT / '.tools/java/jdk-21.0.12.1+1/bin'
runtime = json.loads((ROOT / 'build/neoforge-runtime/runClient.json').read_text(encoding='utf-8-sig'))
game = next((ROOT / 'neoforge/.gradle/loom-cache/minecraftMaven/net/minecraft').glob(
    'neoforge-21.1.256-minecraft-merged-*/1.21.1-loom.mappings.*/neoforge-21.1.256-*-neoforge-21.1.256.jar'))
sources = [args.source_root / 'neoforge/src/client/java/dev/goldcraft/client/mixin' / (n + '.java')
           for n in ['RenderToggleAccessor', 'RenderParametersAccessor']]
fixtures = [ROOT / 'tests/entity-outline/OutlineTransform.java'] + [ROOT / 'tests/nightfall-blade' / n
            for n in ['BladeMixinService.java', 'BladeProperties.java']]
install = ROOT / '.tools/neoforge-runtime/21.1.256'
game_overlay = install / 'libraries/net/neoforged/neoforge/21.1.256/neoforge-21.1.256-client.jar'
manifests = [install / 'versions' / v / (v + '.json') for v in ['1.21.1', 'neoforge-21.1.256']]
dependencies = {}
for manifest in manifests:
    for lib in json.loads(manifest.read_text())['libraries']:
        rules = lib.get('rules', [])
        allowed = not rules
        for rule in rules:
            if rule.get('os', {}).get('name', 'windows') == 'windows':
                allowed = rule['action'] == 'allow'
        if not allowed:
            continue
        artifact = lib.get('downloads', {}).get('artifact')
        if artifact:
            key = lib['name'].split(':')
            dependencies[tuple(key[:2] + key[3:])] = install / 'libraries' / artifact['path']
libraries = list(dependencies.values())
base = install / 'libraries/net/minecraft/client/1.21.1-20240808.144430'
vanilla = base / 'client-1.21.1-20240808.144430-srg.jar'
libraries += [vanilla, base / 'client-1.21.1-20240808.144430-extra.jar']
assert all(p.is_file() for p in libraries), 'Incomplete pinned runtime'
runs = []


def run(name, exe, options):
    response = out / (name + '.args')
    response.write_text('\n'.join('"' + str(v).replace('\\', '/').replace('"', '\\"') + '"' for v in options))
    result = subprocess.run([str(java / exe), '@' + str(response)], cwd=out, capture_output=True, timeout=90)
    (out / (name + '.log')).write_bytes(result.stdout + result.stderr)
    runs.append(dict(name=name, exitCode=result.returncode))
    print(name, result.returncode, (result.stdout + result.stderr).decode(errors='replace')[-1600:])
    assert result.returncode == 0, name


for mode in (['development', 'production'] if args.phase != 'routing' else []):
    target = out / mode
    services = target / 'META-INF/services'
    services.mkdir(parents=True, exist_ok=True)
    (services / 'org.spongepowered.asm.service.IMixinService').write_text('probe.BladeMixinService\n')
    (services / 'org.spongepowered.asm.service.IGlobalPropertyService').write_text('probe.BladeProperties\n')
    (target / 'outline.mixins.json').write_text(json.dumps(dict(required=True, package='dev.goldcraft.client.mixin',
        compatibilityLevel='JAVA_21', client=['RenderToggleAccessor', 'RenderParametersAccessor'])))
    if mode == 'development':
        game_jar = game
        libs = [target, game, *runtime['classpath']]
        mixins = sources
    else:
        game_jar = game_overlay
        libs = [target, args.production_jar.resolve(), game_jar, *libraries]
        mixins = []
    cp = os.pathsep.join(map(str, libs))
    run('compile-' + mode, 'javac.exe', ['--release', '21', '-proc:none', '-cp', cp, '-d', target, *fixtures, *mixins])
    run('transform-' + mode, 'java.exe', ['-cp', cp, 'probe.OutlineTransform', game_jar, target / 'transformed', mode])
# Vanilla's real RenderType clinit bootstraps registries. Use the pinned vanilla
# runtime for this CPU semantic probe; NeoForge's patched accessors are verified
# above with real Sponge, without pretending a plain JVM is a full FML launch.
probe = ROOT / 'tests/entity-outline/OutlineRoutingProbe.java'
transport = ROOT / 'tests/entity-outline/OutlineTransportProbe.java'
if args.phase != 'transform':
    routing = out / 'routing'
    routing.mkdir(exist_ok=True)
    cp = os.pathsep.join(map(str, [routing, args.production_jar.resolve(), vanilla, *libraries]))
    run('compile-routing', 'javac.exe', ['--release', '21', '-proc:none', '-cp', cp, '-d', routing, probe, transport])
    run('routing', 'java.exe', ['-cp', cp, 'dev.goldcraft.client.OutlineRoutingProbe'])
    run('transport', 'java.exe', ['-cp', cp, 'dev.goldcraft.bridge.OutlineTransportProbe'])
(out / 'result.json').write_text(json.dumps(dict(passed=True, phase=args.phase, runs=runs,
    inputs={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in [*sources, *fixtures, probe, transport, vanilla, game_overlay, *manifests, args.production_jar]},
    boundary='Real Sponge/ASM against pinned NeoForge; actual vanilla layer routing with shipped provider/collector; no FML client startup or gameplay'), indent=2) + '\n')
