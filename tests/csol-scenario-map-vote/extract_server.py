"""Extract unchanged server adapter methods for controlled transport tests."""
import argparse
import hashlib
import json
from pathlib import Path

from extract_client import extract

SIGNATURES = [
    'std::uint32_t Identity(', 'template<class T> bool Number(', 'bool Send(',
    'std::optional<Choice> CSOL_UpdateScenarioMapVote(',
    'bool CSOL_ScenarioMapVoteCommand(', 'bool CSOL_ScenarioMapVoteActive(',
    'void CSOL_CancelScenarioMapVote(', 'void CSOL_ResetScenarioMapVote(',
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    raw = args.source.read_bytes()
    parts = [extract(raw.decode('utf8'), signature) for signature in SIGNATURES]
    args.output.write_text('\n\n'.join(parts)+'\n', encoding='utf8')
    args.output.with_suffix('.manifest.json').write_text(json.dumps(dict(
        sourceSha256=hashlib.sha256(raw).hexdigest(),
        functions=[dict(signature=s, sha256=hashlib.sha256(p.encode()).hexdigest())
                   for s, p in zip(SIGNATURES, parts)]), indent=2), encoding='utf8')


if __name__ == '__main__':
    main()
