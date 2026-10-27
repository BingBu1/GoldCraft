"""Prepare model-local material tables and bounded GoldSrc download names."""
import hashlib
import io
import json
from pathlib import PurePosixPath

FIELDS = ('BaseTexture', 'NormalTexture', 'SpecularTexture', 'EmissiveTexture', 'DispTexture')


def resource_name(name, limit=63):
    if (not isinstance(name, str) or not name or len(name.encode('ascii')) > limit
            or any(c in name for c in ':\\\"*?') or any(ord(c) < 32 for c in name)
            or any(part in ('', '.', '..') for part in name.split('/'))):
        raise ValueError('Unsafe or oversized resource name: ' + repr(name))
    return name


def prepare_materials(entries, read):
    """Return a complete plan; read hashes inputs before any output is written.

    MDL/SEQ bytes and model names are retained. Model-local tables replace
    common.json for only the selected models; other installed tables are intact.
    """
    originals = {}
    for entry in entries:
        name = resource_name(entry['path'], 128)
        data = entry['content'].encode('utf8') if 'content' in entry else read(entry.get('source', name))
        if name in originals and originals[name] != data:
            raise ValueError('Conflicting resource: ' + name)
        originals[name] = data
    def material_table(data):
        table = json.loads(data)
        if not isinstance(table, dict) or not isinstance(table.get('Materials'), list):
            raise ValueError('Invalid material table')
        return table
    common = material_table(originals['materials/common.json']) if 'materials/common.json' in originals else None
    models = [name for name in originals if name.lower().endswith('.mdl')]
    selected, references = {}, set()
    for model in models:
        local = model[:-4] + '.mat.json'
        table = material_table(originals[local]) if local in originals else common
        if table is None:
            continue
        rows = []
        prefix = model.lower() + '/'
        for row in table['Materials']:
            if not isinstance(row, dict) or not isinstance(row.get('OriginalTexture'), str):
                continue
            key = row['OriginalTexture'].replace('\\', '/').lower()
            if not key.startswith(prefix):
                continue
            copy = dict(row)
            for field in FIELDS:
                name = copy.get(field)
                if name in (None, '', 'OriginalTexture'):
                    continue
                resource_name(name, 128)
                # The collector matches actual MDL textures. Unused rows must
                # not pull unrelated assets into a prepared model's table.
                if name not in originals:
                    break
            else:
                rows.append(copy)
                references.update(copy[f] for f in FIELDS if copy.get(f) not in (None, '', 'OriginalTexture'))
        selected[model] = dict(table, Materials=rows)
    payloads = {n: b for n, b in originals.items() if n != 'materials/common.json'}
    aliases, conversions = {}, []
    for name in sorted(references):
        dds = name.lower().endswith('.dds')
        if not dds and len(name.encode('ascii')) <= 63:
            continue
        if not name.startswith('materials/') and len(name.encode('ascii')) > 63:
            raise ValueError('Long non-material reference needs explicit relocation: ' + name)
        suffix = '.tga' if dds else PurePosixPath(name).suffix
        target = 'materials/csol/r/' + hashlib.sha256(name.encode('ascii')).hexdigest()[:24] + suffix
        resource_name(target)
        data = originals[name]
        record = dict(source=name, target=target, sourceSha256=hashlib.sha256(data).hexdigest())
        if dds:
            from PIL import Image
            with Image.open(io.BytesIO(data)) as source:
                image = source.convert('RGBA')
                buffer = io.BytesIO()
                image.save(buffer, format='TGA')
                data = buffer.getvalue()
                with Image.open(io.BytesIO(data)) as reopened:
                    if reopened.size != image.size or reopened.convert('RGBA').tobytes() != image.tobytes():
                        raise ValueError('Material pixel conversion mismatch: ' + name)
                record.update(width=image.width, height=image.height,
                              rgbaSha256=hashlib.sha256(image.tobytes()).hexdigest())
        if target in payloads and payloads[target] != data:
            raise ValueError('Material alias collision: ' + target)
        payloads[target] = data
        # Short names may also be referenced by MDL/JSON/shader fields which
        # are not material-table fields. Keep those bytes for that consumer.
        if len(name.encode('ascii')) > 63:
            del payloads[name]
        aliases[name] = target
        record['outputSha256'] = hashlib.sha256(data).hexdigest()
        conversions.append(record)
    for model, table in selected.items():
        for row in table['Materials']:
            for field in FIELDS:
                if row.get(field) in aliases:
                    row[field] = aliases[row[field]]
        name = resource_name(model[:-4] + '.mat.json')
        data = json.dumps(table, ensure_ascii=False, separators=(',', ':')).encode('utf8')
        payloads[name] = data
    seen = set()
    for name in payloads:
        resource_name(name)
        if name.lower() in seen:
            raise ValueError('Case-insensitive resource collision: ' + name)
        seen.add(name.lower())
    return payloads, conversions
