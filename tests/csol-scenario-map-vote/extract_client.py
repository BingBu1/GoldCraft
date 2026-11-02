"""Extract unchanged controller bodies for controlled engine/VGUI tests."""
import argparse
import hashlib
import json
from pathlib import Path

SIGNATURES = [
    'std::wstring Wide(', 'unsigned Remaining()', 'void RestoreMouse()', 'void Close(',
    'void Open()', 'int Receive(', 'void Layout()', 'int X(', 'int Y(', 'void Image(',
    'void Panel(', 'void Text(', 'std::wstring Fit(', 'void Button(', 'int MaxScroll()',
    'MenuRect Scrollbar()', 'MenuRect Thumb()', 'MenuRect Card(', 'int Hit(', 'void Click(',
    'void Paint()', 'void ScenarioMapVoteFrame()', 'void ScenarioMapVoteCreateMove(',
]


def extract(text, signature):
    start = text.index(signature)
    end = text.index('{', start) + 1
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    text = raw.decode('utf8')
    parts = [extract(text, signature) for signature in SIGNATURES]
    args.output.write_text('\n\n'.join(parts)+'\n', encoding='utf8')
    args.output.with_suffix('.manifest.json').write_text(json.dumps(dict(
        sourceSha256=hashlib.sha256(raw).hexdigest(),
        functions=[dict(signature=s, sha256=hashlib.sha256(p.encode()).hexdigest())
                   for s, p in zip(SIGNATURES, parts)]), indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
