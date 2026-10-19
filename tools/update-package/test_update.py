"""Exercise the shipped Windows PowerShell updater against isolated installs.

Uses a built package as input; never launches games or updates a live instance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import subprocess
import zipfile


def digest(data):
    return hashlib.sha256(data).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--packages', type=Path, required=True)
    parser.add_argument('--fixtures', type=Path, required=True)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--server-only', action='store_true')
    args = parser.parse_args()
    root = args.fixtures.absolute()
    if root.exists():
        raise ValueError('Use a new fixture folder')
    root.mkdir(parents=True)
    roles = [('server', 'Server')] if args.server_only else [('server', 'Server'), ('cs', 'Players'), ('minecraft', 'Players')]
    packages = {role: next(args.packages.glob('GoldCraft-' + kind + '-Update-*')) for role, kind in roles}
    packages = {k: v if v.is_dir() else v.with_suffix('') for k, v in packages.items()}
    reports = []

    def write(target, name, data):
        path = target / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data if isinstance(data, bytes) else data.encode())

    def snapshot(target):
        return {p.relative_to(target).as_posix(): digest(p.read_bytes())
                for p in target.rglob('*') if p.is_file() and '.goldcraft-updates' not in p.parts}

    def run(role, target, *, plan=False, success=True):
        command = ['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                   '-File', str(packages[role] / 'Update-GoldCraft.ps1'),
                   '-Role', role, '-TargetDirectory', str(target)]
        if plan:
            command.append('-Plan')
        result = subprocess.run(command, capture_output=True, timeout=90)
        text = (result.stdout + result.stderr).decode(errors='replace')
        (root / f'call-{len(list(root.glob("call-*.log"))) + 1:02}.log').write_text(text, encoding='utf-8')
        assert (result.returncode == 0) == success, text
        return json.loads(result.stdout) if plan and success else text

    def mc_fixture(name):
        target = root / name
        core = next((packages['minecraft'] / 'payload/minecraft/mods').glob('*.jar'))
        write(target, 'mods/old-renamed-goldcraft.jar', core.read_bytes())
        with zipfile.ZipFile(target / 'mods/ferritecore.jar', 'w') as stream:
            stream.writestr('example/Unrelated.class', b'unrelated mod')
        write(target, 'saves/private/level.dat', b'private-world')
        write(target, 'config/client.cfg', 'keep=1')
        return target

    def server_fixture(name):
        target = root / name
        core = next((packages['server'] / 'payload/server/minecraft/mods').glob('*.jar'))
        for path, data in {
            'Start.cmd': '@echo off\n', 'cs/hlds.exe': b'fixture',
            'server-settings.json': '{"csPort":27015,"csPublicPort":46344,"minecraftPort":25565}',
            'cs/cstrike/server.cfg': 'hostname "private"\nrcon_password "fixture-only-value"\nmc_map_mining 1\nmc_round_clear_blocks 0\nmc_death_drop_items 1\n',
            'cs/cstrike/addons/amxmodx/configs/users.ini': '; private administrator data',
            'cs/cstrike/addons/amxmodx/configs/plugins.ini': 'custom.amxx\ngoldcraft.amxx disabled\ngoldcraft.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/plugins-existing.ini': 'goldcraft_controls.amxx\ngoldcraft_controls.amxx\ngoldcraft_zombie_armor.amxx\ngoldcraft_zombie_armor.amxx\ngoldcraft_zombie_respawn.amxx disabled\n',
            'cs/cstrike/addons/amxmodx/configs/plugins-goldcraft-connection.ini': '; owner note\ncustom_connection.amxx\ngoldcraft_connection.amxx disabled\n',
            'cs/cstrike/addons/amxmodx/configs/plugins-floating_damage.ini': '; floating owner note\ncustom_numbers.amxx\naddon_floating_damage.amxx\naddon_floating_damage.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/plugins.stock.ini': 'goldcraft.amxx\n',
            'cs/cstrike/addons/amxmodx/configs/modules.ini': 'reapi\n',
            'cs/swds.dll': b'oldengine', 'minecraft/mods/renamed-core.jar': core.read_bytes(),
            'client-update/mods/renamed-core.jar': core.read_bytes(),
            'minecraft/world/level.dat': b'world', '.state/secrets.json': '{"fixture":"private"}',
        }.items():
            write(target, path, data)
        return target

    # Windows PowerShell 5.1, including its parser and the real -File invocation.
    version = subprocess.check_output(['powershell.exe', '-NoProfile', '-Command', '$PSVersionTable.PSVersion.ToString()']).decode().strip()
    assert version.startswith('5.1.'), version
    if not args.server_only:
        target = mc_fixture('minecraft')
        before = snapshot(target)
        plan = run('minecraft', target, plan=True)
        assert not plan['applied'] and plan['files'] == 2 and before == snapshot(target)
        run('minecraft', target)
        assert not (target / 'mods/old-renamed-goldcraft.jar').exists()
        assert before['mods/ferritecore.jar'] == snapshot(target)['mods/ferritecore.jar']
        assert before['saves/private/level.dat'] == snapshot(target)['saves/private/level.dat']
        assert run('minecraft', target, plan=True)['files'] == 0
        assert 'Already current' in run('minecraft', target)
        reports.append('MC renamed-core removal, unrelated mods/world/config, plan and idempotency')

    server = server_fixture('server')
    before = snapshot(server)
    run('server', server)
    after = snapshot(server)
    for name in ('server-settings.json', 'cs/cstrike/addons/amxmodx/configs/users.ini',
                 'cs/cstrike/addons/amxmodx/configs/plugins.stock.ini', 'minecraft/world/level.dat', '.state/secrets.json'):
        assert before[name] == after[name], name
    cfg = (server / 'cs/cstrike/server.cfg').read_text()
    assert 'rcon_password "fixture-only-value"' in cfg and 'hostname "private"' in cfg
    assert all(cfg.count(line) == 1 for line in ['mc_map_mining 2', 'mc_round_clear_blocks 1', 'mc_death_drop_items 0'])
    assert 'mc_map_mining 1' not in cfg
    listings = [server / 'cs/cstrike/addons/amxmodx/configs/plugins.ini', *sorted((server / 'cs/cstrike/addons/amxmodx/configs').glob('plugins-*.ini'))]
    active = [line.split(';')[0].strip() for p in listings for line in p.read_text().splitlines()]
    required = json.loads((packages['server'] / 'update-manifest.json').read_text())['requiredPlugins']
    assert all(active.count(n) == 1 for n in required)
    assert 'custom.amxx' in active
    assert 'custom_connection.amxx' in active
    assert '; owner note' in (server / 'cs/cstrike/addons/amxmodx/configs/plugins-goldcraft-connection.ini').read_text()
    manifest = json.loads((packages['server'] / 'update-manifest.json').read_text())
    floating = [row for row in manifest['files'] if 'floating_damage' in row['destination']]
    assert len(floating) == 4 and 'addon_floating_damage.amxx' in required
    for row in floating:
        if not row['destination'].endswith('.ini'):
            assert digest((server / row['destination']).read_bytes()) == row['sha256']
    listing = (server / 'cs/cstrike/addons/amxmodx/configs/plugins-floating_damage.ini').read_text()
    assert '; floating owner note' in listing and 'custom_numbers.amxx' in active
    reports.append('Floating damage plugin/model/translation installed, enabled once, owner list preserved')
    for name in ('zp50_zombie_armor.amxx', 'zp50_zombie_respawn.amxx', 'zp50_zombie_knockback.amxx', 'zp50_zombie_gore.amxx'):
        if name in required:
            destination = 'cs/cstrike/addons/amxmodx/plugins/' + name
            row = next(r for r in manifest['files'] if r['destination'] == destination)
            assert digest((server / destination).read_bytes()) == row['sha256']
            assert active.count(name) == 1
            retired = name.replace('zp50_', 'goldcraft_', 1)
            assert retired not in active and active.count(retired+' disabled') == 1
    if 'zp50_zombie_respawn.amxx' in required:
        assert (server / 'cs/cstrike/addons/amxmodx/data/lang/zp50_zombie_respawn.txt').is_file()
        reports.append('Zombie armor/respawn/headshot compiled plugins installed and enabled once with translation')
    assert not (server / 'minecraft/mods/renamed-core.jar').exists()
    assert not (server / 'client-update/mods/renamed-core.jar').exists()
    assert run('server', server, plan=True)['files'] == 0
    reports.append('Server ports/admin/RCON/save/secrets preserved, gameplay defaults and unique enabled registrations')

    failed = server_fixture('rollback')
    before = snapshot(failed)
    with (failed / 'cs/swds.dll').open('rb'):
        run('server', failed, success=False)
    assert before == snapshot(failed), 'Rollback changed installed files'
    transaction = next((failed / '.goldcraft-updates').glob('*/transaction.json'))
    assert json.loads(transaction.read_text())['status'] == 'rolled-back'
    reports.append('Real mid-install locked-file failure restores every changed file')

    if args.server_only:
        package = packages['server']
        entry = package / 'Update-Server.cmd'
        before = snapshot(server)
        result = subprocess.run(['cmd.exe', '/d', '/c', str(entry), '-TargetDirectory', str(server), '-Plan'],
                                input=b'\r\n', capture_output=True, timeout=90)
        assert b'"applied":  false' in result.stdout and before == snapshot(server), result.stdout
        reports.append('Shipped server CMD invokes PowerShell 5.1 without changing current files')
        # A mistaken paste must be recoverable in the same window, before any
        # transaction or target mutation. Exercise actual Read-Host input.
        command = ['powershell.exe', '-NoLogo', '-NoProfile', '-ExecutionPolicy', 'Bypass',
                   '-File', str(package / 'Update-GoldCraft.ps1'), '-Role', 'server', '-Plan']
        for first in ('E:\\bad<path', str(server / 'Start.cmd'), 'Start.cmd'):
            result = subprocess.run(command, input=(first+'\r\n"'+str(server)+'"\r\n').encode(),
                                    capture_output=True, timeout=90)
            assert result.returncode == 0 and b'Invalid target:' in result.stdout, result.stdout + result.stderr
            assert b'"applied":  false' in result.stdout and before == snapshot(server)
        cancelled = subprocess.run(command, input=b'\r\n', capture_output=True, timeout=90)
        # Windows PowerShell wraps error text to console width, including within
        # words when the package path makes the error prefix long.
        error = cancelled.stderr.replace(b'\r', b'').replace(b'\n', b'')
        assert cancelled.returncode != 0 and b'Update cancelled' in error
        assert before == snapshot(server)
        for path in ('E:\\bad<path', str(server / 'Start.cmd')):
            result = subprocess.run(command + ['-TargetDirectory', path], capture_output=True, timeout=90)
            assert result.returncode != 0 and before == snapshot(server)
        for quote in ('"', "'"):
            result = subprocess.run(command + ['-TargetDirectory', '  '+quote+str(server)+quote+'  '], capture_output=True, timeout=90)
            assert result.returncode == 0 and b'"applied":  false' in result.stdout, result.stdout + result.stderr
            assert before == snapshot(server)
        reports.append('Invalid interactive paths retry, blank cancels, explicit invalid paths fail before writes, quoted folders accepted')
        report = dict(passed=True, powerShell=version, checks=reports, gameLaunched=False, liveInstallTouched=False)
        (root / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report))
        return

    cs = root / 'cs'
    for name in ('hw.dll', 'cstrike/cl_dlls/client.dll'):
        write(cs, name, (args.game / name).read_bytes())
    write(cs, 'cstrike/config.cfg', 'bind F6 "custom"\n')
    write(cs, 'cstrike/metahook/configs/plugins.lst', 'Custom.dll\nRenderer.dll\nRenderer_AVX2.dll\nGoldCraft.dll\n')
    write(cs, 'cstrike/metahook/configs/plugins_goldsrc.lst', 'Renderer.dll\n')
    write(cs, 'cstrike/metahook/plugins/Renderer.dll', b'oldRenderer')
    before = snapshot(cs)
    run('cs', cs)
    assert before['hw.dll'] == snapshot(cs)['hw.dll'] and before['cstrike/config.cfg'] == snapshot(cs)['cstrike/config.cfg']
    assert not (cs / 'cstrike/metahook/plugins/Renderer.dll').exists()
    assert not (cs / 'cstrike/metahook/configs/plugins_goldsrc.lst').exists()
    entries = (cs / 'cstrike/metahook/configs/plugins.lst').read_text().splitlines()
    assert 'Custom.dll' in entries and entries[entries.index('Renderer_AVX2.dll') + 1] == 'GoldCraft.dll'
    assert run('cs', cs, plan=True)['files'] == 0
    reports.append('CS complete resource update, plugin merge/alias removal, bindings/engine preserved and idempotency')
    write(cs, 'hw.dll', b'mismatched-engine')
    before = snapshot(cs)
    run('cs', cs, success=False)
    assert before == snapshot(cs)
    reports.append('Wrong engine rejected before writes')

    package = packages['minecraft']
    extra = package / 'payload/minecraft/unlisted.txt'
    extra.write_bytes(b'not in manifest')
    before = snapshot(target)
    try:
        assert 'Unlisted payload' in run('minecraft', target, success=False)
    finally:
        extra.unlink()
    assert before == snapshot(target)
    payload = next((package / 'payload/minecraft/mods').glob('*.jar'))
    data = payload.read_bytes()
    try:
        payload.write_bytes(data + b'corrupt')
        assert 'integrity failure' in run('minecraft', target, success=False)
    finally:
        payload.write_bytes(data)
    assert before == snapshot(target)
    reports.append('Unlisted payload and corrupt JAR rejected before writes')

    # Exercise a shipped CMD entry with an explicit target and a no-write plan.
    result = subprocess.run(['cmd.exe', '/d', '/c', str(package / 'Update-Minecraft.cmd'),
                             '-TargetDirectory', str(target), '-Plan'], input=b'\r\n', capture_output=True, timeout=90)
    assert b'"applied":  false' in result.stdout and before == snapshot(target), result.stdout
    reports.append('Shipped CMD entry actually invokes PowerShell 5.1')
    report = dict(passed=True, powerShell=version, checks=reports, gameLaunched=False, liveInstallTouched=False)
    (root / 'result.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
