"""Exercise actual material staging and strict collector against real resources."""
import argparse
from datetime import datetime
import hashlib
import io
import json
from pathlib import Path
import subprocess
import sys
import time

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path.resolve()), bytes=len(data), sha256=sha(data))


def main(args):
    args.output.mkdir(parents=True, exist_ok=False)
    saved = json.loads(args.models.read_text('utf-8-sig'))
    models = [row['model'] for row in saved['models'] if row['model'] != 'models/player/zombi_host/zombi_host.mdl']
    assert len(models) == len(set(models)) == 14, 'Expected original 15-model set excluding unresolved host SEQ'
    stage = args.output / 'stage'
    manifest = args.output / 'manifest.json'
    tools = [ROOT / 'tools/Prepare-CSOLResources.py', ROOT / 'tools/csol_material_stage.py', args.collector]
    result = dict(created=datetime.now().astimezone().isoformat(), models=models,
                  excluded='zombi_host: unresolved 40-bone MDL versus 32-bone external SEQ',
                  before=[identity(p) for p in tools], commands=[], checks=[],
                  compiledByThisTest=False, clientRenderingValidated=False, gameplayValidated=False)

    def save():
        (args.output / 'result.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')

    def run(name, command):
        started = time.monotonic()
        process = subprocess.run(command, capture_output=True)
        stdout, stderr = args.output / (name + '.stdout'), args.output / (name + '.stderr')
        stdout.write_bytes(process.stdout)
        stderr.write_bytes(process.stderr)
        result['commands'].append(dict(name=name, command=command, exitCode=process.returncode,
                                       seconds=time.monotonic() - started, stdout=identity(stdout), stderr=identity(stderr)))
        save()
        if process.returncode:
            raise RuntimeError(name + ' exited ' + str(process.returncode))
        return process.stdout

    def check(name, predicate):
        result['checks'].append(dict(name=name, passed=bool(predicate)))
        if not predicate:
            raise AssertionError(name)

    save()
    try:
        run('prepare', [sys.executable, '-X', 'utf8', '-B', str(ROOT / 'tools/Prepare-CSOLResources.py'),
                        '--collector', str(args.collector.resolve()), '--source', str(args.source.resolve()),
                        '--server-game', str(stage.resolve()), '--manifest', str(manifest.resolve()),
                        '--relocate-materials', *models])
        prepared = json.loads(manifest.read_text('utf8'))
        rows = prepared['files']
        paths = {row['path'] for row in rows}
        check('Manifest has only bounded unique ASCII names', len(paths) == len(rows) == len({n.lower() for n in paths})
              and all(0 < len(n.encode('ascii')) <= 63 for n in paths))
        check('Global common material table is not overwritten', 'materials/common.json' not in paths)
        for row in rows:
            data = (stage / row['path']).read_bytes()
            check('Staged hash ' + row['path'], len(data) == row['bytes'] and sha(data) == row['sha256'])
        inputs = prepared['inputs']
        for name, source in inputs.items():
            data = (args.source / name).read_bytes()
            check('Original input unchanged ' + name, len(data) == source['bytes'] and sha(data) == source['sha256'])
            if name.lower().endswith(('.mdl', '.seq')):
                check('Native model/SEQ preserved ' + name, (stage / name).read_bytes() == data)
        for record in prepared['conversions']:
            original = (args.source / record['source']).read_bytes()
            output = (stage / record['target']).read_bytes()
            check('Material conversion source identity ' + record['source'], sha(original) == record['sourceSha256'])
            check('Material conversion output identity ' + record['target'], sha(output) == record['outputSha256'])
            if record['source'].lower().endswith('.dds'):
                with Image.open(io.BytesIO(original)) as image:
                    expected = image.convert('RGBA')
                with Image.open(io.BytesIO(output)) as image:
                    actual = image.convert('RGBA')
                check('Independent material pixels ' + record['target'], expected.size == actual.size
                      and expected.tobytes() == actual.tobytes() and sha(actual.tobytes()) == record['rgbaSha256'])
        strict = json.loads(run('strict-collector', [str(args.collector.resolve()), str(stage.resolve()), '--json', *models]))
        strict_paths = {row['path'] for row in strict}
        check('Strict collector includes all requested models', set(models).issubset(strict_paths))
        check('Strict collector needs no absent resource', strict_paths.issubset(paths))
        check('Strict collector uses per-model material tables', all(
            n in strict_paths for n in paths if n.endswith('.mat.json')))
        check('No original overlong material references remain', all(len(n.encode('ascii')) <= 63 for n in strict_paths))
        check('Oberon sound excludes three event parameter bytes', 'sound/zombi/oberon/appear.wav' in strict_paths
              and 'sound/200zombi/oberon/appear.wav' not in strict_paths)
        result['counts'] = dict(requestedModels=len(models), outputFiles=len(paths), strictResources=len(strict_paths),
                                preservedModels=sum(n.lower().endswith('.mdl') for n in inputs),
                                preservedSequences=sum(n.lower().endswith('.seq') for n in inputs),
                                conversions=len(prepared['conversions']))
        result['status'] = 'passed'
    except Exception as error:
        result['status'] = 'failed'
        result['error'] = type(error).__name__ + ': ' + str(error)
    result['after'] = [identity(p) for p in tools]
    result['sourceStable'] = result['before'] == result['after']
    result['checkCount'] = len(result['checks'])
    save()
    print(json.dumps({k: result[k] for k in ('status', 'sourceStable', 'checkCount', 'counts', 'error') if k in result}))
    return 0 if result['status'] == 'passed' and result['sourceStable'] else 1


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--models', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    sys.exit(main(parser.parse_args()))
