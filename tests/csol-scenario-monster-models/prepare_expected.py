"""Generate comparison data only from frozen original records, never production."""
import argparse
import hashlib
import json
import struct
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]

def identity(path):
    data=path.read_bytes()
    return {'path':str(path),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--output',type=Path,required=True);args=parser.parse_args()
    catalog_file=ROOT/'analysis/csol-model-20261017/scenario/monster-types/appearance-catalog.json'
    assert identity(catalog_file)['sha256']=='6dc8535729df3aac358c24453fc17d6c8aecf85d3a2c03b822607ca6ce8a40cb'
    catalog=json.loads(catalog_file.read_text(encoding='utf8'))
    local_file=ROOT/'analysis/csol-model-20261017/scenario/monster-attributes/deadend-appearances.json'
    local=json.loads(local_file.read_text(encoding='utf8'))
    assert catalog['count']==143 and len(catalog['entries'])==143
    lines=['// Generated from independently frozen original catalog records.',
        'struct ExpectedAppearance { unsigned index; const char* name; const char* model; std::array<std::uint32_t, 6> bounds; bool summary; };',
        'inline constexpr std::array<ExpectedAppearance,143> ExpectedCatalog{{']
    for index,item in enumerate(catalog['entries']):
        assert item['index']==index
        raw=bytes.fromhex(item['rawHex']);assert len(raw)==60
        assert list(struct.unpack_from('<6f',raw,8))==item['mins']+item['maxs']
        bits=struct.unpack_from('<6I',raw,8);summary=struct.unpack_from('<I',raw,0x38)[0];assert summary in [0,1]
        lines.append('  {'+','.join([str(index),json.dumps(item['name']),json.dumps(item['model']),
            '{'+','.join(str(value)+'u' for value in bits)+'}','true' if summary else 'false'])+'},')
    lines+=['}};','struct ExpectedTypeAppearance { int type; unsigned appearance; };',
        'inline constexpr std::array<ExpectedTypeAppearance,38> ExpectedDeadend{{']
    mappings=sorted((type_id,item['index']) for item in local['entries'] for type_id in item['typeIDs'])
    assert len(local['entries'])==13 and len(mappings)==38
    for type_id,index in mappings:lines.append(f'  {{{type_id},{index}}},')
    lines+=['}};','struct ExpectedDuplicate { const char* name; unsigned first, later; };',
        'inline constexpr std::array<ExpectedDuplicate,4> ExpectedDuplicates{{']
    assert len(local['duplicateNames'])==4
    for item in local['duplicateNames']:
        assert item['winner']==item['indices'][0] and len(item['indices'])==2
        lines.append('  {'+json.dumps(item['name'])+','+str(item['indices'][0])+','+str(item['indices'][1])+'},')
    lines+=['}};','']
    args.output.mkdir(parents=True,exist_ok=True)
    header=args.output/'expected_catalog.hpp';assert not header.exists()
    header.write_text('\n'.join(lines),encoding='utf8')
    manifest={'scope':'143 original raw bounds/name/model/summary entries, 4 duplicate names and 38 original deadend type mappings; not a test result',
        'inputs':[identity(catalog_file),identity(local_file),identity(Path(__file__))],'output':identity(header)}
    (args.output/'expected-catalog.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2)+'\n',encoding='utf8')
    print(json.dumps(identity(args.output/'expected-catalog.json')))

if __name__=='__main__':main()
