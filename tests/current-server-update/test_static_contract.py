"""Exercise the actual publisher preflight with Windows PowerShell 5.1."""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def main(output):
    output = output.resolve()
    if output.exists() or not output.is_relative_to(ROOT / 'build'):
        raise ValueError('Use a fresh workspace build directory')
    output.mkdir(parents=True)
    runtime = output / 'runtime'
    contents = {
        'hw.dll': b'engine fixture',
        'cstrike/cl_dlls/client.dll': b'client fixture',
        'cstrike/metahook/plugins/CSOLSupport.dll': b'new core',
        'cstrike/metahook/gamedata/current/index.json': b'catalog index',
        'cstrike/metahook/gamedata/current/symbols.json': b'catalog symbols',
        'cstrike/renderer/shader/current.glsl': b'shader fixture',
    }
    digest = lambda data: hashlib.sha256(data).hexdigest()
    for name, data in contents.items():
        dest = runtime / name
        dest.parent.mkdir(parents=True, exist_ok=True)
        dest.write_bytes(data)
    core = 'cstrike/metahook/plugins/CSOLSupport.dll'
    cfg = dict(schema=1, bundleId='fixture-bundle', corePaths=[core],
               engineSha256=digest(contents['hw.dll']),
               clientSha256=digest(contents['cstrike/cl_dlls/client.dll']),
               files=[dict(path=n, bytes=len(d), sha256=digest(d)) for n, d in contents.items()
                      if n not in ('hw.dll', 'cstrike/cl_dlls/client.dll')])
    cfg['files'][0]['sha256'] = digest(b'old core')
    release = dict(bundleId=cfg['bundleId'], files=[dict(path=core)])
    bundle = output / 'bundle.json'
    baseline = output / 'baseline.json'
    bundle.write_text(json.dumps(cfg), 'utf-8')
    baseline.write_text(json.dumps(release), 'utf-8')
    runner = output / 'invoke.ps1'
    runner.write_text('''param($Project,$Bundle,$Runtime,$Baseline)
$ErrorActionPreference='Stop'
. (Join-Path $Project 'tools/scenario-update/Files.ps1')
. (Join-Path $Project 'tools/current-server-update/StaticContract.ps1')
Assert-LauncherStaticContract $Bundle $Runtime (Get-Content -LiteralPath $Baseline -Raw|ConvertFrom-Json)
Write-Output 'contract accepted'
''', 'utf-8')
    checks = []

    def run(label, expected=None):
        result = subprocess.run(['powershell.exe', '-NoProfile', '-File', str(runner),
                                 '-Project', str(ROOT), '-Bundle', str(bundle),
                                 '-Runtime', str(runtime), '-Baseline', str(baseline)],
                                capture_output=True, timeout=30)
        text = (result.stdout + result.stderr).decode(errors='replace')
        passed = result.returncode == 0 if expected is None else result.returncode != 0 and expected in text
        checks.append(dict(name=label, passed=passed, exitCode=result.returncode))
        (output / f'call-{len(checks):02}.log').write_text(text, 'utf-8')
        (output / 'receipt.json').write_text(json.dumps(dict(passed=all(c['passed'] for c in checks), checks=checks), indent=2), 'utf-8')
        if not passed:
            raise AssertionError(label + ': ' + text)

    run('unchanged static files allow changed core DLL')
    catalog = runtime / 'cstrike/metahook/gamedata/current/symbols.json'
    old = catalog.read_bytes()
    catalog.write_bytes(b'new CRC symbol')
    run('changed catalog rejects DLL-only release', 'Static launcher resource changed')
    catalog.unlink()
    run('missing catalog rejects release', 'Static launcher resource changed')
    catalog.write_bytes(old)
    extra = catalog.with_name('new.json')
    extra.write_bytes(b'new catalog')
    run('new catalog rejects stale static bundle', 'New launcher symbol catalog')
    extra.unlink()
    shader = runtime / 'cstrike/renderer/shader/current.glsl'
    shader.write_bytes(b'changed shader')
    run('shader change also rejects release', 'Static launcher resource changed')
    shader.write_bytes(contents['cstrike/renderer/shader/current.glsl'])
    (runtime / 'hw.dll').write_bytes(b'other engine')
    run('wrong engine rejects catalog application', 'Launcher base-game identity mismatch')
    (runtime / 'hw.dll').write_bytes(contents['hw.dll'])
    release['bundleId'] = 'wrong-bundle'
    baseline.write_text(json.dumps(release), 'utf-8')
    run('wrong bundle rejects release', 'Launcher bundle does not match')
    release['bundleId'] = cfg['bundleId']
    release['files'].append(dict(path='cstrike/metahook/plugins/Unknown.dll'))
    baseline.write_text(json.dumps(release), 'utf-8')
    run('changed core allowlist rejects release', 'Launcher core contract differs')
    release['files'].pop()
    baseline.write_text(json.dumps(release), 'utf-8')
    cfg['files'].append(dict(path='../escape.json', bytes=1, sha256=digest(b'x')))
    bundle.write_text(json.dumps(cfg), 'utf-8')
    run('traversal rejected before file access', 'Invalid package path')
    cfg['files'].pop()
    bundle.write_text(json.dumps(cfg), 'utf-8')
    run('restored static contract passes')
    print(json.dumps(dict(passed=True, checks=len(checks))))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    main(parser.parse_args().output)
