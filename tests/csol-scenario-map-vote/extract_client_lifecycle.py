"""Extract the controller and actual session/message/frame/input lifecycle.

Compile client.cpp with CSOL_MAP_VOTE_TEST_RESET and the output directory on
the include path. Load, interface creation and rendered pixels remain outside
this controlled fixture's coverage.
"""
import argparse
import hashlib
import json
from pathlib import Path

from extract_client import SIGNATURES, extract


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--session', type=Path)
    parser.add_argument('--plugin', type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    text = raw.decode('utf-8')
    signatures = SIGNATURES + ['void ResetScenarioMapVote()', 'void ScenarioMapVoteDiagnostics(']
    if 'double VoteClock()' in text:
        signatures.insert(0, 'double VoteClock()')
    if 'void Suspend()' in text:
        signatures.insert(signatures.index('void Open()'), 'void Suspend()')
    parts = [extract(text, signature) for signature in signatures]
    args.output.write_text('\n\n'.join(parts)+'\n', encoding='utf-8')
    args.output.with_suffix('.manifest.json').write_text(json.dumps(dict(
        sourceSha256=hashlib.sha256(raw).hexdigest(),
        functions=[dict(signature=s, sha256=hashlib.sha256(p.encode()).hexdigest())
                   for s, p in zip(signatures, parts)]), indent=2)+'\n', encoding='utf-8')
    overlay = extract(text, 'class Overlay final:') + ';\n'
    args.output.with_name('actual_overlay.inc').write_text(overlay, encoding='utf-8')
    session = args.session or args.source.with_name('session.cpp')
    plugin = args.plugin or args.source.with_name('plugin.cpp')
    sources = [(session, ['void ResetSession()', 'void SynchronizeSession()',
                         'void SynchronizeFrameSession()']),
               (plugin, ['static void Frame(', 'static void CreateMove('])]
    functions = []
    source_records = []
    for path, entries in sources:
        data = path.read_bytes()
        source_records.append(dict(path=str(path), sha256=hashlib.sha256(data).hexdigest()))
        for signature in entries:
            body = extract(data.decode('utf-8'), signature)
            functions.append(dict(signature=signature, body=body,
                                  sha256=hashlib.sha256(body.encode()).hexdigest()))
    args.output.with_name('actual_session.inc').write_text(
        '\n\n'.join(row['body'] for row in functions)+'\n', encoding='utf-8')
    args.output.with_name('actual_session.manifest.json').write_text(json.dumps(dict(
        sources=source_records,
        functions=[{key: value for key, value in row.items() if key != 'body'}
                   for row in functions]), indent=2)+'\n', encoding='utf-8')


if __name__ == '__main__':
    main()
