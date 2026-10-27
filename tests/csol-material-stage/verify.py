"""Verify material staging with real collector output and independent pixel reads.

This does not compile or validate the C++ collector, client rendering, or HTTP.
"""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('material_stage', ROOT / 'tools/csol_material_stage.py')
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)

MODELS = ('speed_zombi_origin', 'heavy_zombi_host', 'heal_zombi_host',
          'heal_zombi_origin', 'deimos_zombi_host', 'deimos_zombi_origin')
FIELDS = ('BaseTexture', 'NormalTexture', 'SpecularTexture', 'EmissiveTexture', 'DispTexture')


def sha(data):
    return hashlib.sha256(data).hexdigest()


def identity(path):
    data = path.read_bytes()
    return dict(path=str(path), bytes=len(data), sha256=sha(data))


def verify(args):
    evidence = dict(created=datetime.now().astimezone().isoformat(),
                    scope='Python staging planner and existing collector inputs; no C++ build or gameplay',
                    source=str(args.source.resolve()), collector=identity(args.collector),
                    implementation=identity(ROOT / 'tools/csol_material_stage.py'),
                    tests=identity(Path(__file__)), checks=[], models=[])

    def check(name, predicate, details=None):
        evidence['checks'].append(dict(name=name, passed=bool(predicate), details=details))

    def rejects(name, entries, files):
        try:
            stage.prepare_materials(entries, files.__getitem__)
        except (ValueError, KeyError) as error:
            check(name, True, type(error).__name__ + ': ' + str(error))
        else:
            check(name, False, 'Unexpected accepted plan')

    basic = {'models/plain.mdl': b'original-model', 'models/plain1.seq': b'original-sequence'}
    rows = [dict(path=n, source=n) for n in basic]
    payload, conversions = stage.prepare_materials(rows, basic.__getitem__)
    check('No common table keeps all original bytes', payload == basic and not conversions)
    for name in ('../escape.bin', '/absolute.bin', 'C:/outside.bin', 'models//double.bin',
                 'models/./dot.bin', 'models/../parent.bin', 'models/back\\slash.bin',
                 'models/control\n.bin', 'models/' + 'x' * 64 + '.mdl'):
        rejects('Reject unsafe path ' + repr(name), [dict(path=name, content='x')], {})
    rejects('Reject conflicting duplicate', [dict(path='models/a.mdl', content='one'),
                                             dict(path='models/a.mdl', content='two')], {})
    rejects('Reject Windows case collision without common table', [dict(path='models/A.mdl', content='one'),
                                                                  dict(path='models/a.mdl', content='two')], {})
    rejects('Reject malformed common table', [dict(path='materials/common.json', content='{"Materials":{}}')], {})
    malformed = [dict(path='models/test.mdl', content='model'),
                 dict(path='materials/common.json', content='{"Materials":[]}'),
                 dict(path='models/test.mat.json', content='conflicting existing mapping')]
    rejects('Reject existing sidecar collision', malformed, {})

    all_inputs = {}
    all_entries = []
    for basename in MODELS:
        model = f'models/player/{basename}/{basename}.mdl'
        command = [str(args.collector.resolve()), str(args.source.resolve()), '--relocatable-json', model]
        process = subprocess.run(command, check=False, capture_output=True)
        (args.output / (basename + '.collector.json')).write_bytes(process.stdout)
        (args.output / (basename + '.collector.stderr.txt')).write_bytes(process.stderr)
        check(basename + ': existing collector accepts offline 128-byte budget', process.returncode == 0,
              dict(exitCode=process.returncode, stdoutSHA256=sha(process.stdout), stderrSHA256=sha(process.stderr)))
        if process.returncode:
            continue
        entries = json.loads(process.stdout)
        inputs = {}

        def read(name):
            path = (args.source / name).resolve()
            if not path.is_relative_to(args.source.resolve()):
                raise ValueError('Input escaped source root')
            data = path.read_bytes()
            inputs[name] = data
            return data

        payloads, converted = stage.prepare_materials(entries, read)
        all_inputs.update(inputs)
        all_entries.extend(entries)
        check(basename + ': all output names within 63 ASCII bytes',
              all(0 < len(n.encode('ascii')) <= 63 for n in payloads))
        check(basename + ': no case-insensitive output collision',
              len(payloads) == len({n.lower() for n in payloads}))
        check(basename + ': source MDL remains byte-identical', payloads.get(model) == inputs[model])
        sequence_names = [n for n in inputs if n.lower().endswith('.seq')]
        check(basename + ': source SEQ remains byte-identical',
              bool(sequence_names) and all(payloads.get(n) == inputs[n] for n in sequence_names))
        check(basename + ': global material table is not published', 'materials/common.json' not in payloads)
        local_name = model[:-4] + '.mat.json'
        prepared = json.loads(payloads[local_name])['Materials']
        originals = json.loads(inputs['materials/common.json'])['Materials']
        matching = [row for row in originals if isinstance(row, dict) and
                    str(row.get('OriginalTexture', '')).replace('\\', '/').lower().startswith(model.lower() + '/')]
        check(basename + ': all matching material rows preserved', len(prepared) == len(matching) and bool(prepared))
        check(basename + ': other models do not contaminate table', all(
            row['OriginalTexture'].replace('\\', '/').lower().startswith(model.lower() + '/') for row in prepared))
        by_source = {record['source']: record for record in converted}
        for expected, actual in zip(matching, prepared):
            # Compare every non-file property independently of production's
            # projection; parameter loss would change material appearance.
            check(basename + ': non-file material properties preserved',
                  {k: v for k, v in expected.items() if k not in FIELDS} ==
                  {k: v for k, v in actual.items() if k not in FIELDS})
            for field in FIELDS:
                source = expected.get(field)
                if source in (None, '', 'OriginalTexture'):
                    check(basename + ': sentinel retained ' + field, actual.get(field) == source)
                    continue
                target = actual[field]
                check(basename + ': referenced output exists ' + field, target in payloads)
                if source.lower().endswith('.dds'):
                    with Image.open(io.BytesIO(inputs[source])) as original_image:
                        source_pixels = original_image.convert('RGBA')
                    with Image.open(io.BytesIO(payloads[target])) as target_image:
                        target_pixels = target_image.convert('RGBA')
                    check(basename + ': independent DDS/TGA dimensions and pixels ' + field,
                          source_pixels.size == target_pixels.size and source_pixels.tobytes() == target_pixels.tobytes())
                    check(basename + ': recorded pixel digest agrees ' + field,
                          by_source[source]['rgbaSha256'] == sha(source_pixels.tobytes()))
        moved = set(by_source)
        check(basename + ': non-material dependencies remain byte-identical', all(
            payloads.get(n) == data for n, data in inputs.items() if n != 'materials/common.json' and n not in moved))
        check(basename + ': source resources unchanged after conversion', all(
            (args.source / name).read_bytes() == data for name, data in inputs.items()))
        evidence['models'].append(dict(model=model, command=command, inputs={n: dict(bytes=len(b), sha256=sha(b)) for n, b in inputs.items()},
                                       outputs={n: dict(bytes=len(b), sha256=sha(b)) for n, b in payloads.items()}, conversions=converted))

    combined, conversions = stage.prepare_materials(all_entries, all_inputs.__getitem__)
    check('Combined six-model plan has six isolated sidecars',
          sum(n.endswith('.mat.json') for n in combined) == 6)
    check('Combined six-model plan has six pixel conversions', len(conversions) == 6)
    check('Combined six-model names remain within network budget', all(len(n.encode('ascii')) <= 63 for n in combined))
    evidence['summary'] = dict(checks=len(evidence['checks']),
                               passed=sum(row['passed'] for row in evidence['checks']),
                               failed=sum(not row['passed'] for row in evidence['checks']),
                               realModels=len(evidence['models']),
                               cxxCompiled=False, runtimeCollectorValidated=False, gameplayValidated=False)
    (args.output / 'planner-result.json').write_text(json.dumps(evidence, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(evidence['summary']))
    for row in evidence['checks']:
        if not row['passed']:
            print('FAIL:', row['name'], row['details'])
    return 1 if evidence['summary']['failed'] else 0


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    sys.exit(verify(args))
