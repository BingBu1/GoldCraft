"""Extract and exercise actual ReHLDS reconnect methods with controlled edges."""
from pathlib import Path
import argparse
import hashlib
import json


def extract(text, name):
    start=text.index('void '+name+'(void)')
    end=text.index('\n}\n',start)+3
    return text[start:end]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    raw=args.source.read_bytes()
    source=raw.decode('utf-8-sig').replace('\r\n','\n')
    body=extract(source,'SV_New_f')+'\n'+extract(source,'SV_CheckTimeouts')
    args.output.mkdir(parents=True,exist_ok=False)
    target=args.output/'actual_reconnect.inc'
    target.write_text(body,encoding='utf-8',newline='\n')
    (args.output/'extraction.json').write_text(json.dumps(dict(source=str(args.source.resolve()),
        sourceSha256=hashlib.sha256(raw).hexdigest(),extractedSha256=hashlib.sha256(target.read_bytes()).hexdigest(),
        methods=['SV_New_f','SV_CheckTimeouts'],compiled=False,gameplay=False),indent=2)+'\n',encoding='utf-8')


if __name__=='__main__':
    main()
