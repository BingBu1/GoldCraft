"""Verify real CS/MC death, team, respawn and block interaction without teleporting lifecycle fixtures."""
import argparse
import importlib.util
import json
import math
import re
import time
from pathlib import Path


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(file))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


r = module('lifecycle_renderer', 'Exercise-Renderer.py')
cs = module('lifecycle_cs', 'GoldSrc-Command.py')
DIM = 'goldcraft:cs_assault_f6725c06'
MC_STATUS = r.ROOT / 'sandbox/cs-client-a/logs/minecraft-server-status.json'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--phase', choices=('lifecycle', 'interaction', 'all'), default='all')
    args = parser.parse_args()
    boxes = {name: r.Sandbox('cs-client-' + name.lower()) for name in ('A', 'B')}
    a, b = boxes.values()
    prefix = r.ROOT / 'analysis/goldcraft-tests' / f'player-lifecycle-{int(time.time())}'
    report = {'scope': __doc__, 'phase': args.phase, 'checks': {}, 'states': {}, 'observations': [],
              'minecraftCommands': b.commands, 'nativeCommands': [], 'captures': {}}
    cvars, originals = {}, {}
    keep_inventory = None
    fixture = False
    phase = 'initial'

    def save():
        prefix.with_suffix('.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    def command(text):
        reply = cs.command(text)
        report['nativeCommands'].append({'command': text, 'response': reply})
        return reply

    def sample():
        native = r.read_json(r.SERVER_STATUS)
        mc = r.read_json(MC_STATUS)
        state = {'time': time.time(), 'phase': phase, 'world': native['world'], 'map': native['map'],
                 'dedicated': native['dedicated'], 'staleLifePoses': native['staleLifePoses'], 'mcWorld': mc['world']}
        for name, box in boxes.items():
            view = box.status()
            java = r.read_json(box.logs / 'minecraft-client-status.json')
            actor = next((p for p in native['actors'] if p['slot'] == view['playerSlot']), None)
            player = next((p for p in mc['players'] if p['uuid'] == java.get('player')), None)
            state[name] = {'actor': actor, 'player': player,
                'client': {k: java.get(k) for k in ('world', 'serial', 'life', 'position', 'controlling', 'screen', 'inputButtons', 'entityId')},
                'native': {k: view.get(k) for k in ('world', 'playerSlot', 'playerSerial', 'playerLife', 'minecraftFeet', 'minecraftControl', 'avatarCount', 'entityBatches', 'glError', 'inputButtons', 'viewAngles')}}
        report['observations'].append(state)
        return state

    def aligned(state, name='B'):
        pair = state[name]
        actor, player, client, view = (pair[k] for k in ('actor', 'player', 'client', 'native'))
        if not actor or not player or not client['position']:
            return False
        if not (state['dedicated'] and state['map'] == 'cs_assault' and actor['alive'] and actor['controlled']
                and player['enabled'] and player['alive'] and player['handlerMatchesPlayer']
                and player['trackedEntityMatchesPlayer'] and not player['teleportPending']
                and client['controlling'] and view['minecraftControl']):
            return False
        if len({state['world'], state['mcWorld'], client['world'], view['world']}) != 1:
            return False
        if len({actor['serial'], player['serial'], client['serial'], view['playerSerial']}) != 1:
            return False
        if len({actor['life'], player['life'], client['life'], view['playerLife']}) != 1:
            return False
        x, y, z = actor['origin']
        host_feet = [x / 32, (z - 36) / 32 + 64, -y / 32]
        return (player['dimension'] == DIM and player['position'][1] > 50
                and max(math.dist(player['position'], p) for p in (host_feet, client['position'], view['minecraftFeet'])) < .04)

    def wait(predicate, seconds=25):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            state = sample()
            if predicate(state):
                return state
            time.sleep(.12)
        raise TimeoutError(f'{phase}: runtime condition did not complete')

    def check(name, condition):
        report['checks'][name] = bool(condition)
        save()
        if not condition:
            raise AssertionError(name)

    def settled(name='B'):
        return wait(lambda s: aligned(s, name) and s[name]['native']['inputButtons'] == 0)

    def spawn_matches(state):
        pair = state['B']
        pos, spawn = pair['player']['position'], pair['actor']['spawnFeet']
        return math.dist([pos[0], pos[2]], [spawn[0], spawn[2]]) < .04 and abs(pos[1] - spawn[1]) < 1

    def mc(text):
        return b.minecraft(f'execute in {DIM} run {text}')

    def block(position, name):
        return 'Test passed' in mc(f"execute if block {' '.join(map(str, position))} minecraft:{name}")

    try:
        initial = wait(lambda s: aligned(s, 'A') and aligned(s, 'B'))
        report['states']['initial'] = initial
        check('initial_all_three_positions_agree', aligned(initial, 'A') and aligned(initial))
        for name in ('mp_forcerespawn', 'mp_round_infinite', 'humans_join_team'):
            reply = command(name)
            match = re.search(r'is "([^"]*)"', reply)
            if not match:
                raise RuntimeError('Could not preserve sandbox cvar ' + name)
            cvars[name] = match[1]
        command('mp_forcerespawn 1')
        command('mp_round_infinite 1')
        command('humans_join_team any')

        if args.phase != 'interaction':
            phase = 'native_suicide'
            before = settled()
            b.host('kill')
            dead = wait(lambda s: s['B']['actor'] and not s['B']['actor']['alive'], 10)
            report['states']['nativeDeath'] = dead
            check('native_death_releases_movement', not dead['B']['actor']['controlled'])
            after = wait(lambda s: aligned(s) and s['B']['actor']['life'] > before['B']['actor']['life'])
            report['states']['nativeRespawn'] = after
            check('native_respawn_uses_same_connection_new_life', after['B']['actor']['serial'] == before['B']['actor']['serial'])
            check('native_respawn_matches_actual_CS_spawn', spawn_matches(after))
            print('CS suicide and respawn positions agree', flush=True)

            phase = 'opposing_team'
            other_team = 1 if after['A']['actor']['team'] == 2 else 2
            b.host(f'team {other_team}')
            switched = wait(lambda s: s['B']['actor'] and s['B']['actor']['team'] == other_team)
            report['states']['teamChanged'] = switched
            command('sv_restartround 1')
            after = wait(lambda s: aligned(s, 'A') and aligned(s)
                         and s['B']['actor']['life'] > switched['B']['actor']['life']
                         and s['A']['actor']['life'] > switched['A']['actor']['life'])
            report['states']['teamRespawn'] = after
            check('opposing_teams_share_correct_world', after['B']['actor']['team'] != after['A']['actor']['team'])
            check('team_respawn_matches_actual_CS_spawn', spawn_matches(after))
            check('round_restart_relocates_both_players_with_new_lives',
                  after['world'] == switched['world'] and aligned(after, 'A') and aligned(after))

            phase = 'minecraft_death'
            before = settled()
            keep_inventory = b.minecraft('gamerule keepInventory').strip().endswith('true')
            b.minecraft('gamerule keepInventory true')
            reply = b.minecraft('kill GoldCraft_B')
            if 'Killed GoldCraft_B' not in reply:
                raise RuntimeError(reply)
            time.sleep(.8)
            after = settled()
            report['states']['minecraftRespawn'] = after
            check('minecraft_respawn_keeps_handler_on_current_player', after['B']['player']['handlerMatchesPlayer'])
            check('minecraft_respawn_preserves_host_life', after['B']['actor']['life'] == before['B']['actor']['life'])
            check('minecraft_respawn_does_not_use_void_spawn', math.dist(after['B']['player']['position'], before['B']['player']['position']) < .04)
            check('respawn_reuses_entity_ID_safely', after['B']['player']['entityId'] == before['B']['player']['entityId'])
            print('MC death and replacement player stay synchronized', flush=True)

            phase = 'native_reconnect'
            before = settled()
            b.host('disconnect')
            wait(lambda s: s['B']['actor'] is None, 8)
            b.host('reconnect')
            after = wait(lambda s: aligned(s) and s['B']['actor']['serial'] > before['B']['actor']['serial'], 35)
            report['states']['reconnected'] = after
            check('reconnect_uses_new_connection_at_actual_spawn', spawn_matches(after))

        if args.phase != 'lifecycle':
            phase = 'interaction_fixtures_after_lifecycle'
            before = settled()
            if before['A']['actor']['team'] == before['B']['actor']['team']:
                b.host('team 1' if before['A']['actor']['team'] == 2 else 'team 2')
                wait(lambda s: s['B']['actor'] and s['A']['actor']['team'] != s['B']['actor']['team'])
                command('sv_restartround 1')
                time.sleep(2)
                settled()
            for name, box in boxes.items():
                original = settled(name)[name]
                originals[name] = {'position': original['player']['position'], 'view': original['native']['viewAngles']}
                box.host('menu_close')
            for name, position, yaw in (('A', '8.5 64.0009765625 -5.5', 0), ('B', '12.5 64.0009765625 -5.5', 180)):
                mc(f'tp {boxes[name].player} {position} {-yaw-90} 0')
                boxes[name].host(f'view {yaw} 0')
            visible = wait(lambda s: aligned(s, 'A') and aligned(s)
                           and s['A']['native']['avatarCount'] == s['B']['native']['avatarCount'] == 1)
            report['states']['opposingTeamsVisible'] = visible
            check('both_teams_receive_each_other_avatar', visible['A']['actor']['team'] != visible['B']['actor']['team'])
            for name, box in boxes.items():
                report['captures']['opposing-' + name] = box.capture(prefix.with_name(prefix.name + '-opposing-' + name + '.bmp'))
            anchor, placed = (16, 65, -6), (15, 65, -6)
            if not block(anchor, 'air') or not block(placed, 'air'):
                raise RuntimeError('Reserved fixture is occupied; existing construction was preserved')
            mc('setblock 16 65 -6 minecraft:stone keep')
            fixture = True
            b.host('view 0 0')
            b.minecraft('item replace entity GoldCraft_B weapon.mainhand with minecraft:lime_wool 1')
            time.sleep(.65)
            b.host('attack2 0.12')
            deadline = time.monotonic() + 4
            while not block(placed, 'lime_wool') and time.monotonic() < deadline:
                time.sleep(.08)
            check('CS_right_click_places_block_on_server', block(placed, 'lime_wool'))
            report['captures']['placed-B'] = b.capture(prefix.with_name(prefix.name + '-placed-B.bmp'))
            b.host('attack 1.8')
            deadline = time.monotonic() + 4
            while not block(placed, 'air') and time.monotonic() < deadline:
                time.sleep(.08)
            check('CS_left_click_breaks_block_on_server', block(placed, 'air'))
            time.sleep(.7)
            report['captures']['removed-B'] = b.capture(prefix.with_name(prefix.name + '-removed-B.bmp'))
            report['states']['afterBreaking'] = settled()
            check('block_actions_keep_three_positions_aligned', aligned(report['states']['afterBreaking']))

        report['outcome'] = 'passed'
    except Exception as error:
        report['outcome'] = 'incomplete'
        report['error'] = str(error)
        try:
            report['states']['failure'] = sample()
        except Exception as secondary:
            report['observationError'] = str(secondary)
    finally:
        try:
            if fixture:
                mc('execute if block 16 65 -6 minecraft:stone run setblock 16 65 -6 minecraft:air')
                mc('execute if block 15 65 -6 minecraft:lime_wool run setblock 15 65 -6 minecraft:air')
            for name, original in originals.items():
                mc(f"tp {boxes[name].player} {' '.join(map(str, original['position']))}")
                boxes[name].host(f"view {original['view'][1]} {original['view'][0]}")
            if keep_inventory is not None:
                b.minecraft('gamerule keepInventory ' + str(keep_inventory).lower())
            for name, value in cvars.items():
                command(f'{name} "{value}"')
        except Exception as error:
            report['cleanupError'] = str(error)
        for box in boxes.values():
            box.connection.close()
        save()
    print(json.dumps({'report': str(prefix.with_suffix('.json')), 'outcome': report['outcome'],
                      'checks': report['checks'], 'error': report.get('error')}, indent=2))
    return report['outcome'] == 'passed'


if __name__ == '__main__':
    raise SystemExit(0 if main() else 1)
