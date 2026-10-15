"""Prepare/build the pinned official Avalonia UI with the GoldCraft integration.

Requires a .NET SDK and the complete pinned MetahookInstaller source checkout.
All generated source, NuGet dependencies and outputs stay below --work.
Without --build this only verifies inputs and applies the source adaptation.
"""
from pathlib import Path
import argparse
import json
import os
import shutil
import subprocess
import time
from package import HERE, digest, safe


def prepare(upstream, work):
    upstream, work = safe(upstream), safe(work)
    pin = json.loads((HERE / 'gui-source.json').read_text('utf-8'))
    if work.exists() or upstream.is_relative_to(work) or work.is_relative_to(upstream):
        raise ValueError('Use a new independent work directory')
    for row in pin['files']:
        if digest(safe(upstream / row['path'])) != row['sha256']:
            raise ValueError('Pinned upstream changed: ' + row['path'])
    source = work / 'source'
    for row in pin['files']:
        target = source / row['path']
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(upstream / row['path'], target)
        # GitHub archive uses CRLF/BOM; the tracked local diff uses LF.
        if target.suffix != '.ico':
            content = target.read_text(encoding='utf-8-sig')
            with target.open('w', encoding='utf-8', newline='\n') as stream:
                stream.write(content)
    environment = dict(os.environ, GIT_CEILING_DIRECTORIES=str(work))
    patch = HERE / 'goldcraft-gui.patch'
    subprocess.run(['git', 'apply', '--check', str(patch)], cwd=source, env=environment, check=True, timeout=15)
    subprocess.run(['git', 'apply', str(patch)], cwd=source, env=environment, check=True, timeout=15)
    shutil.copyfile(HERE / 'GoldCraftIntegration.cs', source / 'src/MetahookInstallerAvalonia/ViewModels/GoldCraftIntegration.cs')
    result = dict(upstreamCommit=pin['commit'], patchSha256=digest(patch),
                  integrationSha256=digest(HERE / 'GoldCraftIntegration.cs'),
                  sourceFiles=[dict(path=p.relative_to(source).as_posix(), sha256=digest(p))
                               for p in sorted(source.rglob('*')) if p.is_file()])
    (work / 'prepared.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    return source, result


def build(source, result, dotnet, cache=None):
    work = source.parent
    cache = safe(cache) if cache else work / 'nuget'
    if not cache.is_relative_to(work.parent):
        raise ValueError('NuGet cache must remain in the workspace build area')
    config = work / 'NuGet.Config'
    config.write_text('<configuration><packageSources><clear/><add key="nuget.org" '
                      'value="https://api.nuget.org/v3/index.json"/></packageSources></configuration>\n', encoding='utf-8')
    env = dict(os.environ, DOTNET_CLI_HOME=str(work / 'dotnet-home'),
               NUGET_PACKAGES=str(cache), DOTNET_CLI_TELEMETRY_OPTOUT='1',
               AVALONIA_TELEMETRY_OPTOUT='1',
               DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1', DOTNET_GENERATE_ASPNET_CERTIFICATE='false',
               MSBUILDDISABLENODEREUSE='1')
    output = work / 'publish'
    project = source / 'src/MetahookInstallerAvalonia.Desktop/MetahookInstallerAvalonia.Desktop.csproj'
    command = [dotnet, 'publish', str(project), '-c', 'Release', '-r', 'win-x64',
               '--self-contained', 'true', '-o', str(output), '-m:1', '-nr:false',
               '-p:UseSharedCompilation=false', '-p:BuildInParallel=false',
               '-p:PublishAot=false', '-p:PublishTrimmed=false', '-p:PublishSingleFile=true',
               '-p:IncludeNativeLibrariesForSelfExtract=true', '-p:EnableCompressionInSingleFile=true',
               '-p:DebugType=None', '-p:DebugSymbols=false', '-p:RestoreDisableParallel=true',
               '-p:NuGetAudit=false', '-p:RestoreConfigFile=' + str(config)]
    started = time.monotonic()
    result.update(command=command, exitCode=None, timedOut=False)
    try:
        with (work / 'build.log').open('wb') as log:
            completed = subprocess.run(command, cwd=source, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=230)
        result['exitCode'] = completed.returncode
    except subprocess.TimeoutExpired:
        result['timedOut'] = True
        raise
    finally:
        result['seconds'] = time.monotonic() - started
        (work / 'build-result.json').write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    completed.check_returncode()
    for row in result['sourceFiles']:
        if digest(source / row['path']) != row['sha256']:
            raise ValueError('Build input changed: ' + row['path'])
    exe = output / 'MetahookInstallerAvalonia.Desktop.exe'
    identity = {key: result[key] for key in ('upstreamCommit', 'patchSha256', 'integrationSha256')}
    identity['files'] = [dict(path=p.relative_to(output).as_posix(), bytes=p.stat().st_size, sha256=digest(p))
                         for p in sorted(output.rglob('*')) if p.is_file()]
    if not exe.is_file():
        raise FileNotFoundError(exe)
    (output / 'gui-build.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    return identity


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream', required=True, type=Path)
    parser.add_argument('--work', required=True, type=Path)
    parser.add_argument('--build', action='store_true')
    parser.add_argument('--dotnet', default='dotnet')
    parser.add_argument('--nuget-cache', type=Path, help='Reuse a sibling build cache without copying it')
    args = parser.parse_args()
    source, result = prepare(args.upstream, args.work)
    if args.build:
        print(json.dumps(build(source, result, args.dotnet, args.nuget_cache)))
    else:
        print(json.dumps(dict(prepared=str(source), files=len(result['sourceFiles']), built=False)))
