"""Extract unchanged vote rules methods; do not replace their lifecycle guards."""
import argparse,hashlib,json
from pathlib import Path
from extract_client import extract

SIGNATURES=['bool RequestMapVote(', 'bool UpdateAdminMapVote(', 'bool ChangeScenarioMap(',
            'bool RequestReplay(', 'void ScenarioThink(', 'void ApplyFinishPolicy(']

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args();raw=args.source.read_bytes();text=raw.decode('utf-8-sig')
    parts=[extract(text,s) for s in SIGNATURES]
    args.output.mkdir(parents=True,exist_ok=True)
    with (args.output/'actual_rules.inc').open('x',encoding='utf8') as f:f.write('\n\n'.join(parts)+'\n')
    with (args.output/'rules-extracted.json').open('x',encoding='utf8') as f:json.dump(dict(
        source=str(args.source.resolve()),sourceSha256=hashlib.sha256(raw).hexdigest(),
        methods=[dict(signature=s,sha256=hashlib.sha256(p.encode()).hexdigest()) for s,p in zip(SIGNATURES,parts)]),f,indent=2)
if __name__=='__main__':main()
