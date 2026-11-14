#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Bounded, read-only inventory observation using the loaded server's matching PDB.

No debugger, breakpoints, thread suspension, memory writes or game commands.
Identity is checked again after each read; this is not an atomic frame snapshot.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct
import time

ROOT = Path(__file__).resolve().parent.parent


class ClientObservation:
    """Resolve only live data present in this client's exact loaded PDB."""

    def __init__(self, audit, pid):
        self.process = audit.Process(pid, 'cs-client-b')
        try:
            expected = (ROOT / 'sandbox/cs-client-b/Half-Life/cstrike/metahook/plugins/CSOLSupport.dll').resolve(strict=True)
            modules = [m for m in self.process.modules if m['path'].resolve() == expected]
            if len(modules) != 1:
                raise ValueError('Expected one loaded B CSOLSupport module')
            module = modules[0]
            pe = audit.PE(expected)
            symbols = audit.Symbols(expected.with_suffix('.pdb'), pe)
            if not pe.codeview or self.process.read(module['base'] + pe.codeview['rva'], 24).hex() != pe.codeview['identity']:
                raise ValueError('Loaded client differs from its matching file and PDB')
            units = re.findall(r'Mod (\d+) \| `([^`]+)`', symbols.dump('-modules'))
            self.addresses = {}
            for unit, names in [('guest_cof_admission', ('admittedWeapons', 'generation', 'resources')),
                                ('guest_cof_crosshair', ('receiver', 'presentation'))]:
                matches = [int(i) for i, name in units if name.lower().endswith(unit + '.cpp.obj')]
                if len(matches) != 1:
                    raise ValueError('Expected one client compilation unit: ' + unit)
                data = symbols.dump('-symbols', '-modi=' + str(matches[0]))
                for name in names:
                    match = re.search(r'S_LDATA32[^\n]*::' + re.escape(name) + r'`\s*\n[^\n]*addr = (\d+):(\d+)', data)
                    if not match or not 0 < int(match[1]) <= len(pe.sections):
                        raise ValueError('Missing live client symbol: ' + name)
                    self.addresses[name] = module['base'] + pe.sections[int(match[1]) - 1]['rva'] + int(match[2])
            self.receiver = symbols.layout('guest::cof::CrosshairReceiver')
            self.presentation = symbols.layout('guest::cof::PresentationState')
            self.snapshot = symbols.layout('guest::cof::CrosshairSnapshot')
            self.image = expected
            self.identity = dict(pid=pid, imageSha256=pe.sha256, pdb=symbols.identity,
                                 addresses=self.addresses, receiver=self.receiver,
                                 presentation=self.presentation, snapshot=self.snapshot)
        except BaseException:
            self.process.close()
            raise

    def read(self):
        process, addresses = self.process, self.addresses
        admission = process.read(addresses['generation'], 8)
        receiver = process.read(addresses['receiver'], self.receiver['size'])
        presentation = process.read(addresses['presentation'], self.presentation['size'])
        r, p, s = self.receiver['members'], self.presentation['members'], self.snapshot['members']
        current = dict(weapons=process.u32(addresses['admittedWeapons']),
                       generation=int.from_bytes(admission, 'little'),
                       resources=int.from_bytes(process.read(addresses['resources'], 8), 'little'),
                       originalWeapon=presentation[p['originalWeapon']],
                       aim=bool(presentation[p['aimEnabled']]),
                       presentationRevision=struct.unpack_from('<I', presentation, p['revision'])[0],
                       crosshairRevision=struct.unpack_from('<I', receiver, r['revision_'])[0],
                       crosshairActive=bool(receiver[r['active_']]),
                       crosshairReceived=bool(receiver[r['received_']]))
        current['acceptedScalar'] = receiver[r['snapshot_'] + s['scalar']] if current['crosshairReceived'] else None
        # Keep keys opaque: equality is useful without guessing an optimized ABI.
        key_bytes = p['revision'] - p['key']
        current['presentationKey'] = presentation[p['key']:p['key'] + key_bytes].hex()
        current['crosshairKey'] = receiver[r['key_']:r['key_'] + key_bytes].hex()
        current['admissionStableDuringRead'] = process.read(addresses['generation'], 8) == admission
        return current

    def close(self):
        self.process.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', type=int, required=True)
    parser.add_argument('--client-pid', type=int, help='Optionally correlate the single sandbox B client using its exact loaded PDB')
    parser.add_argument('--pdb', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--slot', type=int, default=1, choices=range(1, 33), metavar='1..32')
    parser.add_argument('--seconds', type=int, default=60, choices=range(1, 121), metavar='1..120')
    parser.add_argument('--hz', type=int, default=50, choices=range(1, 101), metavar='1..100')
    args = parser.parse_args()
    out = args.output.absolute()
    if out.exists() or not out.resolve().is_relative_to(ROOT / 'build') or any(
            p.is_symlink() or p.is_junction() for p in (out, *out.parents)):
        raise ValueError('Use a fresh workspace build directory')
    pdb = args.pdb.resolve(strict=True)
    if not pdb.is_relative_to(ROOT):
        raise ValueError('Use the workspace server symbols')
    spec = importlib.util.spec_from_file_location('sandbox_memory', ROOT / 'tools/Inspect-MetaHookRuntime.py')
    audit = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(audit)
    process = audit.Process(args.pid, 'cs-server', executable='hlds.exe')
    client = None
    try:
        image = (ROOT / 'sandbox/cs-server/Half-Life/cstrike/dlls/mp.dll').resolve(strict=True)
        modules = [m for m in process.modules if m['path'].resolve() == image]
        if len(modules) != 1:
            raise ValueError('Expected one loaded native server module')
        module = modules[0]
        pe = audit.PE(image)
        symbols = audit.Symbols(pdb, pe)
        if not pe.codeview or process.read(module['base'] + pe.codeview['rva'], 24).hex() != pe.codeview['identity']:
            raise ValueError('Loaded server identity differs from the matching file and PDB')
        names = ('edict_s', 'entvars_s', 'CBaseEntity', 'CBasePlayer', 'CBasePlayerItem',
                 'CBasePlayerWeapon', 'guest::cof::server::RevolverWeapon',
                 'guest::cof::MagazineActionState', 'guest::cof::server::RevolverCylinder')
        layout = {name: symbols.layout(name) for name in names}
        E, V, B, P, I, W, R, A, D = [layout[name] for name in names]
        F = symbols.layout('guest::cof::server::RevolverFailure') if 'failure_' in R['members'] else None
        edicts_address = module['base'] + symbols.global_rva('g_pEdicts')
        globals_address = module['base'] + symbols.global_rva('gpGlobals')
        if args.client_pid is not None:
            client = ClientObservation(audit, args.client_pid)
        out.mkdir(parents=True)
        identity = dict(pid=args.pid, imageSha256=pe.sha256, pdb=symbols.identity,
                        moduleBase=module['base'], layouts=layout, failureLayout=F,
                        readOnly=True, noThreadSuspension=True, hz=args.hz, seconds=args.seconds)
        if client:
            identity['client'] = client.identity
        (out / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n', 'utf8')

        def integer(data, offset):
            return struct.unpack_from('<i', data, offset)[0]

        def pointer(data, offset):
            return struct.unpack_from('<I', data, offset)[0]

        def number(data, offset):
            return struct.unpack_from('<f', data, offset)[0]

        def snapshot():
            edicts = process.u32(edicts_address)
            globals_pointer = process.u32(globals_address)
            edict = edicts + args.slot * E['size']
            ed = process.read(edict, E['size'])
            actor = pointer(ed, E['members']['pvPrivateData'])
            if integer(ed, E['members']['free']) or not actor:
                return dict(connectedObject=False)
            player = process.read(actor, P['size'])
            variables = pointer(player, B['members']['pev'])
            native = process.read(variables, V['size'])
            if variables != edict + E['members']['v']:
                raise ValueError('Player entvars identity changed')
            active = pointer(player, P['members']['m_pActiveItem'])
            value = dict(connectedObject=True, engineTime=number(process.read(globals_pointer, 4), 0),
                         actor=actor, serial=integer(ed, E['members']['serialnumber']),
                         spawn=integer(player, P['members']['m_iNumSpawns']), active=active,
                         health=number(native, V['members']['health']),
                         deadflag=integer(native, V['members']['deadflag']),
                         flags=integer(native, V['members']['flags']),
                         buttons=integer(native, V['members']['button']),
                         animation=integer(native, V['members']['weaponanim']),
                         model=integer(native, V['members']['modelindex']),
                         menu=integer(player, P['members']['m_iMenu']), inventory=[])
            seen = set()
            for slot in range(6):
                item = pointer(player, P['members']['m_rgpPlayerItems'] + slot * 4)
                while item:
                    if item in seen or len(seen) >= 128:
                        raise ValueError('Inventory observation bound or cycle exceeded')
                    seen.add(item)
                    common = process.read(item, I['size'])
                    next_item = pointer(common, I['members']['m_pNext'])
                    if pointer(common, I['members']['m_pPlayer']) != actor:
                        raise ValueError('Inventory owner changed while reading')
                    weapon_id = integer(common, I['members']['m_iId'])
                    row = dict(slot=slot, item=item, id=weapon_id, selected=item == active)
                    if weapon_id == 898:
                        data = process.read(item, R['size'])
                        weapon_variables = pointer(data, B['members']['pev'])
                        ev = process.read(weapon_variables, V['size'])
                        item_edict = pointer(ev, V['members']['pContainingEntity'])
                        entity = process.read(item_edict, E['size'])
                        if integer(entity, E['members']['free']) or pointer(entity, E['members']['pvPrivateData']) != item:
                            raise ValueError('Revolver private object was retired')
                        action, cylinder = R['members']['action_'], R['members']['cylinder_']
                        row.update(serial=integer(entity, E['members']['serialnumber']),
                                   clip=integer(data, W['members']['m_iClip']),
                                   reserve=integer(data, action + A['members']['reserve']),
                                   action=integer(data, action + A['members']['action']),
                                   loading=data[cylinder + D['members']['loading']],
                                   cancel=data[cylinder + D['members']['cancel']],
                                   pending=data[cylinder + D['members']['pending']],
                                   commit=number(data, cylinder + D['members']['commitDeadline']),
                                   think=data[R['members']['cylinderThink_']],
                                   deadline=number(data, R['members']['cylinderDeadline_']),
                                   nextThink=number(ev, V['members']['nextthink']),
                                   bound=data[R['members']['bound_']], status=data[R['members']['status_']])
                        if F:
                            at, fields = R['members']['failure_'], F['members']
                            failure = {k: integer(data, at + fields[k]) for k in ('animation', 'body', 'clip', 'reserve')}
                            failure.update({k: data[at + fields[k]] for k in ('point', 'effect', 'loading', 'cancel', 'pending')})
                            failure.update({k: number(data, at + fields[k]) for k in ('enteredAt', 'failedAt', 'argument')})
                            failure['count'] = pointer(data, at + fields['count'])
                            row['failure'] = failure
                        if process.u32(item_edict + E['members']['pvPrivateData']) != item or \
                                process.u32(item_edict + E['members']['serialnumber']) != row['serial']:
                            raise ValueError('Revolver identity changed after reading')
                    value['inventory'].append(row)
                    item = next_item
            if process.u32(edicts_address) != edicts or \
                    process.u32(edict + E['members']['pvPrivateData']) != actor or \
                    process.u32(edict + E['members']['serialnumber']) != value['serial'] or \
                    process.u32(actor + P['members']['m_pActiveItem']) != active:
                raise ValueError('Player identity changed after reading')
            return value

        started, cpu = time.perf_counter(), time.process_time()
        samples = rejected = 0
        client_errors = client_unstable = 0
        errors = {}
        with (out / 'samples.jsonl').open('w', encoding='utf8', buffering=65536) as stream:
            for tick in range(args.hz * args.seconds):
                remaining = started + tick / args.hz - time.perf_counter()
                if remaining > 0:
                    time.sleep(remaining)
                try:
                    value = snapshot()
                    if client:
                        try:
                            value['client'] = client.read()
                            client_unstable += not value['client']['admissionStableDuringRead']
                        except (OSError, ValueError) as error:
                            client_errors += 1
                            value['client'] = dict(error=type(error).__name__ + ': ' + str(error))
                    value['sampleSeconds'] = time.perf_counter() - started
                    stream.write(json.dumps(value, separators=(',', ':')) + '\n')
                    samples += 1
                except (OSError, ValueError) as error:
                    rejected += 1
                    name = type(error).__name__ + ': ' + str(error)
                    errors[name] = errors.get(name, 0) + 1
                if tick % args.hz == 0:
                    stream.flush()
                    (out / 'progress.json').write_text(json.dumps(dict(samples=samples, rejected=rejected)), 'utf8')
        unchanged = hashlib.sha256(image.read_bytes()).hexdigest() == pe.sha256
        result = dict(samples=samples, rejected=rejected, errors=errors, imageUnchanged=unchanged,
                      seconds=time.perf_counter() - started, samplerCpuSeconds=time.process_time() - cpu,
                      readOnly=True, noThreadSuspension=True,
                      boundary='Identity-checked external reads; not an atomic frame or automatic diagnosis/fidelity verdict')
        if client:
            result.update(clientImageUnchanged=hashlib.sha256(client.image.read_bytes()).hexdigest() == client.identity['imageSha256'],
                          clientReadErrors=client_errors, clientAdmissionUnstableReads=client_unstable)
        (out / 'receipt.json').write_text(json.dumps(result, indent=2) + '\n', 'utf8')
        print(json.dumps(result))
    finally:
        if client:
            client.close()
        process.close()


if __name__ == '__main__':
    main()
