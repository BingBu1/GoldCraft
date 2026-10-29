"""Validate real weapon dependency closure without copying or changing assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collector', required=True, type=Path)
    parser.add_argument('--resources', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    root = args.resources.resolve(strict=True)
    result = {}
    for name in ('beamgun', 'beamgunle', 'tknifeex2'):
        command = [str(args.collector.resolve(strict=True)), str(root), '--json', f'models/v_{name}.mdl']
        process = subprocess.run(command, capture_output=True, text=True, encoding='utf-8', timeout=90)
        if process.returncode:
            raise RuntimeError(process.stderr)
        files = json.loads(process.stdout)
        paths = {row['path'].lower() for row in files}
        assert len(paths) == len(files), 'case-insensitive duplicate dependency'
        for row in files:
            destination = (root / row['path']).resolve()
            assert destination.is_relative_to(root), row
            if 'source' in row:
                source = (root / row['source']).resolve(strict=True)
                assert source.is_relative_to(root) and source.is_file(), row
                row['bytes'] = source.stat().st_size
                with source.open('rb') as stream:
                    row['sha256'] = hashlib.file_digest(stream, 'sha256').hexdigest()
            else:
                assert 'content' in row, row
        if name != 'tknifeex2':
            for suffix in ('shootb', 'shootb2_land', 'shootb2_landx', 'shootb2_exp', 'jump', 'jump_land', 'wingman'):
                assert f'models/ef_{name}_{suffix}.mdl' in paths, suffix
                if suffix != 'wingman':
                    assert f'models/ef_{name}_{suffix}.json' in paths, suffix
            for suffix in ('v', 'shootb_t4', 'shootb2_exp2', 'wingman', 'charge_shoota', 'chargeshootahit'):
                assert f'sprites/ef_{name}_{suffix}.spr' in paths, suffix
            assert f'models/v_{name}.json' in paths
            assert f'models/p_{name}.mdl' in paths and f'models/w_{name}.mdl' in paths
            assert 'sprites/csol/640hud266.spr' in paths and 'sprites/csol/640hud224.spr' in paths
            assert 'sprites/640hud266.spr' not in paths and 'sprites/640hud224.spr' not in paths
            hud = next(row['content'] for row in files if row['path'] == f'sprites/weapon_{name}.txt')
            original = (root / f'sprites/weapon_{name}.txt').read_text()
            assert hud.split() == original.replace('640hud266', 'csol/640hud266').replace('640hud224', 'csol/640hud224').split()
        else:
            assert 'models/tknifeb.mdl' in paths and 'sprites/smoke.spr' in paths
            assert 'sprites/csol/640hud97.spr' in paths
        result[name] = dict(count=len(files), files=files, warnings=process.stderr)
        print(f'{name}: {len(files)} dependency files checked')
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
