"""Verify an extracted release with real independent servers, never sandbox B.

Uses loopback and alternate ports, retains evidence, then requests graceful stop.
Online authentication stays enabled. A raw login cannot impersonate a paid user;
Successful authenticated CS admission still needs a real Minecraft account.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import socket
import struct
import subprocess
import time
import uuid
import zipfile

ROOT = Path(__file__).resolve().parent.parent
POWERSHELL = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'


def varint(n):
    result = bytearray()
    while n & ~127:
        result.append((n & 127) | 128)
        n >>= 7
    return bytes(result + bytes([n]))


def string(s):
    data = s.encode()
    return varint(len(data)) + data


def read_varint(sock):
    n = 0
    for shift in range(0, 35, 7):
        b = sock.recv(1)
        if not b:
            raise EOFError('Disconnected before a complete Minecraft packet')
        n |= (b[0] & 127) << shift
        if not b[0] & 128:
            return n
    raise ValueError('Invalid VarInt')


def packet(sock):
    length = read_varint(sock)
    if length > 2**20:
        raise ValueError('Unexpected packet size')
    data = bytearray()
    while len(data) < length:
        chunk = sock.recv(length - len(data))
        if not chunk:
            raise EOFError()
        data += chunk
    return bytes(data)


def send(sock, body):
    sock.sendall(varint(len(body)) + body)


def mc_connection(port, intention):
    sock = socket.create_connection(('127.0.0.1', port), timeout=5)
    send(sock, b'\x00' + varint(767) + string('localhost') + struct.pack('>H', port) + varint(intention))
    return sock


def read_status(path):
    # Native status publication truncates in place; a partial read is not a
    # product failure. Bound retries so a permanently bad file still fails.
    deadline = time.monotonic() + 3
    while True:
        try:
            return json.loads(path.read_text(encoding='utf-8'))
        except (OSError, ValueError):
            if time.monotonic() >= deadline:
                raise
            time.sleep(.05)


def server_say_feedback(log, marker):
    # Match the dedicated server's complete chat announcement, never command
    # echoes or another logger mentioning the same token.
    pattern = (r'^\[[^\]\r\n]+\] \[Server thread/INFO\] '
               r'\[(?:net\.minecraft\.server\.MinecraftServer/|minecraft/MinecraftServer)\]: '
               r'(?:\[Not Secure\] )?\[Server\] ' + re.escape(marker) + r'\r?$')
    match = re.search(pattern, log, re.MULTILINE)
    return match.group(0).rstrip('\r') if match else None


def vector_close(actual, expected):
    return (isinstance(actual, list) and len(actual) == 3
            and all(isinstance(a, (int, float)) and not isinstance(a, bool)
                    and math.isfinite(a) and abs(a - b) <= .05
                    for a, b in zip(actual, expected)))


def live_actor_near_probe(status, mirrored_slots=None):
    # Match MinecraftObjects' live-actor export radius for block (0,80,0).
    for actor in status.get('actors', []):
        if actor.get('alive') is not True:
            continue
        if mirrored_slots is not None and actor.get('slot') not in mirrored_slots:
            continue
        origin, mins = actor.get('origin'), actor.get('mins')
        if not (isinstance(origin, list) and isinstance(mins, list) and len(origin) == len(mins) == 3):
            continue
        feet = (origin[0] / 32, (origin[2] + mins[2]) / 32 + 64, -origin[1] / 32)
        distance = sum(max(lo - p, 0, p - hi) ** 2
                       for p, lo, hi in zip(feet, (0, 80, 0), (1, 81, 1)))
        if distance < 128 ** 2:
            return True
    return False


def populated_gold_proxy(status):
    if (status.get('map') != 'cs_assault' or status.get('world') in (None, '0')
            or status.get('dedicated') is not True or status.get('tick', 0) <= 0
            or status.get('objectRevision', 0) <= 0 or status.get('minecraftObjects', 0) <= 0
            or not live_actor_near_probe(status)):
        return None
    for entity in status.get('hostEntities', []):
        if (entity.get('class') == 'goldcraft_object' and entity.get('minecraftKey', 0) > 0
                and entity.get('solid') == 2 and entity.get('health', 0) > 0
                and vector_close(entity.get('origin'), (16.0, -16.0, 528.0))
                and vector_close(entity.get('min'), (-1.0, -33.0, 511.0))
                and vector_close(entity.get('max'), (33.0, 1.0, 545.0))):
            return entity
    return None


def cleared_new_world(before, native, minecraft, expected_map='sy_zombie2_Bloodmoon'):
    exports = minecraft.get('minecraftObjects', {})
    mirrors = minecraft.get('nativePlayers', {})
    mirrored_slots = {body.get('slot') for body in mirrors.get('hitboxes', [])}
    return (populated_gold_proxy(before) is not None
            and native.get('dedicated') is True and native.get('tick', 0) > 0
            and native.get('objectRevision', 0) > 0 and isinstance(native.get('hostEntities'), list)
            and native.get('world') not in (None, '0', before['world'])
            and native.get('world') == minecraft.get('world')
            and native.get('map') == minecraft.get('map') == expected_map
            and live_actor_near_probe(native, mirrored_slots)
            and mirrors.get('players', 0) > 0
            and exports.get('revision', 0) > 0
            and native['objectRevision'] >= exports['revision']
            and exports.get('pendingChunks') == 0
            and exports.get('candidates') == exports.get('exported') == 0
            and native.get('minecraftObjects') == 0
            and not any(e.get('class') == 'goldcraft_object' or e.get('minecraftKey', 0) != 0
                        for e in native.get('hostEntities', [])))


def cs_query(query, *, challenge=False):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as udp:
        udp.settimeout(5)
        udp.connect(('127.0.0.1', 27145))
        udp.send(query)
        reply = udp.recv(4096)
        if challenge and reply[:5] == b'\xff'*4 + b'A' and len(reply) == 9:
            udp.send(query + reply[5:9])
            reply = udp.recv(4096)
        return reply


def cs_slots(reply):
    if reply[:4] != b'\xff'*4:
        raise ValueError('Invalid CS query header')
    if reply[4:5] == b'I':
        at = 6
        for _ in range(4):
            at = reply.index(0, at) + 1
        at += 2
        players, maximum, bots = reply[at:at+3]
    elif reply[4:5] == b'm':
        at = 5
        for _ in range(5):
            at = reply.index(0, at) + 1
        players, maximum = reply[at:at+2]
        bots = None
    else:
        raise ValueError('Unknown CS query response')
    return {'players': players, 'maximum': maximum, 'bots': bots}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--archive', type=Path, required=True)
    p.add_argument('--work', type=Path, required=True)
    p.add_argument('--evidence', type=Path, required=True)
    p.add_argument('--change-map', default='cs_assault', help='Installed map used for lifecycle verification')
    args = p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_]+', args.change_map):
        p.error('Invalid change-map name')
    work, evidence = args.work.resolve(), args.evidence.resolve()
    if not work.is_relative_to(ROOT / 'server-packages') or work.exists():
        raise ValueError('Use a new directory below server-packages for extraction')
    if not evidence.is_relative_to(ROOT / 'analysis') or evidence.exists():
        raise ValueError('Use a new analysis evidence directory')
    work.mkdir(parents=True)
    evidence.mkdir(parents=True)
    result = {'checks': {}, 'limitations': [
        'No authenticated remote-player join or 32-human load test.',
        'Clean Windows host without an installed Steam client has not been tested.']}
    checks = result['checks']
    server = work / 'GoldCraft-Servers'

    def shell(script, *extra, timeout=60):
        log = evidence / (script + '-' + str(time.time_ns()) + '.log')
        with log.open('wb') as output:
            r = subprocess.run([str(POWERSHELL), '-NoProfile', '-ExecutionPolicy', 'Bypass',
                                '-File', str(server / 'scripts' / script), *extra], cwd=server,
                               stdout=output, stderr=subprocess.STDOUT, timeout=timeout,
                               creationflags=subprocess.CREATE_NO_WINDOW)
        if r.returncode:
            raise RuntimeError(f'{script} exited {r.returncode}: {log.name}')

    def assert_native_probe(predicate, label, timeout=8):
        deadline = time.monotonic() + timeout
        while True:
            native_sample = read_status(server / '.state/goldcraft-server-status.json')
            java_sample = read_status(server / '.state/minecraft-server-status.json')
            if predicate(native_sample, java_sample):
                return native_sample, java_sample
            if time.monotonic() >= deadline:
                raise RuntimeError('Native map-reset condition failed: ' + label)
            time.sleep(.05)

    def assert_command_marker(command, marker, timeout=8):
        # The console transport acknowledging a command is not proof that the
        # command succeeded. Require its unique conditional feedback in MC.
        deadline = time.monotonic() + timeout
        while True:
            shell('Send-Command.ps1', '-Role', 'minecraft', '-Command', command)
            log = (server / 'minecraft/logs/latest.log').read_text(encoding='utf-8', errors='replace')
            feedback = server_say_feedback(log, marker)
            if feedback:
                result.setdefault('mapResetFeedback', []).append(feedback)
                return
            if time.monotonic() >= deadline:
                raise RuntimeError('Minecraft command condition failed: ' + marker)
            time.sleep(.1)

    try:
        with zipfile.ZipFile(args.archive) as z:
            names = z.namelist()
            for name in names:
                path = PurePosixPath(name)
                if path.is_absolute() or '..' in path.parts or ':' in name or '\\' in name or path.parts[0] != 'GoldCraft-Servers':
                    raise ValueError('Unsafe ZIP member')
            if len(names) != len(set(n.lower() for n in names)):
                raise ValueError('Duplicate/case-colliding ZIP member')
            z.extractall(work)
        manifest = json.loads((server / 'package-manifest.json').read_text(encoding='utf-8'))
        expected = {'GoldCraft-Servers/package-manifest.json'} | {'GoldCraft-Servers/' + item['path'] for item in manifest['files']}
        if set(names) != expected:
            raise ValueError('ZIP member set differs from manifest')
        for item in manifest['files']:
            f = server / item['path']
            with f.open('rb') as stream:
                if hashlib.file_digest(stream, 'sha256').hexdigest() != item['sha256']:
                    raise ValueError(f'ZIP hash mismatch: {item["path"]}')
        checks['allArchiveMembersMatchManifest'] = True
        checks['noDevelopmentWorldOrState'] = not (server / '.state').exists() and not list(server.rglob('level.dat'))
        checks['noCopiedOpsOrWhitelist'] = all(json.loads((server / 'minecraft' / n).read_text()) == [] for n in ('ops.json','whitelist.json'))
        settings = json.loads((server / 'server-settings.json').read_text())
        settings.update(csPort=27145, steamPort=26945, minecraftPort=25645, bridgePort=25646, bindAddress='127.0.0.1',
                        publicListing=True, minecraftMemoryGB=3, botTotalPlayers=0)
        (server / 'server-settings.json').write_text(json.dumps(settings), encoding='utf-8')
        shell('Start-Servers.ps1', '-CheckOnly')
        checks['relocatedConfigurationPreflight'] = True
        shell('Start-Servers.ps1', '-AcceptEula', '-Hidden', '-Diagnostics', '-StartupTimeoutSeconds', '90', timeout=140)
        checks['realDualServerStartupAndBridge'] = True
        initial_native = read_status(server / '.state/goldcraft-server-status.json')
        checks['initialBotsDisabled'] = len(initial_native['actors']) == 0
        # Exercise the operator command path, not merely file configuration.
        shell('Send-Command.ps1', '-Role', 'minecraft', '-Command', 'whitelist list')
        shell('Send-Command.ps1', '-Role', 'minecraft', '-Command', 'list')
        time.sleep(1)
        log = (server / 'minecraft/logs/latest.log').read_text(encoding='utf-8', errors='replace')
        checks['consoleCommandExecutedWhitelistEmpty'] = any(text in log for text in (
            '0 whitelisted', 'There are no whitelisted players'))
        with mc_connection(25645, 1) as sock:
            send(sock, b'\x00')
            raw = packet(sock)
            # Decode packet id and VarInt string length from a small memory view.
            at = 1
            while raw[at] & 128:
                at += 1
            status = json.loads(raw[at+1:])
            result['minecraftStatus'] = status
            checks['minecraftAdvertises32Slots'] = status['players']['max'] == 32
        with mc_connection(25645, 2) as sock:
            send(sock, b'\x00' + string('PackageProbe') + uuid.uuid4().bytes)
            first = packet(sock)
            result['loginFirstPacketId'] = first[0]
            checks['unauthenticatedLoginRequiresEncryption'] = first[0] == 1
            # Close before any signed/session-authenticated login can succeed.
        shell('Send-Command.ps1', '-Role', 'minecraft', '-Command', 'list')
        shell('Send-Command.ps1', '-Role', 'cs', '-Command', 'sypb_auto_players 25')
        bot_deadline = time.monotonic() + 25
        while True:
            native = read_status(server / '.state/goldcraft-server-status.json')
            if len(native['actors']) >= 24 or time.monotonic() >= bot_deadline:
                break
            time.sleep(.25)
        java = read_status(server / '.state/minecraft-server-status.json')
        result['nativeWorld'] = native['world']
        result['javaWorld'] = java['world']
        checks['authenticatedBridgeWorldMatches'] = native['world'] == java['world'] and native['world'] != '0'
        checks['realHostEntityApiLoaded'] = native['entityPhysics'] is True
        result['nativeActorCount'] = len(native['actors'])
        checks['botManagerAddsBots'] = len(native['actors']) >= 24
        shell('Send-Command.ps1', '-Role', 'cs', '-Command', 'status')
        deadline = time.monotonic() + 3
        while True:
            native_log = (server / 'cs/qconsole.log').read_text(encoding='utf-8', errors='replace')
            slots = re.findall(r'players\s*:\s*(\d+) active \((\d+) max\)', native_log)
            if slots or time.monotonic() >= deadline:
                break
            time.sleep(.05)
        result['csConsoleSlots'] = list(map(int, slots[-1])) if slots else None
        checks['csConsoleConfirms32Slots'] = bool(slots) and int(slots[-1][1]) == 32
        # Keep engine UDP connectivity distinct from the Steam server browser.
        # A failed query must not prevent map-change/save/exit checks.
        try:
            reply = cs_query(b'\xff'*4 + b'getchallenge steam\n')
            result['csChallengeHex'] = reply.hex()
            checks['csSecureConnectionChallenge'] = bool(re.fullmatch(
                rb'\xff{4}A00000000 [0-9]+ 3 [0-9]+ 1\n\x00?', reply))
        except (OSError, ValueError) as exc:
            checks['csSecureConnectionChallenge'] = False
            result['csChallengeError'] = repr(exc)
        try:
            reply = cs_query(b'\xff'*4 + b'TSource Engine Query\x00', challenge=True)
            result['csInfoHex'] = reply.hex()
            result['csSlots'] = cs_slots(reply)
            checks['csAdvertises32Slots'] = result['csSlots']['maximum'] == 32
        except (OSError, ValueError) as exc:
            checks['csAdvertises32Slots'] = False
            result['csInfoError'] = repr(exc)
        if not (server / 'cs/cstrike/maps' / (args.change_map + '.bsp')).is_file():
            raise ValueError('Lifecycle verification map not included in package')
        dimensions = {item['map'].lower(): item['dimension'] for item in manifest['maps']}
        initial_dimension = dimensions[settings['map'].lower()]
        target_dimension = dimensions[args.change_map.lower()]
        probe_dimensions = list(dict.fromkeys((initial_dimension, target_dimension)))
        result['mapResetProbeDimensions'] = probe_dimensions
        probe_tag = 'gc_package_reset_' + uuid.uuid4().hex
        for index, dimension in enumerate(probe_dimensions):
            shell('Send-Command.ps1', '-Role', 'minecraft', '-Command',
                  f'execute in {dimension} run forceload add 0 0')
            loaded_marker = f'GC_PACKAGE_LOADED_{probe_tag}_{index}'
            assert_command_marker(f'execute in {dimension} if block 0 80 0 minecraft:air run say {loaded_marker}', loaded_marker)
            shell('Send-Command.ps1', '-Role', 'minecraft', '-Command',
                  f'execute in {dimension} run setblock 0 80 0 minecraft:gold_block')
            shell('Send-Command.ps1', '-Role', 'minecraft', '-Command',
                  f'execute in {dimension} run summon minecraft:armor_stand 0.5 82 0.5 {{NoGravity:1b,Tags:["{probe_tag}"]}}')
            ready_marker = f'GC_PACKAGE_READY_{probe_tag}_{index}'
            assert_command_marker(
                f'execute in {dimension} if block 0 80 0 minecraft:gold_block '
                f'if entity @e[type=minecraft:armor_stand,tag={probe_tag}] run say {ready_marker}', ready_marker)
        checks['mapResetFixturesPlacedInTestedDimensions'] = True
        proxy_before, _ = assert_native_probe(
            lambda n, j: n.get('world') == native['world'] and populated_gold_proxy(n) is not None,
            'real gold-block proxy before changelevel')
        checks['nativeGoldProxyExistsBeforeMapChange'] = True
        result['nativeProxyBeforeMapChange'] = proxy_before
        shell('Send-Command.ps1', '-Role', 'cs', '-Command', 'changelevel ' + args.change_map)
        deadline = time.monotonic() + 25
        after, mc_after = native, java
        while time.monotonic() < deadline:
            try:
                after = read_status(server / '.state/goldcraft-server-status.json')
                mc_after = read_status(server / '.state/minecraft-server-status.json')
                if (after['world'] != native['world'] and after['world'] == mc_after['world']
                        and mc_after.get('map') == args.change_map and not after['actors']):
                    break
            except (OSError, ValueError, KeyError):
                pass
            time.sleep(.25)
        checks['bridgeReconnectsAfterMapChange'] = after['world'] != native['world'] and after['world'] == mc_after['world']
        checks['targetMapActivatedInMinecraft'] = mc_after.get('map') == args.change_map
        checks['configuredBotsRemainOffAfterMapChange'] = len(after['actors']) == 0
        for index, dimension in enumerate(probe_dimensions):
            cleared_marker = f'GC_PACKAGE_CLEARED_{probe_tag}_{index}'
            assert_command_marker(
                f'execute in {dimension} if block 0 80 0 minecraft:air '
                f'unless entity @e[type=minecraft:armor_stand,tag={probe_tag}] run say {cleared_marker}', cleared_marker)
        checks['departingDimensionBlocksAndEntitiesCleared'] = True
        checks['targetDimensionBlocksAndEntitiesCleared'] = True
        # First verify the configured Bot reset above, then restore live actors.
        # With no actors MinecraftObjects naturally exports zero collision boxes.
        shell('Send-Command.ps1', '-Role', 'cs', '-Command', 'sypb_auto_players 25')
        after, mc_after = assert_native_probe(
            lambda n, j: len(n.get('actors', [])) >= 24 and
                         cleared_new_world(proxy_before, n, j, args.change_map),
            'new authenticated world has live actors and no Minecraft proxies', timeout=25)
        checks['botManagerAddsBotsAfterMapChange'] = True
        checks['nativeProxiesClearedWithLiveActors'] = True
        result['nativeProxyAfterMapChange'] = after
        result['minecraftProxyAfterMapChange'] = mc_after
        # Keep both probe chunks loaded until the native acceptance snapshot.
        # MinecraftObjects drops cached candidates on ChunkEvent.Unload, which
        # must not be the reason this map-reset check sees an empty export.
        for dimension in probe_dimensions:
            shell('Send-Command.ps1', '-Role', 'minecraft', '-Command',
                  f'execute in {dimension} run forceload remove 0 0')
        result['nativeWorldAfterMapChange'] = after['world']
        result['javaWorldAfterMapChange'] = mc_after['world']
        shell('Stop-Servers.ps1', '-TimeoutSeconds', '30', timeout=65)
        checks['gracefulStopBothServers'] = True
        log = (server / 'minecraft/logs/latest.log').read_text(encoding='utf-8', errors='replace')
        checks['minecraftSavedWorld'] = 'All dimensions are saved' in log
        (evidence / 'minecraft-latest.log').write_text(log, encoding='utf-8')
        result['passed'] = all(checks.values())
    except Exception as exc:
        result['passed'] = False
        result['error'] = str(exc)
    finally:
        if (server / '.state').exists():
            try: shell('Stop-Servers.ps1', '-TimeoutSeconds', '30', timeout=65)
            except Exception as exc: result['cleanupError'] = str(exc)
        for name in ('latest.log', 'debug.log'):
            source = server / 'minecraft/logs' / name
            if source.exists():
                shutil.copyfile(source, evidence / ('minecraft-' + name))
        for name in ('cs-exit.json', 'minecraft-exit.json'):
            source = server / '.state' / name
            if source.exists():
                shutil.copyfile(source, evidence / name)
        for relative in ('.state/native-bridge.log', 'cs/qconsole.log', 'cs/cstrike/qconsole.log'):
            source = server / relative
            if source.exists():
                shutil.copyfile(source, evidence / ('native-' + source.name))
        (evidence / 'result.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result.get('passed') and 'cleanupError' not in result else 1)


if __name__ == '__main__':
    main()
