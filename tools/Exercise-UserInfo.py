"""Check online userinfo edits through the opted-in single-B sandbox channel.

Touches only temporary gcuit_ fields, never gameplay input, reconnect or a map.
The live client/server must already run the candidate. No credentials or full
userinfo strings are written to the evidence report.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import time

ROOT = Path(__file__).resolve().parent.parent
STATUS = ROOT / 'sandbox/cs-client-b/logs/goldcraft-client-status.json'
SERVER = ROOT / 'sandbox/cs-server/logs/goldcraft-server-status.json'


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, ROOT / 'tools' / filename)
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


def fresh(path):
    deadline = time.monotonic() + 1
    while True:
        try:
            result = json.loads(path.read_text(encoding='utf-8-sig'))
            if time.time() - path.stat().st_mtime > 3:
                raise RuntimeError('Live diagnostics stopped updating')
            return result
        except (OSError, json.JSONDecodeError):
            # The live server rewrites its diagnostics in place. A partial
            # read is not evidence that the game or connection stopped.
            if time.monotonic() >= deadline:
                raise
            time.sleep(.02)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check-only', action='store_true')
    args = parser.parse_args()
    client = module('renderer_command', 'Exercise-RendererPerformance.py')
    rcon = module('sandbox_rcon', 'GoldSrc-Command.py')
    first = fresh(STATUS)
    info = first.get('userinfo', {})
    if info.get('commandGuard') != 1 or info.get('capacity') != 1024:
        raise RuntimeError('Candidate with userinfo command guard is not running; no changes made')
    slot, world = first['playerSlot'], first['world']
    server = fresh(SERVER)
    actor = next(a for a in server['actors'] if a['slot'] == slot)
    if str(server['world']) != str(world) or not actor['uuid'].strip('0-'):
        raise RuntimeError('Single B is not paired with this local server')
    serial = actor['serial']
    users = re.findall(r'^\s*(\d+) : .* : .+$', rcon.command('users'), re.M)
    if len(users) != 1:
        raise RuntimeError('This check requires exactly one real local CS player')
    userid = users[0]

    def state():
        value = fresh(STATUS)
        current_server = fresh(SERVER)
        current = next((a for a in current_server['actors'] if a['slot'] == slot), {})
        if (value['world'] != world or value['playerSlot'] != slot
                or str(current_server['world']) != str(world) or current.get('serial') != serial):
            raise RuntimeError('Map or connection changed during the online check')
        return value['userinfo']

    def remote():
        # Info_Print pads short keys to 20 columns; probe keys are all shorter.
        text = rcon.command('user ' + userid)
        result = {line[:20].strip(): line[20:].rstrip('\r') for line in text.splitlines()
                  if len(line) >= 20 and line[:20].strip() and ' ' not in line[:20].strip()}
        if result.get('_gcu') != '1':
            raise RuntimeError('Server no longer reports the extended player')
        return result

    baseline = remote()
    if any(k.startswith('gcuit_') for k in baseline):
        raise RuntimeError('Existing gcuit_ fields must be resolved before this check')
    if args.check_only:
        print(json.dumps({'ready': True, 'mutated': False, 'slot': slot}))
        return
    output = ROOT / 'build/userinfo-live-edit-20261019'
    output.mkdir(parents=True, exist_ok=True)
    report = {'checks': [], 'passed': False, 'cleanupPassed': False,
              'scope': 'Actual single-B local client/server commands and received player-info API; no reconnect or multiplayer acceptance.'}
    touched = []

    def change(key, value, expected=None):
        before = state()
        if key not in touched:
            touched.append(key)
        client.command(f'userinfo_set "{key}" "{value}" {slot}')
        desired = value if expected is None else expected
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            current = state()
            if (current.get('probeKey') == key and current.get('probeLocal') == desired
                    and (expected is None or current['rejected'] > before['rejected'])
                    and current.get('probeReceived') == desired and remote().get(key, '') == desired):
                return current
            time.sleep(.1)
        raise AssertionError('Local storage, received player-info API and server differ for ' + key)

    try:
        edit = 'a' * 16
        change('gcuit_edit', edit)
        report['checks'].append('Online addition reaches local storage, server and received player-info API')
        for index in range(16):
            remaining = 991 - state()['localBytes']
            if remaining <= 0:
                break
            key = f'gcuit_{index:02d}'
            overhead = len(key) + 2
            if remaining <= overhead:
                edit += 'a' * remaining
                change('gcuit_edit', edit)
                break
            change(key, 'x' * min(120, remaining - overhead))
        full = state()
        assert full['localBytes'] == 991
        report['checks'].append('Online fields reach 991-byte local budget without losing admission fields')
        edit = 'b' * len(edit)
        change('gcuit_edit', edit)
        report['checks'].append('Full-buffer same-size replacement reaches all three views')
        before_reject = state()['rejected']
        change('gcuit_edit', edit + 'c', expected=edit)
        rejected = state()
        assert rejected['rejected'] == before_reject + 1 and rejected['localBytes'] == 991
        # Wait after rejection as well, so a delayed forwarded command cannot
        # falsely pass against the previous server/roster values.
        until = time.monotonic() + 2
        while time.monotonic() < until:
            current = state()
            assert current['probeLocal'] == current['probeReceived'] == edit
            assert remote().get('gcuit_edit') == edit
            time.sleep(.1)
        report['checks'].append('Over-capacity replacement is rejected once and preserves all three views')
        change('gcuit_edit', '')
        change('gcuit_edit', 'restored')
        report['checks'].append('Deletion frees capacity and permits a subsequent addition')
        report['passed'] = True
    except Exception as error:
        report['error'] = str(error)
        raise
    finally:
        try:
            for key in reversed(touched):
                change(key, '')
            final = state()
            assert final['localBytes'] == info['localBytes'], 'Local byte count was not restored'
            restored = remote()
            changed = sorted(k for k in baseline.keys() | restored.keys() if baseline.get(k) != restored.get(k))
            report['serverFieldsChangedDuringPlay'] = changed
            # ZP changes the server-side player model on infection/cure while
            # this non-disruptive check runs. That live gameplay change is not
            # a probe field to restore or an input setting changed by us.
            assert set(changed) <= {'model'}, 'Unrelated server fields changed: ' + ','.join(changed)
            assert not any(k.startswith('gcuit_') for k in restored), 'Temporary fields remain on server'
            report['cleanupPassed'] = True
            report['checks'].append('All temporary fields removed; initial byte count, identity and admission fields preserved')
        except Exception as error:
            report['cleanupError'] = str(error)
            report['passed'] = False
        report['finishedAt'] = time.time()
        (output / 'live-edits.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
        print(json.dumps(report))
    if not report['passed']:
        raise RuntimeError('Online check did not complete cleanly; inspect its evidence before retrying')


if __name__ == '__main__':
    main()
