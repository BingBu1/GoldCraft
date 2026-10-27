"""Regression cases for material overrides and shared resource ownership."""
import argparse
from datetime import datetime
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys

from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'tools/csol_material_stage.py'
spec = importlib.util.spec_from_file_location('material_stage', SOURCE)
stage = importlib.util.module_from_spec(spec)
spec.loader.exec_module(stage)


def encode(obj):
    return json.dumps(obj, separators=(',', ':')).encode()


def plan(files):
    return stage.prepare_materials([dict(path=n, source=n) for n in files], files.__getitem__)


def image_bytes(format):
    image = Image.new('RGBA', (2, 2))
    image.putdata([(12, 34, 56, 78), (1, 2, 3, 255), (200, 190, 180, 0), (9, 8, 7, 6)])
    data = io.BytesIO()
    image.save(data, format=format)
    return data.getvalue(), image.tobytes()


def mixed_local_and_global():
    a = {'OriginalTexture': 'models/a.mdl/skin.bmp', 'BaseTexture': 'materials/short/a.tga',
         'BaseColor': [.1, .2, .3, .4]}
    stale_a = dict(a, BaseColor=[1, 1, 1, 1])
    b = {'OriginalTexture': 'models/b.mdl/skin.bmp', 'BaseTexture': 'materials/short/b.tga',
         'BlendMode': 'Translucent'}
    files = {'models/a.mdl': b'a-model', 'models/b.mdl': b'b-model',
             'models/a.mat.json': encode({'Materials': [a]}),
             'materials/common.json': encode({'Materials': [stale_a, b]}),
             'materials/short/a.tga': b'a-image', 'materials/short/b.tga': b'b-image'}
    result, _ = plan(files)
    assert json.loads(result['models/a.mat.json'])['Materials'] == [a], 'Local table lost priority'
    assert json.loads(result['models/b.mat.json'])['Materials'] == [b], 'Global table lost model B'


def five_fields():
    dds, pixels = image_bytes('DDS')
    fields = ('BaseTexture', 'NormalTexture', 'SpecularTexture', 'EmissiveTexture', 'DispTexture')
    row = {'OriginalTexture': 'models/test.mdl/body.bmp', 'SubKey': 0,
           'BaseColor': [.2, .3, .4, .5], 'BlendMode': 'Additive', 'CustomValue': {'keep': [2, 4]}}
    files = {'models/test.mdl': b'model', 'models/test1.seq': b'anim', 'sound/test.wav': b'event-sound'}
    for index, field in enumerate(fields):
        name = 'materials/' + ('long-path-' * 7) + str(index) + '.dds'
        row[field] = name
        files[name] = dds
    files['materials/common.json'] = encode({'Materials': [row]})
    result, conversions = plan(files)
    actual = json.loads(result['models/test.mat.json'])['Materials'][0]
    assert len(conversions) == 5
    assert {k: v for k, v in row.items() if k not in fields} == {k: v for k, v in actual.items() if k not in fields}
    for field in fields:
        target = actual[field]
        assert len(target.encode('ascii')) <= 63 and target.endswith('.tga')
        with Image.open(io.BytesIO(result[target])) as image:
            assert image.size == (2, 2) and image.convert('RGBA').tobytes() == pixels
    assert result['models/test.mdl'] == b'model'
    assert result['models/test1.seq'] == b'anim'
    assert result['sound/test.wav'] == b'event-sound'


def alias_collision():
    name = 'materials/' + 'x' * 62 + '.tga'
    target = 'materials/csol/r/' + hashlib.sha256(name.encode('ascii')).hexdigest()[:24] + '.tga'
    files = {'models/test.mdl': b'model', name: b'image', target: b'different-image',
             'materials/common.json': encode({'Materials': [
                 {'OriginalTexture': 'models/test.mdl/body.bmp', 'NormalTexture': name}]})}
    try:
        plan(files)
    except ValueError as error:
        assert 'collision' in str(error).lower()
        return
    raise AssertionError('Conflicting alias was accepted')


def preserve_shared_short_name():
    # Material relocation does not own external MDL/JSON references. If it
    # converts a resource from another namespace, its valid original name
    # must remain accessible as well as the material-only short image.
    name = 'models/texture/shared.dds'
    dds, _ = image_bytes('DDS')
    files = {'models/test.mdl': b'model', name: dds,
             'models/test.json': encode({'externalDependency': name}),
             'materials/common.json': encode({'Materials': [
                 {'OriginalTexture': 'models/test.mdl/body.bmp', 'BaseTexture': name}]})}
    result, _ = plan(files)
    assert result.get(name) == dds, 'Material alias removed a valid name owned by another consumer'


def reject_shared_long_name():
    name = 'models/texture/' + 'long-name-' * 6 + '.tga'
    files = {'models/test.mdl': b'model', name: b'image',
             'models/test.json': encode({'externalDependency': name}),
             'materials/common.json': encode({'Materials': [
                 {'OriginalTexture': 'models/test.mdl/body.bmp', 'BaseTexture': name}]})}
    try:
        plan(files)
    except ValueError:
        return
    raise AssertionError('Unsupported non-material long-path reference was silently renamed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        raise ValueError('Use a fresh output evidence file')
    source_before = SOURCE.read_bytes()
    result = dict(created=datetime.now().astimezone().isoformat(),
                  sourceSha256=hashlib.sha256(source_before).hexdigest(),
                  testsSha256=hashlib.sha256(Path(__file__).read_bytes()).hexdigest(), cases=[])
    for case in (mixed_local_and_global, five_fields, alias_collision,
                 preserve_shared_short_name, reject_shared_long_name):
        try:
            case()
            result['cases'].append(dict(name=case.__name__, passed=True))
        except Exception as error:
            result['cases'].append(dict(name=case.__name__, passed=False,
                                        error=type(error).__name__ + ': ' + str(error)))
    result['sourceStable'] = SOURCE.read_bytes() == source_before
    result['passed'] = sum(c['passed'] for c in result['cases'])
    result['failed'] = sum(not c['passed'] for c in result['cases'])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps(result, ensure_ascii=False))
    sys.exit(1 if result['failed'] or not result['sourceStable'] else 0)
