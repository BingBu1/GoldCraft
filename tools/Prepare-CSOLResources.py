"""Prepare CSOL weapon dependencies in a server game directory, without decoding originals."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--collector', type=Path, required=True)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--server-game', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('models', nargs='+')
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.server_game.resolve()
    if source == destination or source.is_relative_to(destination) or destination.is_relative_to(source):
        raise ValueError('Source and server resource directories must be separate')
    result = subprocess.run([str(args.collector.resolve()), str(source), '--json', *args.models],
                            check=True, capture_output=True, text=True, encoding='utf-8')
    if result.stderr:
        print(result.stderr, end='')
    rows = []
    payloads = {}
    # Validate the entire plan before writing; never overwrite a different server asset.
    for entry in json.loads(result.stdout):
        name = entry['path']
        relative = Path(name)
        if relative.is_absolute() or '..' in relative.parts:
            raise ValueError('Invalid collected resource path')
        original = Path(entry.get('source', name))
        incoming, outgoing = (source / original).resolve(), (destination / relative).resolve()
        if not incoming.is_relative_to(source) or not outgoing.is_relative_to(destination):
            raise ValueError('Resource escaped its directory')
        data = entry['content'].encode('utf-8') if 'content' in entry else incoming.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if outgoing.exists() and hashlib.sha256(outgoing.read_bytes()).hexdigest() != digest:
            raise ValueError('Existing server resource differs; resolve version explicitly: ' + name)
        if name in payloads:
            if payloads[name] != data:
                raise ValueError('Conflicting collected resource: ' + name)
            continue
        payloads[name] = data
        rows.append({'path': name, 'source': entry.get('source'), 'generated': 'content' in entry,
                     'bytes': len(data), 'sha256': digest})
    for row in rows:
        outgoing = destination / row['path']
        if not outgoing.exists():
            outgoing.parent.mkdir(parents=True, exist_ok=True)
            outgoing.write_bytes(payloads[row['path']])
        if hashlib.sha256(outgoing.read_bytes()).hexdigest() != row['sha256']:
            raise RuntimeError('Server copy verification failed: ' + row['path'])
    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps({'models': args.models, 'files': rows}, indent=2), encoding='utf-8')
    print(json.dumps({'files': len(rows), 'bytes': sum(row['bytes'] for row in rows)}))


if __name__ == '__main__':
    main()
