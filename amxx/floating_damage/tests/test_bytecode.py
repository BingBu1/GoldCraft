"""Run the production AMXX bytecode with the pinned Pawn VM and fake engine natives.

This verifies plugin execution, not a live ReHLDS server, network client or rendering.
"""
import collections
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'build/floating-damage-20261018'
SDK = ROOT / '.tools/reapi-5.29.0.358/addons/amxmodx/scripting/include'
AMXX = ROOT / '.tools/amxx-1.9.0.5303/addons/amxmodx/scripting/include'


def clean(path):
    return re.sub(r'/\*.*?\*/|//[^\n]*', '', path.read_text(encoding='utf-8-sig'), flags=re.S)


def enum(path, name, base=0):
    match = re.search(r'\benum\s+' + re.escape(name) + r'\s*\{([^}]+)\}', clean(path))
    result = {}
    value = base
    for item in match[1].split(','):
        item = item.strip()
        if not item:
            continue
        parts = item.split('=')
        if len(parts) == 2:
            expr = parts[1].strip()
            if not expr.startswith('BEGIN_MEMBER_REGION'):
                value = int(expr)
        result[parts[0].strip()] = value
        value += 1
    return result


TABLE = enum(SDK / 'reapi.inc', 'members_tables_e')
VARS = enum(SDK / 'reapi_engine_const.inc', 'EntVars', 1024 * TABLE['mt_entvars'])
NAMES = {v: k for k, v in VARS.items()}
MEMBERS = enum(SDK / 'reapi_gamedll_const.inc', 'CBasePlayer_Members', 1024 * TABLE['mt_player'])
MEMBERS.update(enum(SDK / 'reapi_gamedll_const.inc', 'CBaseMonster_Members', 1024 * TABLE['mt_basemonster']))
FUNCS = re.findall(r'\bEngFunc_\w+', clean(AMXX / 'fakemeta_const.inc'))
FUNCS = list(dict.fromkeys(FUNCS))


def cell(value):
    return struct.unpack('<i', struct.pack('<f', value))[0]


def number(value):
    return struct.unpack('<f', struct.pack('<i', value))[0]


def extract():
    packed = (OUT / 'addon_floating_damage.amxx').read_bytes()
    width, disk, image, memory, offset = struct.unpack_from('<BIIII', packed, 7)
    assert width == 4
    raw = zlib.decompress(packed[offset:offset + disk])
    assert len(raw) == image
    output = OUT / 'addon_floating_damage.amx'
    output.write_bytes(raw)
    return output


class Engine:
    def __init__(self):
        self.proc = subprocess.Popen([str(OUT / 'pawn_runner.exe'), str(extract())],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                     text=True, bufsize=1)
        assert self.read() == 'READY'
        self.send('var MaxClients 32')
        assert self.read() == 'V'
        self.time = 10.0
        self.entities = {}
        self.next_entity = 33
        self.connected = {1, 2, 3}
        self.alive = {1, 2, 3}
        self.teams = {1: 1, 2: 2, 3: 1}
        self.hitgroup = 0
        self.hook_success = 1
        self.fail_after = None
        self.native_args = []
        self.natives = collections.Counter()
        self.checks = []
        for player in self.connected:
            self.entities[player] = {'var_origin': [cell(player * 100), 0, 0],
                                     'var_view_ofs': [0, 0, cell(17)], 'var_v_angle': [0, 0, 0]}
            self.call('client_putinserver', player)

    def send(self, line):
        self.proc.stdin.write(line + '\n')
        self.proc.stdin.flush()

    def read(self):
        line = self.proc.stdout.readline().strip()
        if not line:
            raise RuntimeError(f'Pawn VM exited: {self.proc.poll()} {self.proc.stderr.read()}')
        return line

    def get(self, addr, count=1):
        self.send(f'get {addr} {count}')
        line = self.read().split()
        assert line[0] == 'M'
        return list(map(int, line[1:]))

    def put(self, addr, values):
        self.send(f'set {addr} {len(values)} ' + ' '.join(map(str, values)))
        assert self.read() == 'W'

    def string(self, addr):
        result = []
        while True:
            value = self.get(addr)[0]
            if value == 0:
                return ''.join(result)
            result.append(chr(value))
            addr += 4

    def call(self, name, *args):
        self.send('call ' + name + ' ' + ' '.join(map(str, args)))
        while True:
            line = self.read().split()
            if line[0] == 'R':
                assert int(line[1]) == 0, (name, args, line)
                return int(line[2])
            assert line[0] == 'N', line
            native, params = line[1], list(map(int, line[2:]))
            self.natives[native] += 1
            try:
                result = self.native(native, params)
            except Exception:
                self.proc.kill()
                raise
            self.send(f'ret {result}')

    def native(self, name, p):
        if name == 'get_gametime':
            return cell(self.time)
        if name == 'is_user_connected':
            return int(p[0] in self.connected)
        if name == 'is_user_alive':
            return int(p[0] in self.alive)
        if name == 'is_entity':
            return int(p[0] in self.entities)
        if name == 'FClassnameIs':
            return int(self.entities[p[0]].get('var_classname') == self.string(p[1]))
        if name == 'GetHookChainReturn':
            return self.hook_success
        if name in ('get_member', 'get_member_s'):
            if p[1] == MEMBERS['m_iTeam']:
                return self.teams[p[0]]
            assert p[1] == MEMBERS['m_LastHitGroup'], p
            return self.hitgroup
        if name == 'set_entvar':
            key = NAMES[p[1]]
            if key == 'var_classname':
                value = self.string(p[2])
            elif key in ('var_origin', 'var_angles', 'var_rendercolor'):
                value = self.get(p[2], 3)
            else:
                value = self.get(p[2])[0]
            self.entities[p[0]][key] = value
            return 1
        if name == 'get_entvar':
            key = NAMES[p[1]]
            value = self.entities[p[0]].get(key, 0)
            if len(p) > 2:
                self.put(p[2], value if isinstance(value, list) else [value])
                return 1
            return value
        if name == 'rg_create_entity':
            if self.fail_after == 0:
                return 0
            if self.fail_after is not None:
                self.fail_after -= 1
            ent = self.next_entity
            self.next_entity += 1
            self.entities[ent] = {'var_classname': self.string(p[0])}
            return ent
        if name == 'engfunc':
            func = FUNCS[p[0]]
            if func == 'EngFunc_RemoveEntity':
                del self.entities[self.get(p[1])[0]]
            elif func == 'EngFunc_SetOrigin':
                self.entities[self.get(p[1])[0]]['var_origin'] = self.get(p[2], 3)
            elif func == 'EngFunc_SetModel':
                self.entities[self.get(p[1])[0]]['model'] = self.string(p[2])
            elif func == 'EngFunc_PrecacheModel':
                assert self.string(p[1]) == 'models/x_re/floating_damage.mdl'
            else:
                raise AssertionError(func)
            return 1
        if name == 'SetThink':
            self.entities[p[0]]['think'] = self.string(p[1])
            return 1
        if name == 'num_to_str':
            s = str(p[0])[:p[2]]
            self.put(p[1], [ord(c) for c in s] + [0])
            return len(s)
        if name == 'random_float':
            return cell((number(p[0]) + number(p[1])) * .5)
        if name == 'floatround':
            assert p[1] == 0
            return math.floor(number(p[0]) + .5)
        if name == 'floatsqroot':
            return cell(math.sqrt(number(p[0])))
        if name == 'float':
            return cell(p[0])
        if name in ('floatadd', 'floatsub', 'floatmul', 'floatdiv'):
            a, b = map(number, p)
            return cell({'floatadd':lambda:a+b, 'floatsub':lambda:a-b,
                         'floatmul':lambda:a*b, 'floatdiv':lambda:a/b}[name]())
        if name == 'floatcmp':
            a, b = map(number, p)
            return 1 if a>b else -1 if a<b else 0
        if name == 'angle_vector':
            pitch, yaw, roll = map(lambda x: math.radians(number(x)), self.get(p[0], 3))
            sp, cp, sy, cy, sr, cr = math.sin(pitch), math.cos(pitch), math.sin(yaw), math.cos(yaw), math.sin(roll), math.cos(roll)
            assert p[1] == 2
            self.put(p[2], list(map(cell, [-sr*sp*cy+cr*sy, -sr*sp*sy-cr*cy, -sr*cp])))
            return 1
        if name == 'get_param':
            return self.native_args[p[0]-1]
        if name == 'clamp':
            return max(p[1], min(p[2], p[0]))
        if name.startswith('register_') or name in ('RegisterHookChain', 'create_cvar'):
            return 1
        raise AssertionError((name, p))

    def hit(self, damage, attacker=1, victim=2):
        self.call('CBasePlayer__TakeDamage_Post', victim, attacker, attacker, cell(damage), 2)
        self.time += .01

    def groups(self, owner=1):
        groups = collections.defaultdict(list)
        for ent, data in self.entities.items():
            if data.get('var_classname') == 'ent_floating_damage' and data.get('var_owner') == owner:
                groups[data['var_iuser2']].append((ent, data))
        return {token: sorted(items, key=lambda x: x[1]['var_iuser3']) for token, items in sorted(groups.items())}

    def values(self, owner=1):
        return [int(''.join(str(min(255, d['var_body'])-1) for _, d in items)) for items in self.groups(owner).values()]

    def check(self, condition, label):
        assert condition, label
        self.checks.append(label)

    def clear(self):
        self.call('CDamager__ClearAll')
        self.check(not self.groups(1) and not self.groups(3), 'clear all removes events for both attackers')

    def tick(self, when):
        self.time = when
        for ent in list(self.entities):
            data = self.entities.get(ent)
            if data and data.get('think') and number(data.get('var_nextthink', 0)) <= when:
                self.call(data['think'], ent)

    def run(self):
        self.call('plugin_natives')
        self.call('plugin_precache')
        self.call('plugin_init')
        self.call('plugin_cfg')
        self.hit(101)
        first = next(iter(self.groups().values()))
        first_snapshot = {ent: dict(data) for ent, data in first}
        self.hit(102)
        self.check(self.values() == [101, 102], 'same victim hits remain independent, no sum')
        self.check(all(self.entities[e] == d for e, d in first_snapshot.items()), 'new hit does not change old position, animation or lifetime')
        for value in (9999, 10000, 100000):
            self.hit(value)
        self.check(self.values() == [101, 102, 9999, 10000, 100000], 'five active events include five and six digit damage')
        self.hit(777, attacker=3)
        other = self.groups(3)
        self.hit(2147483520)
        self.check(self.values() == [102, 9999, 10000, 100000, 2147483520], 'sixth hit evicts only oldest full event')
        self.check(all(ent not in self.entities for ent in first_snapshot), 'eviction immediately removes every oldest glyph')
        self.check(self.groups(3) == other, 'other attacker queue is not evicted or changed')
        self.hit(1e30)
        self.check(self.values()[-1] == 2147483647, 'overflow safely clamps before float to int conversion')
        self.check(all(1 <= d['var_body'] <= 10 and d['var_effects'] == 4096 and d.get('think') for g in self.groups().values() for _, d in g), 'all glyphs fit 8-bit body, retain owner visibility and own expiry')
        before = self.values()
        for damage, attacker, victim in [(0, 1, 2), (-1, 1, 2), (float('nan'), 1, 2), (30, 1, 1), (30, 1, 3), (30, 0, 2), (30, 33, 2)]:
            self.hit(damage, attacker, victim)
        self.hook_success = 0
        self.hit(123)
        self.hook_success = 1
        self.check(self.values() == before, 'zero, negative, NaN, self, teammate, invalid attacker and rejected damage create no event')
        self.clear()
        created = self.time
        self.hit(12345)
        initial = {e: number(d['var_origin'][2]) for e, d in next(iter(self.groups().values()))}
        self.tick(created + .6)
        self.check(all(abs(number(self.entities[e]['var_origin'][2])-z-38.4)<.001 for e,z in initial.items()), 'actual Think moves complete event upward by elapsed time')
        self.check(all(d['var_sequence']==1 and abs(number(d['var_renderamt'])-165)<.001 for _,d in next(iter(self.groups().values()))), 'actual Think begins end sequence and fades each glyph')
        self.tick(created + 1.11)
        self.check(not self.groups(), 'whole event expires at original 1.1 second lifetime')
        created = self.time
        self.hit(100000)
        items = next(iter(self.groups().values()))
        anchor = items[0][0]
        del self.entities[anchor]
        self.tick(created + 1.11)
        self.check(not self.groups(), 'externally deleted anchor does not leave orphan glyphs at expiry')
        self.hit(123)
        items = next(iter(self.groups().values()))
        reused = items[1][0]
        self.entities[reused] = {'var_classname':'unrelated_entity'}
        self.tick(self.time + .1)
        self.check(self.entities.get(reused)=={'var_classname':'unrelated_entity'} and not self.groups(), 'external entity reuse is preserved while damaged event is cleared')
        self.fail_after = 2
        self.hit(100000)
        self.fail_after = None
        self.check(not self.groups(), 'allocation failure removes all partially created glyphs')
        self.hit(50)
        self.hit(60, attacker=3)
        self.native_args = [1, 0]
        self.call('native_set_user_damager_status', 0, 2)
        self.check(not self.groups() and self.values(3)==[60], 'disabling clears only that player events')
        self.hit(70)
        self.check(not self.groups(), 'disabled player produces no new event')
        self.native_args = [1, 1]
        self.call('native_set_user_damager_status', 0, 2)
        self.native_args = [1, 9]
        self.call('native_set_user_damager_skin', 0, 2)
        self.hitgroup = 1
        self.hit(88)
        self.check(all(d['var_skin']==19 for _,d in next(iter(self.groups().values()))), 'existing skin selection and headshot alternative are retained')
        self.call('CBasePlayer__Spawn_Post', 1)
        self.check(not self.groups() and self.values(3)==[60], 'respawn removes only owner queue')
        self.hit(99)
        self.connected.remove(1)
        self.call('client_disconnected', 1)
        self.check(not self.groups(), 'disconnect clears all owner glyphs')
        self.connected.add(1)
        self.call('client_putinserver', 1)
        self.hit(100)
        self.check(self.values()==[100] and all(d['var_skin']==1 for _,d in next(iter(self.groups().values()))), 'reused player slot starts enabled with reset skin and no previous events')
        self.clear()
        self.hit(23)
        self.call('plugin_end')
        self.check(not self.groups(), 'plugin end removes all generated entities')
        self.send('quit')
        self.proc.wait(timeout=10)
        self.check(self.proc.returncode == 0, 'pinned VM exits successfully')
        result = {'status': 'passed', 'checks': self.checks, 'check_count':len(self.checks),
                  'native_calls': dict(self.natives),
                  'amxx_sha256': hashlib.sha256((OUT/'addon_floating_damage.amxx').read_bytes()).hexdigest(),
                  'limits': ['Engine natives are a deterministic fixture.', 'No server startup, deployment, live network or visual gameplay validation.']}
        (OUT/'bytecode-results.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps({'status':'passed', 'checks':len(self.checks), 'native_calls':sum(self.natives.values())}))


if __name__ == '__main__':
    Engine().run()
