#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Prepare value-only resource metadata for an isolated native jump playtest."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import runpy

ROOT = Path(__file__).resolve().parents[2]


def checked(path):
    path = path.absolute()
    if not path.resolve().is_relative_to(ROOT) or any(
            p.is_symlink() or p.is_junction() for p in (path, *path.parents)):
        raise ValueError('Plain workspace paths required')
    return path


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('baseline', 'client', 'guest-bootstrap', 'cold-receipt', 'damage', 'output'):
        parser.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    for key, value in vars(args).items():
        setattr(args, key, checked(value))
    if not args.output.is_relative_to(ROOT / 'build') or args.output.exists():
        raise ValueError('Use a fresh metadata file beneath build')
    hud = runpy.run_path(str(ROOT / 'tools/csol_hud_resources.py'))
    pair = runpy.run_path(str(ROOT / 'tools/Verify-CSOLPresentationPair.py'))
    assert hud['verify'](args.baseline)['success']
    constants, images = hud['contract']()
    names = hud['dependencies']((args.baseline / constants['CatalogPath']).read_text('utf-8-sig'), constants, images)
    phase = (ROOT / 'native/csol/scenario/custom/phase_art.hpp').read_text('utf-8-sig')
    names += re.findall(r'NightAnimationAtlas\[\]="([^"]+)"', phase)
    stamina = (ROOT / 'native/csol/scenario/custom/stamina_audio.hpp').read_text('utf-8-sig')
    recovery = re.search(r'\bRecoverySounds\{(.*?)\};', stamina, re.S)
    if not recovery:
        raise ValueError('Recovery sound producer contract changed')
    sounds = re.findall(r'"([^"\r\n]+\.wav)"', recovery[1])
    if len(sounds) != 2:
        raise ValueError('Incomplete recovery sound closure')
    names += ['sound/' + name for name in sounds]
    menu = pair['compiled_menu'](args.client)
    names += pair['menu_resources'](menu, (args.baseline / menu).read_bytes())
    rows = {}

    def add(name, source, expected=None):
        source = checked(source).resolve(strict=True)
        data = source.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if expected and (digest, len(data)) != (expected['sha256'], expected['bytes']):
            raise ValueError('Sealed resource differs: ' + name)
        rows[name] = dict(source=str(source), destination=name, sha256=digest, bytes=len(data))

    for name in sorted(set(names)):
        add(name, args.baseline / name)
    old = json.loads(args.cold_receipt.read_text('utf-8'))
    if not old['success'] or not old['checks']['actualGuestHttpHashes']:
        raise ValueError('Use a successful actual HTTP closure receipt')
    for row in old['http']:
        name = row['path']
        candidates = (args.baseline / name, args.guest_bootstrap / 'payload/cs/cstrike' / name)
        source = next((p for p in candidates if p.is_file() and
                       hashlib.sha256(p.read_bytes()).hexdigest() == row['sha256']), None)
        if not source:
            raise ValueError('Missing sealed HTTP resource: ' + name)
        add(name, source, row)
    for name in list(rows):
        if not name.startswith('models/guest/cof/') or not name.endswith('.mdl'):
            continue
        profile_name = name[:-4] + '_hands.json'
        profile_path = args.baseline / profile_name
        if profile_path.is_file():
            add(profile_name, profile_path)
            profile = json.loads(profile_path.read_text('utf-8-sig'))
            donor = profile.get('donor')
            if (profile.get('version') != 1 or not isinstance(donor, str) or
                    not donor.startswith('models/csol/hands/') or not donor.endswith('.mdl')):
                raise ValueError('Unsupported authored hand profile: ' + profile_name)
            add(donor, args.baseline / donor)
    add('configs/guest/cof/scenario_damage.json', args.damage)
    if not 0 < len(rows) <= 1024:
        raise ValueError('Resource closure exceeds the controlled test bound')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(dict(files=list(rows.values())), indent=2) + '\n', 'utf-8')
    print(json.dumps(dict(resources=len(rows), guestResources=len(old['http']), menu=menu)))


if __name__ == '__main__':
    main()
