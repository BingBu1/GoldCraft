"""Prepare independent factual expectations and exact SDK bodies; no build/run."""
from pathlib import Path
import argparse
import hashlib
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[2]


def identity(path):
    data = path.read_bytes()
    return {'path': str(path.resolve()), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    catalog = ROOT / 'analysis/csol-model-20261017/scenario/monster-types/appearance-catalog.json'
    assert identity(catalog)['sha256'] == '6dc8535729df3aac358c24453fc17d6c8aecf85d3a2c03b822607ca6ce8a40cb'
    entries = json.loads(catalog.read_text(encoding='utf-8'))['entries']
    assert len(entries) == 143
    lines = ['// Independently generated from original 60-byte records, never production.',
        'struct ExpectedDeath { unsigned index; const char* name; std::uint32_t seconds; const char* sound; };',
        'inline constexpr std::array<ExpectedDeath, 143> ExpectedDeaths{{']
    for i, row in enumerate(entries):
        raw = bytes.fromhex(row['rawHex'])
        assert row['index'] == i and len(raw) == 60
        assert int(row['deathSoundAddress'], 16) == struct.unpack_from('<I', raw, 0x30)[0]
        lines.append('  {' + ','.join([str(i), json.dumps(row['name']), str(struct.unpack_from('<I', raw, 0x2c)[0]) + 'u', json.dumps(row['deathSound'] or '')]) + '},')
    lines += ['}};', '']
    target = args.output / 'expected_death.hpp'
    assert not target.exists(), 'do not overwrite prepared evidence'
    target.write_text('\n'.join(lines), encoding='utf-8')

    # Static comparison is explicit about not executing the C++ compiler.
    production = ROOT / 'native/csol/scenario/monster_appearances.cpp'
    pattern = r'\{(\d+), "([^"]+)", "[^"]+",\s*\{[^}]+\}, \{[^}]+\}, (?:true|false), ([\d.]+)f, "([^"]*)"\}'
    parsed = re.findall(pattern, production.read_text(encoding='utf-8'))
    assert len(parsed) == len(entries)
    for row, (index, name, seconds, sound) in zip(entries, parsed):
        assert int(index) == row['index'] and name == row['name']
        assert struct.pack('<f', float(seconds)) == bytes.fromhex(row['rawHex'])[0x2c:0x30]
        assert sound == (row['deathSound'] or '')

    # Extract only unchanged methods, preserving exact bytes and their origin.
    sdk = ROOT / 'external/ReGameDLL_CS/regamedll/dlls'
    requests = [(sdk / 'animating.cpp', b'void EXT_FUNC CBaseAnimating::__API_HOOK(ResetSequenceInfo)()', b'\nfloat CBaseAnimating::SetBoneController('),
        (sdk / 'animation.cpp', b'void GetSequenceInfo(void *pmodel,', b'\nfloat GetSequenceDuration('),
        (sdk / 'animation.cpp', b'int GetSequenceFlags(void *pmodel,', b'\nint GetAnimationEvent('),
        (sdk / 'sound.cpp', b'void EMIT_SOUND_DYN(edict_t *entity,', b'\n// play a specific sentence')]
    parts, records = [], []
    for path, begin, end in requests:
        data = path.read_bytes()
        start = data.index(begin)
        finish = data.index(end, start)
        part = data[start:finish].rstrip() + b'\n'
        parts.append(part)
        records.append({'source': identity(path), 'offset': start, 'bytes': len(part),
            'sha256': hashlib.sha256(part).hexdigest(), 'bodyChanged': False})
    generated = args.output / 'sdk_death.cpp'
    assert not generated.exists()
    generated.write_bytes(b'// Exact SDK methods; original ReGameDLL_CS GPL-3.0-or-later notices apply.\n#include "precompiled.h"\n' + b'\n'.join(parts))
    report = {'scope': 'Preparation and static comparison only; no compilation or native/original instruction execution.',
        'catalogCount': len(entries), 'staticCatalogFieldsMatched': ['index', 'name', 'durationFloatBits', 'deathSound'],
        'inputs': [identity(catalog), identity(production), identity(Path(__file__))],
        'sdkExtractions': records, 'outputs': [identity(target), identity(generated)]}
    (args.output / 'preparation.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(identity(args.output / 'preparation.json')))


if __name__ == '__main__':
    main()
