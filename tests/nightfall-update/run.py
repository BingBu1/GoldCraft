"""Exercise the delivered Nightfall installer in Windows PowerShell 5.1.

Real payload bytes, an isolated fake server root, no game process launches.
"""
from pathlib import Path
import argparse
import base64
import hashlib
import json
import os
import shutil
import subprocess
import time
import zipfile

ROOT = Path(__file__).resolve().parents[2]
STATE = '.bingsang-current-update'


def sha(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main(package, plan_path, output, fixture_root=None, prior_receipt=None):
    package, plan_path, output = [p.resolve() for p in (package, plan_path, output)]
    if output.exists() or not output.is_relative_to(ROOT/'build'):
        raise ValueError('Fresh workspace output required')
    output.mkdir(parents=True)
    target = fixture_root.resolve() if fixture_root else output/'server with spaces'
    if not target.is_relative_to(ROOT/'build') or target.is_symlink() or target.is_junction():
        raise ValueError('Fixture must remain in workspace build')
    target.mkdir(exist_ok=True)
    plan = json.loads(plan_path.read_text('utf-8'))
    manifest = json.loads((package/'update-manifest.json').read_text('utf-8'))
    report = dict(passed=False, checks=[], calls=[], realRuntimeModified=False)
    report['packageInputs'] = {p.name: sha(p) for p in package.iterdir()
                              if p.is_file() and p.suffix.lower() in ('.ps1', '.json', '.cmd')}
    if prior_receipt:
        prior = json.loads(prior_receipt.read_text('utf-8-sig'))
        if not prior.get('passed') or not all(prior['packageInputs'].get(n) == h
                for n, h in report['packageInputs'].items() if n.endswith(('.ps1', '.cmd'))):
            raise ValueError('Reused rejection checks require exactly the same installer scripts')
        report['priorRejectionReceipt'] = dict(path=str(prior_receipt), sha256=sha(prior_receipt))
    archives = {}

    def check(value, label):
        if not value:
            raise AssertionError(label)
        report['checks'].append(label)
        print('PASS '+label, flush=True)

    def put(relative, data):
        path = target/relative
        path.parent.mkdir(parents=True, exist_ok=True)
        temp = path.with_name(path.name+'.fixture.tmp')
        temp.write_bytes(data.encode('utf-8') if isinstance(data, str) else data)
        os.replace(temp, path)

    def archived(row):
        if row['archive'] not in archives:
            archives[row['archive']] = zipfile.ZipFile(row['archive'])
        data = archives[row['archive']].read(row['member'])
        if hashlib.sha256(data).hexdigest() != row['sha256']:
            raise ValueError('Issued archive changed')
        return data

    def snapshot():
        return {p.relative_to(target).as_posix(): sha(p) for p in target.rglob('*')
                if p.is_file() and STATE not in p.parts}

    def run(label, *args, ok=True, cmd=False):
        if cmd:
            command = 'cmd.exe /d /s /c ""'+str(package/'Update-Server.cmd')+'" -TargetDirectory "'+str(target)+'" '+' '.join(args)+'"'
        else:
            command = ['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                       '-File', str(package/'Update-Server.ps1'), '-TargetDirectory', str(target), *args]
        env = dict(os.environ)
        env.pop('PSModulePath', None)
        start = time.monotonic()
        result = subprocess.run(command, input=b'\r\n', capture_output=True, timeout=600,
                                env=env, creationflags=subprocess.CREATE_NO_WINDOW)
        log = output/('call-%02d.log' % (len(report['calls'])+1))
        log.write_bytes(result.stdout+result.stderr)
        report['calls'].append(dict(label=label, code=result.returncode,
                                   seconds=round(time.monotonic()-start, 3), log=log.name))
        if (result.returncode == 0) != ok:
            raise AssertionError(log.read_text(errors='replace')[-5000:])
        check(True, label)

    try:
        check(subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()']).startswith(b'5.1.'), 'Windows PowerShell 5.1')
        for i, row in enumerate(plan['requiredBaseline'], 1):
            source = Path(row['source'])
            if sha(source) != row['sha256']:
                raise ValueError('Required real fixture source changed')
            dest = target/row['path']
            dest.parent.mkdir(parents=True, exist_ok=True)
            if dest.is_file() and sha(dest) == row['sha256']:
                continue
            # Only create a new, recorded immutable sharing group. Never extend
            # an existing group whose other owners were not reviewed here.
            if source.stat().st_nlink == 1 and source.suffix.lower() not in ('.dll', '.json', '.txt', '.res', '.csv'):
                os.link(source, dest)
            else:
                shutil.copy2(source, dest)
            if i % 1000 == 0:
                print('Seeded baseline '+str(i), flush=True)
        for name in plan['pins']:
            candidates = [r for r in plan['history'][name.lower()].values() if 'archive' in r]
            row = max(candidates, key=lambda r: r['release'])
            put(name, archived(row))
        publications = []
        for row in plan['history']['cs/launcher-public/manifest.json'].values():
            if 'archive' not in row:
                continue
            data = archived(row)
            release = json.loads(base64.b64decode(json.loads(data)['payload']))
            if release['schema'] == 2:
                publications.append((release['sequence'], data))
        put('cs/launcher-public/manifest.json', max(publications)[1])
        fixture = {
            'Start.cmd': 'fixture launcher, never execute', 'Stop.cmd': 'fixture stop, never execute',
            'cs/hlds.exe': 'fixture marker, never execute',
            'server-settings.json': json.dumps(dict(map='zs_deadend', botTotalPlayers=0, csolGameMode=2,
                    csPort=27015, csPublicPort=46344, httpPublicUrl='http://owner.invalid:28080/goldcraft/', ownerExtension={'retain': True})),
            'cs/cstrike/server.cfg': 'hostname "Owner"\nrcon_password "fixture-only"\nmc_http_public_url "http://owner.invalid:28080/goldcraft/"\n',
            'cs/cstrike/game_init.cfg': '// owner comment\n',
            'cs/cstrike/addons/sypb/sypb.cfg': '// owner\nsypb_quota 0\n',
            'cs/cstrike/addons/amxmodx/configs/plugins.ini': '; owner\ncustom.amxx debug\ngoldcraft.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/plugins-owner.ini': 'addon_floating_damage.amxx\nowner_extra.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/goldcraft/game_modes.ini': '; owner\ncustom.amxx 7\n',
            'cs/cstrike/addons/amxmodx/configs/modules.ini': 'reapi\n',
            'cs/cstrike/addons/amxmodx/configs/users.ini': 'private-admin-fixture',
            '.state/secrets.json': '{"token":"private-fixture"}',
            '.state/database.env': 'private-database-fixture',
            'minecraft/world/level.dat': 'preserve world',
        }
        for name, data in fixture.items():
            put(name, data)
        for name in ('Run-Server.ps1', 'Start-Servers.ps1', 'Common.ps1'):
            put('scripts/'+name, (ROOT/'tools/server-package/scripts'/name).read_bytes())
        original = snapshot()
        run('plan validates every real omitted resource', '-Plan')
        check(snapshot() == original, 'plan changes no server bytes')
        if not prior_receipt:
            missing = plan['requiredBaseline'][0]
            path = target/missing['path']
            saved = path.read_bytes()
            path.unlink()
            run('missing baseline rejects before replacement', '-Plan', ok=False)
            put(missing['path'], saved)
            unknown = plan['files'][0]['path']
            put(unknown, 'unknown fixture version')
            run('unknown existing resource rejects before replacement', '-Plan', ok=False)
            (target/unknown).unlink()
            check(snapshot() == original, 'rejected preflights preserve original root')
            helper = package/'Files.ps1'
            raw = helper.read_bytes()
            try:
                helper.write_bytes(raw+b'''\r\n$script:originalAtomic=${function:Atomic}
$script:failedOnce=$false
function Atomic([string]$Target,[string]$Source,[byte[]]$Bytes){
    if(!$script:failedOnce -and $Target.EndsWith('\\cs\\swds.dll') -and $Target -notmatch 'bingsang-current-update'){
        $script:failedOnce=$true;throw 'injected engine replacement failure'
    }
    & $script:originalAtomic $Target $Source $Bytes
}
''')
                run('late replacement failure restores all prior writes', ok=False)
                check(snapshot() == original, 'automatic rollback restores exact root')
            finally:
                helper.write_bytes(raw)
        run('actual CMD installs real cumulative payload', cmd=True)
        check(all(sha(target/r['path']) == r['sha256'] for r in manifest['files']), 'all installed payload SHA values match')
        doc = json.loads((target/'cs/cstrike/addons/goldcraft/config/gameplay.json').read_text('utf-8-sig'))
        check(doc['features']['time_cycle']['enabled'] and doc['features']['time_cycle']['values']['midday_seconds']['value'] == 180 and doc['features']['time_cycle']['values']['dusk_seconds']['value'] == 120, 'normal 180/120 second Nightfall defaults enabled')
        check(all(not v['enabled'] for k, v in doc['features'].items() if k not in ('kick', 'time_cycle')), 'unfinished gameplay capabilities remain disabled')
        protected = [n for n in fixture if n.startswith(('.state/', 'minecraft/')) or n.endswith('users.ini') or n in ('Start.cmd', 'Stop.cmd')]
        check(all(sha(target/n) == original[n] for n in protected), 'admins database accounts worlds launchers preserved')
        settings = json.loads((target/'server-settings.json').read_text('utf-8-sig'))
        check(settings == json.loads(fixture['server-settings.json']), 'ports HTTP address and operator extension preserved')
        check('-csol_game_mode 2 +csol_weapons 1' in (target/'scripts/Run-Server.ps1').read_text('utf-8-sig'), 'scenario mode selected before the first map')
        installed = snapshot()
        run('second update is idempotent')
        check(snapshot() == installed, 'idempotent update preserves every byte')
        run('actual CMD rollback succeeds', '-Restore', cmd=True)
        check(snapshot() == original, 'rollback restores exact baseline including absent gameplay document')
        custom = json.loads((package/'gameplay-defaults.json').read_text('utf-8'))
        custom['operatorExtension'] = {'retain': True}
        custom['features']['time_cycle']['enabled'] = False
        custom['features']['time_cycle']['values']['midday_seconds']['value'] = 181
        custom['features']['time_cycle']['values']['dusk_seconds']['value'] = 121
        custom['features']['kick']['enabled'] = False
        relative = 'cs/cstrike/addons/goldcraft/config/gameplay.json'
        put(relative, json.dumps(custom))
        script = output/'configuration-test.ps1'
        script.write_text('''param([string]$Package,[string]$Target)
. (Join-Path $Package 'Files.ps1')
. (Join-Path $Package 'Nightfall.ps1')
$changes=@{}
Add-NightfallConfiguration $Target $changes
[Console]::OutputEncoding=[Text.Encoding]::UTF8
Write-Output $changes['cs/cstrike/addons/goldcraft/config/gameplay.json']
''', 'utf-8-sig')
        result = subprocess.run(['powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', str(script),
                                 '-Package', str(package), '-Target', str(target)], capture_output=True,
                                timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        if result.returncode:
            raise AssertionError(result.stderr.decode(errors='replace'))
        merged = json.loads(result.stdout.decode('utf-8-sig'))
        check(merged['operatorExtension'] == custom['operatorExtension'] and not merged['features']['kick']['enabled'] and
              merged['features']['time_cycle']['enabled'] and
              merged['features']['time_cycle']['values']['midday_seconds']['value'] == 181 and
              merged['features']['time_cycle']['values']['dusk_seconds']['value'] == 121,
              'existing operator values and fields survive the time-cycle merge')
        (target/relative).unlink()
        check(all(sha(package/n) == h for n, h in report['packageInputs'].items()),
              'delivered installer inputs unchanged after all checks')
        report['passed'] = True
    finally:
        for archive in archives.values():
            archive.close()
        (output/'receipt.json').write_text(json.dumps(report, indent=2)+'\n', 'utf-8')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    for name in ('package', 'plan', 'output'):
        p.add_argument('--'+name, type=Path, required=True)
    p.add_argument('--fixture', type=Path)
    p.add_argument('--prior-receipt', type=Path)
    a = p.parse_args()
    main(a.package, a.plan, a.output, a.fixture, a.prior_receipt)
