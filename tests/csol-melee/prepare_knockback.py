"""Extract actual ordinary-monster predicates for the focused regression."""
from pathlib import Path
import argparse, hashlib, json, re

def extract(path,signature):
    text=path.read_text('utf-8-sig');start=text.index(signature);end=text.index('{',start)+1;depth=1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}');end+=1
    return text[start:end]+'\n'

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',type=Path,required=True)
    parser.add_argument('--old-damage',type=Path,required=True)
    parser.add_argument('--new-damage',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();args.output.mkdir(parents=True)
    owner=args.source/'native/csol/server/wpn_melee.cpp'
    outputs={
        'knockback_identity.inc':extract(owner,'int CSOL_MeleeItemIdentity(')+extract(owner,'bool CSOL_MeleeEmptyMonsterKillCallbacks('),
        'knockback_new.inc':extract(args.new_damage,'bool ApplyOrdinaryKnockback('),
        'knockback_old.inc':extract(args.old_damage,'bool ApplyOrdinaryKnockback(')}
    names=sorted(set(re.findall(r'\b(?:WEAPON|CSOL_WEAPON)_[A-Z0-9_]+',outputs['knockback_new.inc'])))
    outputs['knockback_constants.inc']='enum {'+','.join(names)+'};\n'
    for name,text in outputs.items():(args.output/name).write_text(text,encoding='utf8')
    rows=[dict(path=str(p.resolve()),sha256=hashlib.sha256(p.read_bytes()).hexdigest())for p in [owner,args.old_damage,args.new_damage]]
    (args.output/'extraction.json').write_text(json.dumps(dict(sources=rows,generated=[dict(name=n,sha256=hashlib.sha256(t.encode()).hexdigest())for n,t in outputs.items()]),indent=2),encoding='utf8')
if __name__=='__main__':main()
