#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Verify and stage the actual SKULL2 dependency closure from extracted common."""
import argparse, hashlib, json, re, struct, subprocess
from pathlib import Path


def identity(path):
    path = Path(path).resolve()
    return dict(path=str(path), bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--common', type=Path, required=True)
    parser.add_argument('--header', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    common, output = args.common.resolve(), args.output.resolve()
    assert output.is_relative_to(Path(__file__).resolve().parents[2]/'build')
    output.mkdir()
    inputs, resources = {}, []
    checks = 0

    def check(ok, reason):
        nonlocal checks
        checks += 1
        if not ok:
            raise AssertionError(reason)

    def read(path):
        path = Path(path)
        row = identity(path)
        check(str(path) not in inputs or inputs[str(path)] == row, 'stable source '+str(path))
        inputs[str(path)] = row
        return path.read_bytes()

    exe = args.collector.resolve()
    for path in (exe, exe.parent/'plan.json', exe.parent/'result.json', Path(__file__), args.header):
        read(path)
    check(json.loads(read(exe.parent/'result.json'))['success'], 'collector build passed')
    optional = [common/'materials/common.json']+sorted((common/'weapons').glob('*.json'))
    for path in optional:
        if path.exists():
            read(path)
    argv = [str(exe), str(common), '--json', 'models/v_skull2.mdl']
    run = subprocess.run(argv, capture_output=True, timeout=60)
    (output/'dependencies.json').write_bytes(run.stdout)
    (output/'stderr.log').write_bytes(run.stderr)
    check(run.returncode == 0, run.stderr.decode('utf8', 'replace'))
    rows = json.loads(run.stdout)
    paths = {row['path'] for row in rows}
    check(len(paths) == len(rows), 'unique resources')
    header = read(args.header).decode('utf8')
    declaration = header.split('Skull2Resources{', 1)[1].split('};', 1)[0]
    declared = set(re.findall(r'"([^"]+)"', declaration))
    check(declared <= paths, 'all declared resources collected')
    required_sounds = {'sound/weapons/'+name+'.wav' for name in (
        'skull2_clipin', 'skull2_clipout', 'skull2_draw', 'skull2_draw2',
        'skull2_draw3', 'skull2_draw4', 'skull2_reload2_1', 'skull2_reload2_2')}
    check(required_sounds <= paths, 'model animation sounds collected recursively')
    check({'models/p_skull2.mdl','models/p_skull2dual.mdl','models/w_skull2.mdl'} <= paths,
          'single/dual/world models in closure')
    descriptor = 'sprites/csol/weapon_skull2_v1.txt'
    generated = {row['path']: row['content'] for row in rows if 'content' in row}
    check(set(generated) == {descriptor, 'events/skull2_a.sc',
                            'events/skull2_left.sc', 'events/skull2_right.sc'}, 'four generated adapters')
    for name, text in generated.items():
        if name.startswith('events/'):
            check(text == '// SKULL-2 native event handler.\n', 'explicit native event adapter')
    original = read(common/'sprites/weapon_skull2.txt').decode('ascii').splitlines()
    actual = generated[descriptor].splitlines()
    check(original[0] == actual[0] == '3' and len(original) == len(actual) == 4,
          'original HUD row count')

    def crop(atlas, rect):
        data = read(common/('sprites/'+atlas+'.spr'))
        palette = struct.unpack_from('<H', data, 40)[0]
        width, height = struct.unpack_from('<ii', data, 42+3*palette+12)
        x, y, w, h = map(int, rect)
        check(x >= 0 and y >= 0 and w > 0 and h > 0 and x+w <= width and y+h <= height,
              'original atlas crop '+atlas)

    for before, after in zip(original[1:], actual[1:]):
        a, b = before.split(), after.split()
        check(a[:2] == b[:2] and 'csol/'+a[2] == b[2] and a[3:] == b[3:], 'exact original HUD rectangle')
        check('sprites/'+b[2]+'.spr' in paths, 'HUD atlas present')
        crop(a[2], a[3:])
    death = next(line.split() for line in read(common/'sprites/hud.txt').decode('latin1').splitlines()
                 if line.split() and line.split()[0] == 'd_skull2')
    check(death == ['d_skull2','640','640hud38','172','0','48','16'], 'original death icon row')
    check('Skull2DeathHUD="d_skull2 640 csol/640hud38 172 0 48 16\\n"' in header,
          'compiled death icon matches original')
    crop(death[2], death[3:])
    for row in rows:
        path = row['path']
        check(not Path(path).is_absolute() and '..' not in Path(path).parts and len(path) <= 63,
              'runtime-safe path '+path)
        source = None
        if 'content' in row:
            data = row['content'].encode('utf8')
        else:
            source = (common/row.get('source', path)).resolve()
            check(source.is_relative_to(common), 'common source confinement')
            data = read(source)
        check(bool(data), 'nonempty '+path)
        if path.endswith('.wav'):
            check(data[:4] == b'RIFF' and data[8:12] == b'WAVE', 'real WAVE resource '+path)
        destination = output/'resources'/path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        check(destination.stat().st_nlink == 1, 'independent staged resource')
        row = dict(path=path, generated=source is None, bytes=len(data),
                   sha256=hashlib.sha256(data).hexdigest(), staged=identity(destination))
        if source is not None:
            row['source'] = identity(source)
        resources.append(row)
    repeat = subprocess.run(argv, capture_output=True, timeout=60)
    check(repeat.returncode == 0 and repeat.stdout == run.stdout, 'stable second actual collector closure')
    for row in inputs.values():
        check(identity(row['path']) == row, 'unchanged input '+row['path'])
    check(optional == [common/'materials/common.json']+sorted((common/'weapons').glob('*.json')),
          'stable optional source set')
    for row in resources:
        check(identity(row['staged']['path']) == row['staged'], 'staged SHA '+row['path'])
    result = dict(success=True, checks=checks, argv=argv, inputs=list(inputs.values()), resources=resources,
                  declared=len(declared), collected=len(resources), bytes=sum(r['bytes'] for r in resources),
                  dependencyOutput=identity(output/'dependencies.json'),
                  boundary='Actual collector and original common closure staged independently; no HTTP/game test.')
    (output/'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf8')
    print(json.dumps({k: result[k] for k in ('success','checks','declared','collected','bytes')}))


if __name__ == '__main__':
    main()
