"""Exercise real ReHLDS 1024-entity encoding and loopback UDP transport.

Uses a separate cs_assault server and one native fake-client CBasePlayer.
Fixture selection and positions are controlled; the real GameDLL packs every
selected edict. Actual Netchan_Transmit/NET_SendLong send to a loopback socket.
No graphical process, production runtime, ownership or authentication is changed.
"""
import argparse
import ctypes
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import time

ROOT = Path(__file__).resolve().parent.parent
TEST = ROOT / 'sandbox/packet-entities'
GAME = TEST / 'Half-Life'
AMXX = GAME / 'cstrike/addons/amxmodx'
EVIDENCE = ROOT / 'analysis/packet-entities'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    value = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(value)
    return value


def put(path, data):
    if not path.resolve().is_relative_to(TEST.resolve()):
        raise ValueError('Independent fixture write escapes its workspace')
    for parent in (path, *path.parents):
        if parent.is_symlink() or parent.is_junction():
            raise ValueError('Fixture reparse path')
        if parent == ROOT:
            break
    path.parent.mkdir(parents=True, exist_ok=True)
    if not path.exists() or path.read_bytes() != data:
        path.write_bytes(data)


def prepare(engine):
    # Reuse the verified native-only asset selection; its output globals are
    # isolated inside this imported module, not the old vitals fixture.
    assets = module('packet_native_assets', 'Exercise-Vitals.py')
    assets.TEST, assets.GAME, assets.AMXX = TEST, GAME, AMXX
    config = assets.prepare()
    for name in ('hlds.exe', 'filesystem_stdio.dll', 'steam_api.dll'):
        put(GAME / name, (ROOT / 'dist/rehlds' / name).read_bytes())
    put(GAME / 'swds.dll', engine.read_bytes())
    put(GAME / 'cstrike/dlls/mp.dll', (ROOT / 'build/regamedll-headless/Release/mp.dll').read_bytes())
    put(AMXX / 'modules/goldcraft_amxx.dll', (ROOT / 'build/native-x86/Release/goldcraft_amxx.dll').read_bytes())
    for name in ('goldcraft', 'goldcraft_headless_test'):
        put(AMXX / f'plugins/{name}.amxx', (ROOT / f'build/amxx/plugins/{name}.amxx').read_bytes())
    put(AMXX / 'configs/plugins.ini', b'goldcraft.amxx debug\ngoldcraft_headless_test.amxx debug\n')
    put(AMXX / 'configs/modules.ini', b'goldcraft\nreapi\nfakemeta\nhamsandwich\n')
    put(GAME / 'cstrike/server.cfg', ('sv_lan 0\nlog off\nmp_freezetime 0\nmp_round_infinite 1\n'
        'mp_timelimit 0\nmp_limitteams 0\nmp_autoteambalance 0\nmp_autokick 0\n'
        'mp_respawn_immunitytime 0\nmc_default_form 0\nmc_allow_switch 0\n'
        'goldcraft_headless_test 1\nrcon_password "' + config['csRconToken'] + '"\n').encode())
    return config


def reassemble(packets):
    """Independent reference of the documented native and GCPK wire layouts."""
    if len(packets) == 1 and packets[0][:4] != b'\xfe\xff\xff\xff':
        return packets[0], False
    pieces, identity, total, expected = {}, None, None, None
    extended = None
    for packet in packets:
        if packet[:4] != b'\xfe\xff\xff\xff' or len(packet) < 9:
            raise ValueError('Unexpected split transport datagram')
        sequence = struct.unpack_from('<I', packet, 4)[0]
        if identity is not None and identity != sequence:
            raise ValueError('Split message identity changed')
        identity = sequence
        current_extended = packet[8] == 0
        if extended is not None and extended != current_extended:
            raise ValueError('Mixed split layouts')
        extended = current_extended
        if extended:
            if len(packet) < 21 or len(packet) > 1400:
                raise ValueError('Extended fragment length')
            magic, size, index, count = struct.unpack_from('<IIHH', packet, 9)
            if magic != 0x4b504347 or not 0 < size <= 65536 or count != (size + 1378) // 1379:
                raise ValueError('Extended fragment metadata')
            if index >= count or len(packet) - 21 != min(1379, size - index * 1379):
                raise ValueError('Extended fragment bounds')
            if total is not None and total != size:
                raise ValueError('Extended size changed')
            total = size
            payload = packet[21:]
        else:
            index, count = packet[8] >> 4, packet[8] & 15
            if not count or index >= count or len(packet) > 1400:
                raise ValueError('Native fragment bounds')
            payload = packet[9:]
        if expected is not None and expected != count:
            raise ValueError('Split fragment count changed')
        expected = count
        if index in pieces and pieces[index] != payload:
            raise ValueError('Conflicting fragment duplicate')
        pieces[index] = payload
    if len(pieces) != expected:
        raise ValueError('Missing split fragments')
    result = b''.join(pieces[i] for i in range(expected))
    if total is not None and len(result) != total:
        raise ValueError('Reassembled size differs')
    return result, bool(extended)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--engine', type=Path, default=ROOT / 'build/rehlds/Packets/swds.dll')
    parser.add_argument('--production-smoke', action='store_true',
                        help='Start the selected production engine and verify fixture commands are absent')
    args = parser.parse_args()
    engine = args.engine.resolve()
    if not engine.is_relative_to(ROOT) or not engine.is_file():
        raise ValueError('The compiled fixture engine must belong to this workspace')
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetStdHandle.restype = ctypes.c_void_p
    kernel.GetConsoleMode.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_uint)]
    console_mode = ctypes.c_uint()
    if not kernel.GetConsoleMode(kernel.GetStdHandle(-10), ctypes.byref(console_mode)):
        raise RuntimeError('Run in a console/PTY so ReHLDS receives a console input handle')
    inventory_text = subprocess.check_output(['powershell', '-NoProfile', '-Command',
        'Get-CimInstance Win32_Process -Filter "Name = \'hlds.exe\'" | '
        'Select-Object ExecutablePath | ConvertTo-Json -Compress'], text=True)
    inventory = json.loads(inventory_text) if inventory_text.strip() else []
    if isinstance(inventory, dict):
        inventory = [inventory]
    if any(str(item.get('ExecutablePath') or '').lower() == str(GAME / 'hlds.exe').lower() for item in inventory):
        raise RuntimeError('The independent packet fixture is already running')
    config = prepare(engine)
    remote = module('packet_rcon', 'GoldSrc-Command.py')
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    stamp = time.time_ns()
    evidence = EVIDENCE / f'run-{stamp}'
    evidence.mkdir()
    report = {'scope': __doc__, 'checks': {}, 'passed': False, 'evidence': evidence.relative_to(ROOT).as_posix(),
              'inputs': {str(path.relative_to(ROOT)): hashlib.sha256(path.read_bytes()).hexdigest() for path in
                         (engine, GAME / 'hlds.exe', GAME / 'cstrike/dlls/mp.dll',
                          AMXX / 'modules/goldcraft_amxx.dll', AMXX / 'plugins/goldcraft_headless_test.amxx')},
              'graphicalAcceptance': False}
    report['productionSmoke'] = args.production_smoke
    log_path = TEST / 'logs' / f'packets-{stamp}.log'
    log_path.parent.mkdir(parents=True, exist_ok=True)
    process = None

    def command(text):
        if process is not None and process.poll() is not None:
            raise RuntimeError(f'Independent ReHLDS exited {process.returncode}')
        return remote.command(text, config_path=TEST / 'cluster.json')

    def check(name, condition):
        report['checks'][name] = bool(condition)
        print(json.dumps({'check': name, 'passed': bool(condition)}), flush=True)
        if not condition:
            raise AssertionError(name)

    with log_path.open('wb') as log:
        try:
            environment = {key: value for key, value in os.environ.items() if not key.startswith('GOLDCRAFT_')}
            environment['GOLDCRAFT_HEADLESS_BINDINGS'] = str(TEST / 'bindings.bin')
            environment['GOLDCRAFT_PACKET_EVIDENCE'] = str(evidence)
            # Disable the unrelated JVM bridge listener for this native fixture.
            environment['GOLDCRAFT_SERVER_STATUS'] = str(TEST / 'logs/server-status.json')
            process = subprocess.Popen([str(GAME / 'hlds.exe'), '-game', 'cstrike', '-insecure', '-nomaster',
                '-console', '-num_edicts', '2048', '+ip', '127.0.0.1', '-port', str(config['csPort']),
                '-maxplayers', '4', '+sv_lan', '0', '+map', 'cs_assault'], cwd=GAME, env=environment,
                stdout=log, stderr=subprocess.STDOUT, stdin=None, creationflags=0)
            report['pid'] = process.pid
            deadline = time.monotonic() + 45
            while True:
                try:
                    answer = command('version')
                    if 'ReHLDS' in answer:
                        break
                except (OSError, RuntimeError):
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise
                time.sleep(.1)
            check('real ReHLDS starts on loopback without LAN authentication changes',
                  '"sv_lan" is "0"' in command('sv_lan') and f'127.0.0.1:{config["csPort"]}' in command('status'))
            if args.production_smoke:
                listing = command('cmdlist')
                (evidence / 'production-cmdlist.txt').write_text(listing, encoding='utf-8')
                check('production engine publishes its normal command list',
                      'changelevel' in listing and 'Command' in listing)
                forbidden = ('gc_packet_entities', 'gc_packet_transmit', 'gc_carve_delivery',
                             'gc_brush_identity')
                for name in forbidden:
                    reply = command('cmdlist ' + name)
                    (evidence / (name + '-cmdlist.txt')).write_text(reply, encoding='utf-8')
                    check('production engine omits ' + name + ' with fixture environment enabled',
                          bool(re.search(r'\b0 Commands for \[' + name + r'\]', reply)))
                report['passed'] = True
                return
            answer = command('gc_headless_create')
            match = re.search(r'slot=(\d+)', answer)
            if not match:
                raise RuntimeError('Native fixture player failed: ' + answer)
            slot = int(match[1])
            answer = command(f'gc_packet_entities {slot}')
            match = re.search(r'GC_PACKETS (\{[^\r\n]+\})', answer)
            if not match:
                raise RuntimeError('Missing actual encoder result: ' + answer)
            report['encoder'] = json.loads(match[1])
            check('actual GameDLL and full/delta encoder cases pass', report['encoder']['passed'])
            cases = json.loads((evidence / 'cases.json').read_text())
            report['cases'] = cases
            for row in cases['cases']:
                wire = (evidence / (row['name'] + '.bin')).read_bytes()
                states = (evidence / (row['name'] + '.states')).read_bytes()
                check(row['name'] + ' actual SHORT count and retained state buffer',
                      struct.unpack_from('<h', wire, 1)[0] == row['count'] and len(states) == row['count'] * 340)
            check('1025th candidate is filtered before packing outside storage',
                  next(r for r in cases['cases'] if r['name'] == 'wide-1025-boundary')['nativeCalls'] == 1024)
            check('capability downgrade performs full refresh',
                  next(r for r in cases['cases'] if r['name'] == 'downgrade-full')['service'] == 40)
            check('unchanged delta carries 1024 previous entities',
                  next(r for r in cases['cases'] if r['name'] == 'wide-unchanged')['bytes'] <= 8)
            check('oversized hook packet rejected', cases['invalid1025Rejected'])
            check('actual native event reader retains 1023 index and 1024 sentinel', cases['event1023And1024Decoded'])
            report['budget'] = json.loads((evidence / 'budget.json').read_text())
            check('actual high-change delta respects finite datagram budget without out-of-bounds writes',
                  report['budget']['guardUnchanged'] and report['budget']['partialCleared'] and
                  report['budget']['deltaOverflow'] ==
                  (report['budget']['deltaBytes'] > report['budget']['datagramBudget']))
            check('actual normal 1024 encoder recovers after clearing an over-budget snapshot',
                  report['budget']['normal1024Recovery'] and
                  report['budget']['recoveryBytes'] <= report['budget']['datagramBudget'])
            report['transport'] = []
            for mode in range(8):
                with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as receiver:
                    receiver.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 4 * 1024 * 1024)
                    receiver.bind(('127.0.0.1', 0))
                    receiver.settimeout(.3)
                    port = receiver.getsockname()[1]
                    answer = command(f'gc_packet_transmit {slot} {port} {mode}')
                    match = re.search(r'GC_TRANSPORT (\{[^\r\n]+\})', answer)
                    if not match:
                        raise RuntimeError('Missing actual transport result: ' + answer)
                    row = json.loads(match[1])
                    if row.get('passed') is False:
                        raise RuntimeError('Actual transport fixture failed: ' + row.get('error', 'unknown'))
                    packets = []
                    while True:
                        try:
                            packet, address = receiver.recvfrom(65535)
                            if address != ('127.0.0.1', config['csPort']):
                                raise ValueError('UDP source differs from independent ReHLDS')
                            packets.append(packet)
                        except socket.timeout:
                            break
                for i, packet in enumerate(packets):
                    (evidence / f'transport-{mode}-{i}.udp').write_bytes(packet)
                datagrams = evidence / f'transport-{mode}.datagrams'
                datagrams.write_bytes(b''.join(struct.pack('<H', len(packet)) + packet for packet in packets))
                assembled, extended = reassemble(packets)
                expected = (evidence / f'transport-{mode}.netchan').read_bytes()
                check(f'transport{mode} actual UDP matches actual native netchan output', assembled == expected)
                check(f'transport{mode} negotiated wire format', extended == (mode not in (0, 4)))
                if mode == 4:
                    check('ordinary recipient oversized unreliable data keeps old drop behavior', len(assembled) == 16)
                else:
                    check(f'transport{mode} complete unreliable payload retained',
                          len(assembled) == row['payload'] + 8 + (128 if mode in (3, 6) else 0))
                if mode == 7:
                    check('actual SV_SendClientDatagram retains 1024 entities and 257 matching GCBrush identities',
                          row['entities'] == 1024 and row['identityTotal'] == 257 and row['identityBytes'] == 3012)
                    check('actual GCBrush omits its entire sideband when one byte short',
                          json.loads((evidence / 'production.json').read_text())['atomicInsufficientSpace'])
                if extended:
                    backwards, _ = reassemble(packets[::-1] + packets[:1])
                    check(f'transport{mode} independent reverse-order and duplicate replay', backwards == expected)
                row.update(name=f'transport-{mode}', datagrams=datagrams.name,
                           expected=f'transport-{mode}.netchan',
                           fragments=len(packets), extended=extended, capturedBytes=len(assembled))
                report['transport'].append(row)
                (evidence / 'transport.json').write_text(json.dumps({'cases': report['transport']}, indent=2))
            (evidence / 'transport.json').write_text(json.dumps({'cases': report['transport']}, indent=2))
            report['outputs'] = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in evidence.iterdir() if p.is_file()}
            report['passed'] = True
        except Exception as error:
            report['error'] = str(error)
            raise
        finally:
            if process and process.poll() is None:
                try:
                    command('quit')
                except (OSError, RuntimeError):
                    pass
                try:
                    process.wait(timeout=8)
                except subprocess.TimeoutExpired:
                    process.terminate()
                    process.wait(timeout=8)
            report['exitCode'] = process.returncode if process else None
            report['outputs'] = {p.relative_to(ROOT).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                                 for p in evidence.iterdir() if p.is_file()}
            report['log'] = log_path.relative_to(ROOT).as_posix()
            output = EVIDENCE / f'native-{stamp}.json'
            output.write_text(json.dumps(report, indent=2), encoding='utf-8')
            print(json.dumps({'report': str(output), 'passed': report['passed']}), flush=True)


if __name__ == '__main__':
    main()
